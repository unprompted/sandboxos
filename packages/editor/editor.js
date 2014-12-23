var kStaticFiles = [
	{uri: '/editor', path: 'index.html', type: 'text/html'},
	{uri: '/editor/style.css', path: 'style.css', type: 'text/css'},
	{uri: '/editor/script.js', path: 'script.js', type: 'text/javascript'},
];

var packageFs;
imports.filesystem.getPackage().then(function(fs) { packageFs = fs; });

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

function getWorkspace(auth, packageName, create) {
	var promise = imports.filesystem.getPackageData();
	if (create) {
		promise = promise.then(function(fs) {
			fs.ensureDirectoryTreeExists(auth.session.name + "/" + packageName).then(function() { return fs; });
		});
	}
	return promise.then(function(fs) {
		return fs.chroot(auth.session.name + "/" + packageName);
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
				getWorkspace(auth, packageName).then(function(fs) {
					return fs.readFile(form.fileName);
				}).then(function(contents) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(contents);
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "list") {
				getWorkspace(auth, packageName).then(function(fs) {
					return fs.listDirectory(".");
				}).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(error);
				});
			} else if (action == "put") {
				var form = decodeForm(request.body);
				getWorkspace(auth, packageName).then(function(fs) {
					return fs.writeFile(form.fileName, JSON.parse(form.contents));
				}).then(function() {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end("written");
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "new") {
				getWorkspace(auth, packageName, true).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "unlink") {
				var form = decodeForm(request.query);
				getWorkspace(auth, packageName).then(function(fs) {
					return fs.unlinkFile(form.fileName);
				}).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "rename") {
				var form = decodeForm(request.query);
				getWorkspace(auth, packageName).then(function(fs) {
					return fs.renameFile(form.oldName, form.newName);
				}).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "copyToWorkspace") {
				Promise.all([
					imports.filesystem.getPackage(packageName),
					imports.filesystem.getPackageData().then(function(fs) {
						return fs.ensureDirectoryTreeExists(auth.session.name + "/" + packageName).then(function() {
							return fs.chroot(auth.session.name + "/" + packageName);
						});
					}),
				]).then(function(filesystems) {
					return imports.filesystem.copy(filesystems[0], filesystems[1]);
				}).then(function(v) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end("copied");
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "install") {
				getWorkspace(auth, packageName).then(function(fs) {
					return imports.auth.getCredentials(request.headers, 'packager').then(function(credentials) {
						return imports.packager.install(fs, credentials);
					});
				}).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
				}).catch(function(error) {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(error.toString()));
				});
			} else if (action == "restartTask") {
				imports.auth.getCredentials(request.headers, 'packager').then(function(credentials) {
					return imports.packager.restartTask(packageName, credentials);
				}).then(function(result) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					response.end(JSON.stringify(result));
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
