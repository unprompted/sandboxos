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
	return new Promise(function(resolve, reject) {
		parent.invoke({to: "system", action: "get", taskName: oldPackage, fileName: fileName}).then(function(result) {
			parent.invoke({to: "system", action: "put", taskName: newPackage, fileName: fileName, contents: result}).then(resolve).catch(reject);
		}).catch(reject);
	});
}

function onMessage(from, message) {
	var handled = false;
	print("editor received: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	if (message.request) {
		var match;
		for (var i in kStaticFiles) {
			var file = kStaticFiles[i];
			if (message.request.uri == file.uri) {
				match = file;
			}
		}
		if (new RegExp("^/editor/[^/]+/$").exec(message.request.uri)) {
			match = {path: "package.html", type: "text/html"};
		}
		if (match) {
			print("RESPONDING TO " + JSON.stringify(match));
			parent.invoke({to: "system", action: "get", taskName: "editor", fileName: match.path}).then(function(contents) {
				message.response.writeHead(200, {"Content-Type": match.type, "Connection": "close", "Content-Length": contents.length});
				message.response.end(contents);
			});
			handled = true;
		}
		if (!handled) {
			var regex = new RegExp("^/editor/([^/]+)/(.*)$");
			match = regex.exec(message.request.uri);
			if (match) {
				var package = match[1];
				var action = match[2];
				handled = true;
				if (action == "get") {
					var form = decodeForm(message.request.query);
					parent.invoke({to: "system", action: "get", taskName: package, fileName: form.fileName}).then(function(result) {
						message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(result);
					});
				} else if (action == "getPackageList") {
					parent.invoke({to: "system", action: "getPackageList"}).then(function(result) {
						message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(JSON.stringify(result));
					});
				} else if (action == "list") {
					var form = decodeForm(message.request.query);
					parent.invoke({to: "system", action: "list", taskName: package}).then(function(result) {
						message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(JSON.stringify(result));
					});
				} else if (action == "put") {
					var form = decodeForm(message.request.body);
					message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
					message.response.end("updated");

					parent.invoke({to: "system", action: "put", taskName: package, fileName: form.fileName, contents: JSON.parse(form.contents)}).then(function(result) {
						parent.invoke({to: "system", action: "restartTask", taskName: package});
					});
				} else if (action == "new") {
					var form = decodeForm(message.request.query);
					parent.invoke({to: "system", action: "newPackage", taskName: package}).then(function(result) {
						message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(JSON.stringify(result));
					}).catch(function(error) {
						message.response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(error);
					});
				} else if (action == "unlink") {
					var form = decodeForm(message.request.query);
					parent.invoke({to: "system", action: "unlink", taskName: package, fileName: form.fileName}).then(function(result) {
						message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(JSON.stringify(result));
					}).catch(function(error) {
						message.response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(error);
					});
				} else if (action == "rename") {
					var form = decodeForm(message.request.query);
					parent.invoke({to: "system", action: "rename", taskName: package, fileName: form.oldName, newName: form.newName}).then(function(result) {
						message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(JSON.stringify(result));
					}).catch(function(error) {
						message.response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(error);
					});
				} else if (action == "clone") {
					var form = decodeForm(message.request.query);
					var oldName = package;
					var newName = form.newName;
					parent.invoke({to: "system", action: "newPackage", taskName: newName}).then(function(result) {
						parent.invoke({to: "system", action: "list", taskName: oldName}).then(function(oldPackageContents) {
							promises = [];
							for (var i in oldPackageContents) {
								promises.push(copyFile(oldName, newName, oldPackageContents[i]));
							}
							Promise.all(promises).then(function(data) {
								message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
								message.response.end("cloned");
							});
						});
					}).catch(function(error) {
						message.response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
						message.response.end(error);
					});
				} else {
					handled = false;
				}
			}
		}
		if (!handled) {
			print("NOT HANDLED?");
			message.response.writeHead(404, {"Content-Type": "text/plain", "Connection": "close"});
			message.response.end("404 Not found");
		}
	}
}
