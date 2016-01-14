var kAccountsFile = "data/auth/accounts.json";
var kPermissionsFile = "data/auth/permissions.json";

var gAccounts = {};
var gPermissions = {};
var gSessions = {};
var gTokens = {};

var bCryptLib = require('bCrypt');
bCrypt = new bCryptLib.bCrypt();

var form = require('form');

try {
	gAccounts = JSON.parse(File.readFile(kAccountsFile));
} catch (error) {
}

try {
	gPermissions = JSON.parse(File.readFile(kPermissionsFile));
} catch (error) {
}

function getCookies(headers) {
	var cookies = {};

	if (headers.cookie) {
		var parts = headers.cookie.split(/,|;/);
		for (var i in parts) {
			var equals = parts[i].indexOf("=");
			var name = parts[i].substring(0, equals).trim();
			var value = parts[i].substring(equals + 1).trim();
			cookies[name] = value;
		}
	}

	return cookies;
}

function newSession() {
	var alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	var result = "";
	for (var i = 0; i < 32; i++) {
		result += alphabet.charAt(Math.floor(Math.random() * alphabet.length));
	}
	return result;
}

function getPermissions(session) {
	var permissions = {};
	if (session && gSessions[session]) {
		permissions.authenticated = true;
		if (gPermissions[gSessions[session].name]) {
			for (var i in gPermissions[gSessions[session].name]) {
				permissions[gPermissions[gSessions[session].name][i]] = true;
			}
		}
	}
	return permissions;
}

function verifyPassword(password, hash) {
	return bCrypt.hashpw(password, hash) == hash;
}

function hashPassword(password) {
	var salt = bCrypt.gensalt(12);
	return bCrypt.hashpw(password, salt);
}

function authHandler(request, response) {
	var session = getCookies(request.headers).session;
	if (request.uri == "/login") {
		var sessionIsNew = false;
		var loginError;

		if (request.method == "POST") {
			// XXX: Assume a post is a new login attempt.
			session = newSession();
			sessionIsNew = true;
			var formData = form.decodeForm(request.body);
			if (formData.register == "1") {
				if (!gAccounts[formData.name] &&
					formData.password == formData.confirm) {
					gAccounts[formData.name] = {password: hashPassword(formData.password)};
					gSessions[session] = {name: formData.name};
					File.writeFile(kAccountsFile, JSON.stringify(gAccounts));
				} else {
					loginError = "Error registering account.";
				}
			} else {
				if (gAccounts[formData.name] &&
					verifyPassword(formData.password, gAccounts[formData.name].password)) {
					gSessions[session] = {name: formData.name};
				} else {
					loginError = "Invalid username or password.";
				}
			}
		}

		var queryForm = form.decodeForm(request.query);

		var cookie = "session=" + session + "; path=/; Max-Age=604800";
		if (session
			&& gSessions[session]
			&& queryForm.return) {
			response.writeHead(303, {"Location": queryForm.return, "Connection": "close", "Set-Cookie": cookie});
			response.end();
		} else {
			response.writeHead(200, {"Content-Type": "text/html", "Connection": "close", "Set-Cookie": cookie});
			var html = File.readFile("core/auth.html");
			var contents = "";

			if (session && gSessions[session]) {
				if (sessionIsNew) {
					contents += '<div>Welcome back, ' + gSessions[session].name + '.</div>\n';
				} else {
					contents += '<div>You are already logged in, ' + gSessions[session].name + '.</div>\n';
				}
				contents += '<div><a href="/login/logout">Logout</a></div>\n';
			} else {
				contents += '<form method="POST">\n';
				if (loginError) {
					contents += "<p>" + loginError + "</p>\n";
				}
				contents += '<p><b>Halt.  Who goes there?</b></p>\n'
				contents += '<div><label for="name">Name:</label> <input type="text" id="name" name="name" value=""></input></div>\n';
				contents += '<div><label for="password">Password:</label> <input type="password" id="password" name="password" value=""></input></div>\n';
				contents += '<div id="confirmPassword" style="display: none"><label for="confirm">Confirm:</label> <input type="password" id="confirm" name="confirm" value=""></input></div>\n';
				contents += '<div><input type="checkbox" id="register" name="register" value="1" onchange="showHideConfirm()"></input> <label for="register">Register a new account</label></div>\n';
				contents += '<div><input type="submit" value="Login"></input></div>\n';
				contents += '</form>';
			}
			response.end(html.replace("$(SESSION)", contents));
		}
	} else if (request.uri == "/login/logout") {
		delete gSessions[session];
		response.writeHead(303, {"Connection": "close", "Set-Cookie": "session=; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT", "Location": "/login" + (request.query ? "?" + request.query : "")});
		response.end();
	} else {
		response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
		response.end("Hello, " + request.client.peerName + ".");
	}
}

function query(headers) {
	var session = getCookies(headers).session;
	if (session && gSessions[session]) {
		return {session: gSessions[session], permissions: getPermissions(session)};
	}
}

function makeToken(session, task) {
	var now = new Date();
	var random = Math.random();
	var token = hashPassword(task + ":" + session + ":" + random + ":" + now.toString());
	gTokens[token] = {session: session, granted: now, random: random, task: task};
	return {user: gSessions[session].name, token: token};
}

function getCredentials(headers, task) {
	var session = getCookies(headers).session;
	if (session && gSessions[session]) {
		return makeToken(session, task);
	}
}

function transferCredentials(credentials, task) {
	if (verifyCredentials.bind(this)(credentials)) {
		var key = JSON.stringify([credentials.user, this.taskName, task]);
		if (!gTokens[key]) {
			gTokens[key] = makeToken(gTokens[credentials.token].session, task);
		}
		return gTokens[key];
	}
}

function verifyCredentials(credentials) {
	if (gTokens[credentials.token]
		&& gTokens[credentials.token].task == this.taskName
		&& gSessions[gTokens[credentials.token].session]
		&& gSessions[gTokens[credentials.token].session].name == credentials.user) {
		return {permissions: getPermissions(gTokens[credentials.token].session)};
	} else {
		throw new Error("Access denied.");
	}
}

exports.handler = authHandler;
exports.query = query;
