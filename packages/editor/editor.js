var kStaticFiles = [
	{uri: '/editor', path: 'index.html', type: 'text/html'},
	{uri: '/editor/codemirror-compressed.js', path: 'codemirror-compressed.js', type: 'text/javascript'},
	{uri: '/editor/codemirror.css', path: 'codemirror.css', type: 'text/css'},
	{uri: '/editor/lesser-dark.css', path: 'lesser-dark.css', type: 'text/css'},
	{uri: '/editor/script.js', path: 'script.js', type: 'text/javascript'},
];

var packageFs;
imports.filesystem.getPackage().then(function(fs) { packageFs = fs; });

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

function copyFile(oldPackage, newPackage, fileName) {
	return imports.system.getPackageFile(fileName, oldPackage).then(function(result) {
		return imports.system.putPackageFile(fileName, result, newPackage);
	});
}

function handler(request, response) {
	imports.auth.query(request.headers).then(function(auth) {
		if (auth) {
			sessionHandler(request, response, auth);
		} else {
			response.writeHead(303, {"Location": "/login?return=" + request.uri, "Connection": "close"});
			response.end();
		}
	});
}

function validateCanWrite(auth, packageName) {
	return new Promise(function(resolve, reject) {
		if (auth.permissions.administrator) {
			resolve(true);
		} else {
			imports.system.getPackageFile("package.json", packageName).then(function(packageFile) {
				var data = JSON.parse(packageFile);
				if (data.trusted) {
					throw new Error("Permission denied.");
				}
				resolve(true);
			}).catch(function(e) {
				reject(e);
			});
		}
	});
}

function sessionHandler(request, response, auth) {
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
		packageFs.readFile(match.path).then(function(contents) {
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
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(error);
				});;
			} else if (action == "put") {
				var form = decodeForm(request.body);
				validateCanWrite(auth, packageName).then(function() {
					if (form.fileName == "package.json"
						&& JSON.parse(JSON.parse(form.contents)).trusted
						&& !auth.permissions.administrator) {
						throw new Error("Permission denied.");
					}
					return imports.system.putPackageFile(form.fileName, JSON.parse(form.contents), packageName);
				}).then(function(result) {
					return imports.system.restartTask(packageName);
				}).then(function() {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end("updated");
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});;
			} else if (action == "new") {
				var form = decodeForm(request.query);
				imports.system.createPackage(packageName).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "unlink") {
				var form = decodeForm(request.query);
				validateCanWrite(auth, packageName).then(function() {
					return imports.system.unlinkPackageFile(form.fileName, packageName);
				}).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "rename") {
				var form = decodeForm(request.query);
				validateCanWrite(auth, packageName).then(function() {
					if (form.newName == "package.json" || form.oldName == "package.json") {
						throw new Error("Renaming to/from package.json is not allowed.");
					}
					return imports.system.renamePackageFile(form.oldName, form.newName, packageName);
				}).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "clone") {
				var form = decodeForm(request.query);
				var oldName = packageName;
				var newName = form.newName;
				validateCanWrite(auth, packageName).then(function() {
					return imports.system.createPackage(newName);
				}).then(function() {
					return imports.system.listPackageFiles(oldName);
				}).then(function(oldPackageContents) {
					promises = [];
					for (var i in oldPackageContents) {
						promises.push(copyFile(oldName, newName, oldPackageContents[i]));
					}
					return Promise.all(promises);
				}).then(function(data) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end("cloned");
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
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
