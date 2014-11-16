function add(a, b) {
	print("someone invoked my add function");
	print([a, b]);
	return a + b;
}

function onMessage(from, message) {
	if (message.request) {
		message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
		message.response.end("Hello, " + message.request.client.peerName + ".");
	}
}
