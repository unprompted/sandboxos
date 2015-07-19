function decode(encoded) {
	var result = "";
	for (var i = 0; i < encoded.length; i++) {
		var c = encoded[i];
		if (c == "+") {
			result += " ";
		} else if (c == "%") {
			result += String.fromCharCode(parseInt(encoded.slice(i + 1, i + 3), 16));
			i += 2;
		} else {
			result += c;
		}
	}
	return result;
}

function decodeForm(encoded) {
	var result = {};
	if (encoded) {
		encoded = encoded.trim();
		var items = encoded.split('&');
		for (var i = 0; i < items.length; i++) {
			var item = items[i];
			var equals = item.indexOf('=');
			var key = decode(item.slice(0, equals));
			var value = decode(item.slice(equals + 1));
			result[key] = value;
		}
	}
	return result;
}

function parsePart(request, data) {
	var headers = {};
	while (true) {
		var index = data.indexOf("\r\n");
		if (index === 0) {
			data = data.substring("\r\n".length);
			break;
		} else if (index > 0) {
			var line = data.substring(0, index);
			data = data.substring(index + "\r\n".length);
			var colon = line.indexOf(':');
			headers[line.substring(0, colon).toLowerCase()] = line.substring(colon + 1).trim();
		}
	}

	if (!request.multipart) {
		request.multipart = [];
	}
	request.multipart.push({headers: headers, data: data});
}

function parseForm(request) {
	var contentType = request.headers["content-type"];
	if (contentType.indexOf("multipart/form-data;") === 0) {
		var index = contentType.indexOf("boundary=");
		var boundary = contentType.substring(index + "boundary=".length);
		var startToken = "--" + boundary + "\r\n";
		var endToken = "\r\n--" + boundary + "--\r\n";
		var startIndex = request.body.indexOf(startToken);
		var endIndex = request.body.indexOf(endToken);
		if (startIndex != -1 && endIndex != -1) {
			parsePart(request, request.body.substring(startIndex + startToken.length, endIndex));
		}
	}
}

function sayHello(request, response) {
	if (request.method == "POST") {
		parseForm(request);
		return imports.filesystem.getPackageData().then(function(fs) {
			return fs.ensureDirectoryTreeExists(".").then(function() {
				return Promise.all([
					fs.writeFile("data", request.multipart[0].data),
					fs.writeFile("contentType", request.multipart[0]["content-type"]),
				]).then(function() {
					response.writeHead(303, {"Location": request.uri, "Connection": "close"});
					response.end();
				});
			});
		});
	} else if (request.uri == "/upload") {
		response.writeHead(200, {"Content-Type": "text/html", "Connection": "close"});
		return imports.filesystem.getPackage().then(function(fs) {
			return fs.readFile("index.html").then(function(data) {
				return response.end(data);
			});
		});
	} else if (request.uri == "/upload/image") {
		return imports.filesystem.getPackageData().then(function(fs) {
			return Promise.all([
				fs.readFile("data"),
				fs.readFile("contentType"),
			]).then(function(results) {
				response.writeHead(200, {"Content-Type": results[1], "Connection": "close"});
				return response.end(results[0]);
			});
		});
	}
}

imports.httpd.get('/upload', sayHello);
imports.httpd.all('/upload', sayHello);