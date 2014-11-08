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
		} else if (c == '%') {
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
	// HACK
	encoded = encoded.trim();
	var items = encoded.split('&');
	for (var i = 0; i < items.length; i++) {
		var item = items[i];
		var equals = item.indexOf('=');
		var key = decode(item.slice(0, equals));
		var value = decode(item.slice(equals + 1));
		result[key] = value;
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

function onMessage(from, message) {
	print("editor received: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	if (message.request.uri == "/editor/get") {
		var form = decodeForm(message.request.query);
		parent.invoke({to: "system", action: "get", taskName: form.taskName, fileName: form.fileName}).then(function(result) {
			parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + result, messageId: message.messageId});
		});
	} else if (message.request.uri == "/editor/getPackageList") {
		parent.invoke({to: "system", action: "getPackageList"}).then(function(result) {
			parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + JSON.stringify(result), messageId: message.messageId});
		});
	} else if (message.request.uri == "/editor/list") {
		var form = decodeForm(message.request.query);
		parent.invoke({to: "system", action: "list", taskName: form.taskName}).then(function(result) {
			parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + JSON.stringify(result), messageId: message.messageId});
		});
	} else if (message.request.uri == "/editor/put") {
		var form = decodeForm(message.request.body);
		print(form.fileName);
		print(form.fileName);
		print(form.fileName);
		parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + "updated", messageId: message.messageId});
		parent.invoke({to: "system", action: "put", taskName: form.taskName, fileName: form.fileName, contents: JSON.parse(form.contents)}).then(function(result) {
			parent.invoke({to: "system", action: "restartTask", taskName: form.taskName});
		});
	} else {
		var match;
		for (var i in kStaticFiles) {
			var file = kStaticFiles[i];
			if (message.request.uri == file.uri) {
				match = file;
			}
		}
		if (match) {
			parent.invoke({to: "system", action: "get", taskName: "editor", fileName: match.path}).then(function(contents) {
				parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: " + match.type + "\nConnection: close\nContent-Length: " + contents.length + "\n\n" + contents, messageId: message.messageId});
			});
		} else {
			parent.invoke({to: "httpd", response: "HTTP/1.0 404 Not found\nContent-Type: text/plain\nConnection: close\n\n404 Not found", messageId: message.messageId});
		}
	}
}
