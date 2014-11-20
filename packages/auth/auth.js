var gSessions = {};

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

function newSession() {
	var alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	var result = "";
	for (var i = 0; i < 32; i++) {
		result += alphabet.charAt(Math.floor(Math.random() * alphabet.length));
	}
	return result;
}

function onMessage(from, message) {
	if (message.request) {
		var session = getCookies(message.request.headers).session;
		if (message.request.uri == "/login") {
			var sessionIsNew = false;

			if (message.request.method == "POST") {
				// XXX: Assume a post is a new login attempt.
				session = newSession();
				sessionIsNew = true;
				gSessions[session] = {name: message.request.body.substring(message.request.body.indexOf("=") + 1)};
			}

			var cookie = "session=" + session + "; path=/; Max-Age=604800";
			if (session
				&& gSessions[session]
				&& message.request.query
				&& message.request.query.substring(0, 7) == "return=") {
				message.response.writeHead(303, {"Location": message.request.query.substring(7), "Connection": "close", "Set-Cookie": cookie});
				message.response.end();
			} else {
				message.response.writeHead(200, {"Content-Type": "text/html", "Connection": "close", "Set-Cookie": cookie});
				parent.invoke({to: "system", action: "get", fileName: "index.html"}).then(function(html) {
					var contents = "";

					if (session && gSessions[session]) {
						if (sessionIsNew) {
							contents += '<div>I made you a new session, ' + gSessions[session].name + '.</div>\n';
						} else {
							contents += '<div>Welcome back, ' + gSessions[session].name + '.</div>\n';
						}
						contents += '<div><a href="/login/logout">Logout</a></div>\n';
					} else {
						contents += '<form method="POST">\n';
						contents += '<div>Maybe you would like to log in?</div>\n'
						contents += '<div><label for="name">Name:</label> <input type="text" id="name" name="name" value=""></input></div>\n';
						contents += '<div><input type="submit" value="Submit"></input></div>\n';
						contents += '</form>';
					}
					message.response.end(html.replace("$(SESSION)", contents));
				});
			}
		} else if (message.request.uri == "/login/logout") {
			delete gSessions[session];
			message.response.writeHead(303, {"Connection": "close", "Set-Cookie": "session=; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT", "Location": "/login"});
			message.response.end();
		} else {
			message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
			message.response.end("Hello, " + message.request.client.peerName + ".");
		}
	} else if (message.action == "query") {
		print(message.headers);
		var session = getCookies(message.headers).session;
		print(session);
		print(gSessions[session]);
		if (session && gSessions[session]) {
			return {session: gSessions[session]};
		}
	}
}