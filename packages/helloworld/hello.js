function add(a, b) {
	print("someone invoked my add function");
	print([a, b]);
	return a + b;
}

function onMessage(from, message) {
	if (message.request) {
		message.request.respond("HTTP/1.0 200 OK\nContent-Type: text/plain\n\nHello, " + message.request.client.peerName + ".");
	}
}
