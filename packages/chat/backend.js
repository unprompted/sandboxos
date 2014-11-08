// handler.js - handles the handling
var waiting = [];
var index = 0;
var messages = [];
		
var kStaticFiles = [
	{uri: '/chat', path: 'index.html', type: 'text/html'},
	{uri: '/chat/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

function onMessage(from, message) {
	print(message);
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
			if (message.request.uri == "/chat/send") {
				messages[index++] = message.request.body;
				for (var i in waiting) {
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + JSON.stringify({index: index, message: message.request.body}),
						messageId: waiting[i],
					});
				}
				waiting.slice(0);
				parent.invoke({
					to: "httpd",
					response: "HTTP/1.0 200 OK\nContent-Type: text/html\nConnection: close\n\nOK",
					messageId: message.messageId,
				});
			} else if (message.request.uri == "/chat/receive") {
				var haveIndex = parseInt(message.request.body);
				if (haveIndex + 1 < index) {
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + JSON.stringify({index: haveIndex + 1, message: messages[haveIndex + 1]}),
						messageId: message.messageId,
					});
				} else {
					waiting.push(message.messageId);
				}
			}
		}
	}
	return true;
}
