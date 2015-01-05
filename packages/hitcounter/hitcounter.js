var count = 0;
var db;

imports.filesystem.getPackageDatabase().then(function(d) {
	db = d;
	db.get("count").then(function(c) {
		count = parseInt(c) || 0;
	});
});

function sayHello(request, response) {
	response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});

	++count;
	db.set("count", count).then(function() {
		response.end("Hello, " + request.client.peerName + ".  You are visitor number " + count + ".");
	}).catch(function(e) {
		response.end("Do you know what this means?: " + e);
	});
}

imports.httpd.get('/hitcounter', sayHello);
