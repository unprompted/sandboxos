function onMessage(from, message) {
	if (message.request) {
		parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\n\nHello, " + message.request.client.peerName + "!", messageId: message.messageId});
	}
}