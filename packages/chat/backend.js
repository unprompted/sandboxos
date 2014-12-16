var waiting = [];
var index = 0;
var messages = [];

var kStaticFiles = [
	{uri: '/chat', path: 'index.html', type: 'text/html'},
	{uri: '/chat/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

var packageFs;
imports.filesystem.getPackage().then(function(fs) { packageFs = fs; });

function sessionHandler(request, response, session) {
	var found = false;
	for (var i in kStaticFiles) {
		if (kStaticFiles[i].uri == request.uri) {
			found = true;
			var file = kStaticFiles[i];
			packageFs.readFile(file.path).then(function(data) {
				response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
				response.end(data);
			});
			break;
		}
	}
	if (!found) {
		if (request.uri == "/chat/send") {
			var newMessage = "<" + session.name + "> " + request.body;
			messages[index++] = newMessage;
			for (var i in waiting) {
				waiting[i].response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
				waiting[i].response.end(JSON.stringify({index: index, message: newMessage}));
			}
			waiting.slice(0);
			response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
			response.end("OK");
		} else if (request.uri == "/chat/receive") {
			var haveIndex = parseInt(request.body);
			if (haveIndex + 1 < index) {
				response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
				response.end(JSON.stringify({index: haveIndex + 1, message: messages[haveIndex + 1]}));
			} else {
				waiting.push({request: request, response: response});
			}
		}
	}
}

function handler(request, response) {
	imports.auth.query(request.headers).then(function(authResponse) {
		if (authResponse) {
			sessionHandler(request, response, authResponse.session);
		} else {
			response.writeHead(303, {"Location": "/login?return=/chat", "Connection": "close"});
			response.end();
		}
	});
}

imports.httpd.all("/chat", handler);
