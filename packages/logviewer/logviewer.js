var kStaticFiles = [
	{uri: '/log', path: 'index.html', type: 'text/html'},
	{uri: '/log/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

function handleStatic(request, response) {
	var found = false;
	for (var i in kStaticFiles) {
		if (kStaticFiles[i].uri == request.uri) {
			found = true;
			var file = kStaticFiles[i];
			imports.system.getPackageFile(file.path).then(function(data) {
				response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
				response.end(data);
			});
			break;
		}
	}
	if (!found) {
		response.writeHead(404, {"Content-Type": "text/plain", "Connection": "close"});
		response.end("404 Not found");
	}
}

function handleLog(request, response) {
	var start = Number(request.body);
	imports.log.getMessages(start).then(function(messages) {
		response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
		response.end(JSON.stringify(messages));
	});
}

imports.httpd.all("/log/get", handleLog);
imports.httpd.all("/log", handleStatic);
