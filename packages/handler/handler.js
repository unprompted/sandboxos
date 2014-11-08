var votes = {a: 0, b: 0, c: 0};

function onMessage(from, message) {
	var contents;
	if (message.request.uri == "/handler/post") {
		var vote = message.request.body[0].toLowerCase();
		votes[vote] += 1;
	}
	parent.invoke({
		to: "system",
		action: "get",
		fileName: "index.html",
	}).then(function(data) {
		var html = data.replace("$A", votes.a).replace("$B", votes.b).replace("$C", votes.c);
		parent.invoke({
			to: "httpd",
			response: "HTTP 1.0/OK\nContent-Type: text/html\nConnection: close\n\n" + html,
			messageId: message.messageId,
		});
	});
	return true;
}