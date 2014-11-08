function onMessage(from, message) {
	print("editor received: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	parent.invoke({to: "httpd", response: "HTTP 1.0/OK\nContent-Type: text/plain\nConnection: close\n\nEDITOR!", messageId: message.messageId});
	return true;
}
