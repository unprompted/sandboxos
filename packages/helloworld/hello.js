function sayHello(request, response) {
	response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
	response.end("Hello, " + request.client.peerName + ".");
}

imports.httpd.get('/helloworld', sayHello);
