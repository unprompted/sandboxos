function b() {
	throw("hi");
}

function a() {
	b();
}

function onMessage(from, message) {
	var contents = "Yo!";
	for (var i = 0; i < 10; i++) {
		contents += " " + (i * i);
		a();
	}
	parent.invoke({
		to: "httpd",
		response: "HTTP 1.0/OK\nContent-Type: text/plain\nConnection: close\n\n" + contents,
		messageId: message.messageId
	});
	return true;
}
