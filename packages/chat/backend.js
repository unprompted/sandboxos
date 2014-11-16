var waiting = [];
var index = 0;
var messages = [];

//are these ever used... ?
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
					message.response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
					message.response.end(data);
				});
				break;
			}
		}
		if (!found) {
			if (message.request.uri == "/chat/send") {
				messages[index++] = message.request.body;
				for (var i in waiting) {
					waiting[i].response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					waiting[i].response.end(JSON.stringify({index: index, message: message.request.body}));
				}
				waiting.slice(0);
				message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
				message.response.end("OK");
			} else if (message.request.uri == "/chat/receive") {
				var haveIndex = parseInt(message.request.body);
				if (haveIndex + 1 < index) {
					message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					message.response.end(JSON.stringify({index: haveIndex + 1, message: messages[haveIndex + 1]}));
				} else {
					waiting.push(message);
				}
			}
		}
	}
	return true;
}
