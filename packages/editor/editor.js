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
];

function onMessage(from, message) {
	print("editor received: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	if (message.request.uri == "/editor/get") {
		var form = decodeForm(message.request.query);
		parent.invoke({to: "system", action: "get", taskName: "handler", fileName: form.fileName}).then(function(result) {
			parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + result, messageId: message.messageId});
		});
	} else if (message.request.uri == "/editor/list") {
		parent.invoke({to: "system", action: "list", taskName: "handler"}).then(function(result) {
			parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + JSON.stringify(result), messageId: message.messageId});
		});
	} else if (message.request.uri == "/editor/put") {
		var form = decodeForm(message.request.body);
		parent.invoke({to: "system", action: "stopTask", taskName: "handler"}).then(function(result) {
			parent.invoke({to: "system", action: "put", taskName: "handler", fileName: form.fileName, contents: JSON.parse(form.contents)}).then(function(result) {
				parent.invoke({to: "system", action: "startTask", taskName: "handler"}).then(function(result) {
					parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + "updated", messageId: message.messageId});
				});
			});
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
				parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: " + match.type + "\nConnection: close\n\n" + contents, messageId: message.messageId});
			});
		} else {
			parent.invoke({to: "httpd", response: "HTTP/1.0 404 Not found\nContent-Type: text/plain\nConnection: close\n\n404 Not found", messageId: message.messageId});
		}
	}
}
