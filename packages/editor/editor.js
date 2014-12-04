function escapeHtml(value) {
	var kMap = {
		"&": "&amp;",
		"<": "&lt;",
		">": "&gt;",
	};
	return value.replace(/[&<>]/g, function(v) { return kMap[v]; });
}

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

var kStaticFiles = [
	{uri: '/editor', path: 'index.html', type: 'text/html'},
	{uri: '/editor/codemirror-compressed.js', path: 'codemirror-compressed.js', type: 'text/javascript'},
	{uri: '/editor/codemirror.css', path: 'codemirror.css', type: 'text/css'},
	{uri: '/editor/lesser-dark.css', path: 'lesser-dark.css', type: 'text/css'},
	{uri: '/editor/script.js', path: 'script.js', type: 'text/javascript'},
];

function copyFile(oldPackage, newPackage, fileName) {
	return imports.system.getPackageFile(fileName, oldPackage).then(function(result) {
		return imports.system.putPackageFile(fileName, result, newPackage);
	});
}

function handler(request, response) {
	var handled = false;
	var match;
	for (var i in kStaticFiles) {
		var file = kStaticFiles[i];
		if (request.uri == file.uri) {
			match = file;
		}
	}
	if (new RegExp("^/editor/[^/]+/$").exec(request.uri)) {
		match = {path: "package.html", type: "text/html"};
	}
	if (match) {
		imports.system.getPackageFile(match.path).then(function(contents) {
			response.writeHead(200, {"Content-Type": match.type, "Connection": "close", "Content-Length": contents.length});
			response.end(contents);
		});
		handled = true;
	}
	if (!handled) {
		var regex = new RegExp("^/editor/([^/]+)/(.*)$");
		match = regex.exec(request.uri);
		if (match) {
			var packageName = match[1];
			var action = match[2];
			handled = true;
			if (action == "get") {
				var form = decodeForm(request.query);
				imports.system.getPackageFile(form.fileName, packageName).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(result);
				});
			} else if (action == "list") {
				var form = decodeForm(request.query);
				imports.system.listPackageFiles(packageName).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				});
			} else if (action == "put") {
				var form = decodeForm(request.body);
				imports.system.putPackageFile(form.fileName, JSON.parse(form.contents), packageName).then(function(result) {
					return imports.system.restartTask(packageName);
				}).then(function() {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end("updated");
				});
			} else if (action == "new") {
				var form = decodeForm(request.query);
				imports.system.createPackage(packageName).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(error);
				});
			} else if (action == "unlink") {
				var form = decodeForm(request.query);
				imports.system.unlinkPackageFile(form.fileName, packageName).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(error);
				});
			} else if (action == "rename") {
				var form = decodeForm(request.query);
				imports.system.renamePackageFile(form.oldName, form.newName, packageName).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(error);
				});
			} else if (action == "clone") {
				var form = decodeForm(request.query);
				var oldName = packageName;
				var newName = form.newName;
				imports.system.createPackage(newName).then(function() {
					imports.system.listPackageFiles(oldName).then(function(oldPackageContents) {
						promises = [];
						for (var i in oldPackageContents) {
							promises.push(copyFile(oldName, newName, oldPackageContents[i]));
						}
						return Promise.all(promises);
					});
				}).then(function(data) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end("cloned");
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(error);
				});
			} else {
				handled = false;
			}
		}
	}
	if (!handled) {
		response.writeHead(404, {"Content-Type": "text/plain", "Connection": "close"});
		response.end("404 Not found");
	}
}

imports.httpd.all("/editor", handler);
