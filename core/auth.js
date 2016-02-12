"use strict";
var kAccountsFile = "data/auth/accounts.json";

var gAccounts = {};
var gTokens = {};

var bCryptLib = require('bCrypt');
bCrypt = new bCryptLib.bCrypt();

var form = require('form');

File.makeDirectory("data");
File.makeDirectory("data/auth");
File.makeDirectory("data/auth/db");
var gDatabase = new Database("data/auth/db");

try {
	gAccounts = JSON.parse(File.readFile(kAccountsFile));
} catch (error) {
}

function readSession(session) {
	var result = session ? gDatabase.get("session_" + session) : null;

	if (result) {
		result = JSON.parse(result);

		let kRefreshInterval = 1 * 60 * 60 * 1000;
		let now = Date.now();
		if (!result.lastAccess || result.lastAccess < now - kRefreshInterval) {
			result.lastAccess = now;
			writeSession(session, result);
		}
	}

	return result;
}

function writeSession(session, value) {
	gDatabase.set("session_" + session, JSON.stringify(value));
}

function removeSession(session, value) {
	gDatabase.remove("session_" + session);
}

function newSession() {
	var alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	var result = "";
	for (var i = 0; i < 32; i++) {
		result += alphabet.charAt(Math.floor(Math.random() * alphabet.length));
	}
	return result;
}

function verifyPassword(password, hash) {
	return bCrypt.hashpw(password, hash) == hash;
}

function hashPassword(password) {
	var salt = bCrypt.gensalt(12);
	return bCrypt.hashpw(password, salt);
}

function noAdministrator() {
	return !gGlobalSettings || !gGlobalSettings.permissions || !Object.keys(gGlobalSettings.permissions).some(function(name) {
		return gGlobalSettings.permissions[name].indexOf("administration") != -1;
	});
}

function makeAdministrator(name) {
	if (!gGlobalSettings.permissions) {
		gGlobalSettings.permissions = {};
	}
	if (!gGlobalSettings.permissions[name]) {
		gGlobalSettings.permissions[name] = [];
	}
	if (gGlobalSettings.permissions[name].indexOf("administration") == -1) {
		gGlobalSettings.permissions[name].push("administration");
	}
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
			if (formData.submit == "Login") {
				if (formData.register == "1") {
					if (!gAccounts[formData.name] &&
						formData.password == formData.confirm) {
						gAccounts[formData.name] = {password: hashPassword(formData.password)};
						writeSession(session, {name: formData.name});
						File.writeFile(kAccountsFile, JSON.stringify(gAccounts));
						makeAdministrator(formData.name);
					} else {
						loginError = "Error registering account.";
					}
				} else {
					if (gAccounts[formData.name] &&
						verifyPassword(formData.password, gAccounts[formData.name].password)) {
						writeSession(session, {name: formData.name});
						makeAdministrator(formData.name);
					} else {
						loginError = "Invalid username or password.";
					}
				}
			} else {
				// Proceed as Guest
				writeSession(session, {name: "guest"});
			}
		}

		var queryForm = form.decodeForm(request.query);

		var cookie = "session=" + session + "; path=/; Max-Age=604800";
		var entry = readSession(session);
		if (entry && queryForm.return) {
			response.writeHead(303, {"Location": queryForm.return, "Set-Cookie": cookie});
			response.end();
		} else {
			var html = File.readFile("core/auth.html");
			var contents = "";

			if (entry) {
				if (sessionIsNew) {
					contents += '<div>Welcome back, ' + entry.name + '.</div>\n';
				} else {
					contents += '<div>You are already logged in, ' + entry.name + '.</div>\n';
				}
				contents += '<div><a href="/login/logout">Logout</a></div>\n';
			} else {
				contents += '<form method="POST">\n';
				if (loginError) {
					contents += "<p>" + loginError + "</p>\n";
				}
				contents += '<div id="auth_greeting"><b>Halt.  Who goes there?</b></div>\n'
				contents += '<div id="auth">\n';
				contents += '<div id="auth_login">\n'
				if (noAdministrator()) {
					contents += '<div class="notice">There is currently no administrator.  You will be made administrator.</div>\n';
				}
				contents += '<div><label for="name">Name:</label> <input type="text" id="name" name="name" value=""></input></div>\n';
				contents += '<div><label for="password">Password:</label> <input type="password" id="password" name="password" value=""></input></div>\n';
				contents += '<div id="confirmPassword" style="display: none"><label for="confirm">Confirm:</label> <input type="password" id="confirm" name="confirm" value=""></input></div>\n';
				contents += '<div><input type="checkbox" id="register" name="register" value="1" onchange="showHideConfirm()"></input> <label for="register">Register a new account</label></div>\n';
				contents += '<div><input id="loginButton" type="submit" name="submit" value="Login"></input></div>\n';
				contents += '</div>';
				contents += '<div id="auth_or"> - or - </div>';
				contents += '<div id="auth_guest">\n';
				contents += '<input id="guestButton" type="submit" name="submit" value="Proceeed as Guest"></input>\n';
				contents += '</div>\n';
				contents += '</div>\n';
				contents += '</form>';
			}
			var text = html.replace("$(SESSION)", contents);
			response.writeHead(200, {"Content-Type": "text/html; charset=utf-6", "Set-Cookie": cookie, "Content-Length": text.length});
			response.end(text);
		}
	} else if (request.uri == "/login/logout") {
		removeSession(session);
		response.writeHead(303, {"Set-Cookie": "session=; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT", "Location": "/login" + (request.query ? "?" + request.query : "")});
		response.end();
	} else {
		response.writeHead(200, {"Content-Type": "text/plain; charset=utf-8", "Connection": "close"});
		response.end("Hello, " + request.client.peerName + ".");
	}
}

function getPermissions(session) {
	var permissions;
	var entry = readSession(session);
	if (entry) {
		permissions = getPermissionsForUser(entry.name);
		permissions.authenticated = entry.name !== "guest";
	}
	return permissions || {};
}

function getPermissionsForUser(userName) {
	var permissions = {};
	if (gGlobalSettings && gGlobalSettings.permissions && gGlobalSettings.permissions[userName]) {
		for (var i in gGlobalSettings.permissions[userName]) {
			permissions[gGlobalSettings.permissions[userName][i]] = true;
		}
	}
	return permissions;
}

function query(headers) {
	var session = getCookies(headers).session;
	var entry;
	if (entry = readSession(session)) {
		return {session: entry, permissions: getPermissions(session)};
	}
}

exports.handler = authHandler;
exports.query = query;
