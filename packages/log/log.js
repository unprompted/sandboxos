var kMessageLimit = 1024;
var gMessages = [];
var gMessageIndex = 0;
var gWaiting = [];

var kStaticFiles = [
	{uri: '/log', path: 'index.html', type: 'text/html'},
	{uri: '/log/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

function sendMessages(message, start) {
	var messages = gMessages;
	if (start) {
		var realStart = Math.max(gMessages.length - gMessageIndex + start, 0);
		messages = messages.slice(realStart);
	}
	message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
	message.response.end(JSON.stringify({next: gMessageIndex, messages: messages}));
}

function onMessage(from, message) {
	if (message.request && message.response) {
		var found = false;
		for (var i in kStaticFiles) {
			if (kStaticFiles[i].uri == message.request.uri) {
				found = true;
				var file = kStaticFiles[i];
				parent.invoke({
					to: "system",
					action: "get",
					fileName: file.path,
				}).then(function(data) {
					message.response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
					message.response.end(data);
				});
				break;
			}
		}
		if (!found) {
			if (message.request.uri == "/log/get") {
				var start = Number(message.request.body);
				if (!start || start < gMessageIndex) {
					sendMessages(message, start);
				} else {
					gWaiting.push({start: start, message: message});
				}
			} else {
				message.response.writeHead(404, {"Content-Type": "text/plain", "Connection": "close"});
				message.response.end("404 Not found");
			}
		}
	} else {
		gMessages.push(message.payload);
		gMessageIndex++;
		if (gMessages.length > kMessageLimit) {
			gMessages.splice(0, gMessages.length - kMessageLimit);
		}

		for (var i in gWaiting) {
			sendMessages(gWaiting[i].message, gWaiting[i].start);
		}
		gWaiting.length = 0;
	}
}
