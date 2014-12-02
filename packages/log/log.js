var kMessageLimit = 256;
var gMessages = [];
var gMessageIndex = 0;
var gWaiting = [];

var kStaticFiles = [
	{uri: '/log', path: 'index.html', type: 'text/html'},
	{uri: '/log/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

function sendMessages(response, start) {
	var messages = gMessages;
	if (start) {
		var realStart = Math.max(gMessages.length - gMessageIndex + start, 0);
		messages = messages.slice(realStart);
	}
	response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
	response.end(JSON.stringify({next: gMessageIndex, messages: messages}));
}

function handle(request, response) {
	var found = false;
	for (var i in kStaticFiles) {
		if (kStaticFiles[i].uri == request.uri) {
			found = true;
			var file = kStaticFiles[i];
			parent.invoke({
				to: "system",
				action: "get",
				fileName: file.path,
			}).then(function(data) {
				response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
				response.end(data);
			});
			break;
		}
	}
	if (!found) {
		if (request.uri == "/log/get") {
			print(gMessages);
			var start = Number(request.body);
			if (!start || start < gMessageIndex) {
				sendMessages(response, start);
			} else {
				gWaiting.push({start: start, response: response});
			}
		} else {
			response.writeHead(404, {"Content-Type": "text/plain", "Connection": "close"});
			response.end("404 Not found");
		}
	}
}

function onMessage(from, message) {
	if (message.payload) {
		gMessages.push(message.payload);
		gMessageIndex++;
		if (gMessages.length > kMessageLimit) {
			gMessages.splice(0, gMessages.length - kMessageLimit);
		}

		for (var i in gWaiting) {
			sendMessages(gWaiting[i].response, gWaiting[i].start);
		}
		gWaiting.length = 0;
	}
}

imports.httpd.all("/log", handle);
