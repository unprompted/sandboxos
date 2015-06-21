function handler(request, response) {
	if (request.uri == "/packagegraph/vivagraph.min.js") {
		response.writeHead(200, {"Content-Type": "text/javascript", "Connection": "close"});

		return imports.filesystem.getPackage().then(function(fs) {
			return fs.readFile("vivagraph.min.js").then(function(data) {
				response.end(data);
			});
		});
	} else if (request.uri == "/packagegraph/packages.js") {
		response.writeHead(200, {"Content-Type": "text/javascript", "Connection": "close"});

		return imports.tasks.getTaskManifests().then(function(data) {
			response.end("var packages = " + JSON.stringify(data));
		});
	} else {
		response.writeHead(200, {"Content-Type": "text/html", "Connection": "close"});

		return imports.filesystem.getPackage().then(function(fs) {
			return fs.readFile("index.html").then(function(html) {
				response.end(html);
			});
		});
	}
}

imports.httpd.get('/packagegraph', handler);
