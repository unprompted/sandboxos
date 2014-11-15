function onMessage(from, message) {
	if (message.request) {
		parent.invoke({to: "httpd", messageId: message.messageId, response: "HTTP/1.0 200 OK\nContent-Type text/plain\n\nBuild log viewer here!"});
	}
}