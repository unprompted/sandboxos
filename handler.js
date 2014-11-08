function a() {
	var x = 42;
	for (var i = 0; i < 10000000; ++i) {
		for (var j = 0; j < 1000000; ++j) {
			x = [x, x, x, x, x, x, x, x, x];
		}
	}
	print("that wasn't so bad");
}

function onMessage(from, message) {
	var contents = "Yo!";
	for (var i = 0; i < 10; i++) {
		contents += " " + (i * i);
	}
	parent.invoke({
		to: "httpd",
		response: "HTTP 1.0/OK\nContent-Type: text/plain\nConnection: close\n\n" + contents,
		messageId: message.messageId
	});
	return true;
}
