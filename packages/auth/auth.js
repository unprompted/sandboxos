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

function handler(request, response) {
	var session = getCookies(request.headers).session;
	if (request.uri == "/login") {
		var sessionIsNew = false;

		if (request.method == "POST") {
			// XXX: Assume a post is a new login attempt.
			session = newSession();
			sessionIsNew = true;
			gSessions[session] = {name: request.body.substring(request.body.indexOf("=") + 1)};
		}

		var cookie = "session=" + session + "; path=/; Max-Age=604800";
		if (session
			&& gSessions[session]
			&& request.query
			&& request.query.substring(0, 7) == "return=") {
			response.writeHead(303, {"Location": request.query.substring(7), "Connection": "close", "Set-Cookie": cookie});
			response.end();
		} else {
			response.writeHead(200, {"Content-Type": "text/html", "Connection": "close", "Set-Cookie": cookie});
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
		print({session: gSessions[session]});
		return {session: gSessions[session]};
	}
}

imports.httpd.all("/login", handler);

exports = {
	query: query,
};
