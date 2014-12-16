function sayHello(request, response) {
	// Write some standard HTTP headers.
	response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});

	// Deliver a plain text response that greets the user's IP address.
	response.end("Hello, " + request.client.peerName + ".");
}

imports.filesystem.getPackageData().then(function(fs) {
	fs.readFile("hello.js").then(function(data) {
		print(data);
	});
}).catch(function(e) {
	print("E: " + e);
});

// Register for a callback anytime somebody visits /helloworld.
imports.httpd.get('/helloworld', sayHello);
