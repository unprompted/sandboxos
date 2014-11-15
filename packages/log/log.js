var kMessageLimit = 1024;
var gMessages = [];
var gMessageIndex = 0;
var gWaiting = [];

var kStaticFiles = [
	{uri: '/log', path: 'index.html', type: 'text/html'},
	{uri: '/log/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

function sendMessages(messageId, start) {
	var messages = gMessages;
	if (start) {
		var realStart = Math.max(gMessages.length - gMessageIndex + start, 0);
		messages = messages.slice(realStart);
	}
	parent.invoke({to: "httpd", messageId: messageId, response: "HTTP/1.0 200 OK\nContent-Type text/plain\n\n" + JSON.stringify({next: gMessageIndex, messages: messages})});
}

function onMessage(from, message) {
	if (message.request) {
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
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 200 OK\nContent-Type: " + file.type + "\nConnection: close\n\n" + data,
						messageId: message.messageId,
					});
				});
				break;
			}
		}
		if (!found) {
			if (message.request.uri == "/log/get") {
				var start = Number(message.request.body);
				if (!start || start < gMessageIndex) {
					sendMessages(message.messageId, start);
				} else {
					gWaiting.push({start: start, messageId: message.messageId});
				}
			} else {
				parent.invoke({to: "httpd", messageId: message.messageId, response: "HTTP/1.0 404 Not found\nContent-Type text/plain\n\nNot found"});
			}
		}
	} else {
		gMessages.push([from, message]);
		gMessageIndex++;
		if (gMessages.length > kMessageLimit) {
			gMessages.splice(0, gMessages.length - kMessageLimit);
		}

		for (var i in gWaiting) {
			sendMessages(gWaiting[i].messageId, gWaiting[i].start);
		}
		gWaiting.length = 0;
	}
}