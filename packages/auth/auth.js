var gAccounts = {};
var gPermissions = {};
var gSessions = {};

var bCryptLib = require('bCrypt');
bCrypt = new bCryptLib.bCrypt();

var packageFs = null;
var dataFs = null;
imports.filesystem.getPackage().then(function(fs) { packageFs = fs; });
imports.filesystem.getPackageData().then(function(fs) {
	dataFs = fs;

	dataFs.readFile("accounts.json").then(function(data) {
		gAccounts = JSON.parse(data);
	});
	dataFs.readFile("permissions.json").then(function(data) {
		gPermissions = JSON.parse(data);
	});
});

function getCookies(headers) {
	var cookies = {};

	if (headers.cookie) {
		var cookies = headers.cookie.split(/,|;/);
		for (var i in cookies) {
			var equals = cookies[i].indexOf("=");
			var name = cookies[i].substring(0, equals).trim();
			var value = cookies[i].substring(equals + 1).trim();
			cookies[name] = value;
		}
	}

	return cookies;
}

function decode(encoded) {
	var result = "";
	for (var i = 0; i < encoded.length; i++) {
		var c = encoded[i];
		if (c == "+") {
			result += " ";
		} else if (c == '%') {
			result += String.fromCharCode(parseInt(encoded.slice(i + 1, i + 3), 16));
			i += 2;
		} else {
			result += c;
		}
	}
	return result;
}

function decodeForm(encoded) {
	var result = {};
	if (encoded) {
		encoded = encoded.trim();
		var items = encoded.split('&');
		for (var i = 0; i < items.length; i++) {
			var item = items[i];
			var equals = item.indexOf('=');
			var key = decode(item.slice(0, equals));
			var value = decode(item.slice(equals + 1));
			result[key] = value;
		}
	}
	return result;
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

function handler(request, response) {
	var session = getCookies(request.headers).session;
	if (request.uri == "/login") {
		var sessionIsNew = false;
		var loginError;

		if (request.method == "POST") {
			// XXX: Assume a post is a new login attempt.
			session = newSession();
			sessionIsNew = true;
			var form = decodeForm(request.body);
			if (form.register == "1") {
				if (!gAccounts[form.name] &&
					form.password == form.confirm) {
					gAccounts[form.name] = {password: hashPassword(form.password)};
					gSessions[session] = {name: form.name};
					dataFs.writeFile("accounts.json", JSON.stringify(gAccounts));
				} else {
					loginError = "Error registering account.";
				}
			} else {
				if (gAccounts[form.name] &&
					verifyPassword(form.password, gAccounts[form.name].password)) {
					gSessions[session] = {name: form.name};
				} else {
					loginError = "Invalid username or password.";
				}
			}
		}

		var queryForm = decodeForm(request.query);

		var cookie = "session=" + session + "; path=/; Max-Age=604800";
		if (session
			&& gSessions[session]
			&& queryForm.return) {
			response.writeHead(303, {"Location": queryForm.return, "Connection": "close", "Set-Cookie": cookie});
			response.end();
		} else {
			response.writeHead(200, {"Content-Type": "text/html", "Connection": "close", "Set-Cookie": cookie});
			packageFs.readFile("index.html").then(function(html) {
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
			});
		}
	} else if (request.uri == "/login/logout") {
		delete gSessions[session];
		response.writeHead(303, {"Connection": "close", "Set-Cookie": "session=; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT", "Location": "/login"});
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

imports.httpd.all("/login", handler);

exports = {
	query: query,
};
