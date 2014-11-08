var kStaticFiles = [
	{uri: '/wiki/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

function startsWith(string, start) {
	return string.substring(0, start.length) == start;
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

function render(message, fileName, isEdit) {			
	parent.invoke({
		to: "system",
			action: "getData",
			fileName: fileName,
	}).then(function(data) {
		parent.invoke({
			to: "system",
				action: "get",
				fileName: isEdit ? "edit.html" : "index.html",
		}).then(function(html) {
			html = html.replace(/\$\(CONTENTS\)/g, data).replace(/\$\(PAGE\)/g, fileName);
			parent.invoke({
				to: "httpd",
					response: "HTTP/1.0 200 OK\nContent-Type: text/html\nConnection: close\n\n" + html,
					messageId: message.messageId,
			});
		});
	});
}

function onMessage(from, message) {
	print(message);
	if (message.request) {
		var found = false;
		for (var i in kStaticFiles) {
			if (kStaticFiles[i].uri == message.request.uri) {
				found = true;
				var file = kStaticFiles[i];
				parent.invoke({
					to: "system",
					action: "get",
					fileName: file.path,
				}).then(function(data) {
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 200 OK\nContent-Type: " + file.type + "\nConnection: close\n\n" + data,
						messageId: message.messageId,
					});
				});
				break;
			}
		}
		
		if (!found) {
			var isEdit = false;
			var fileName;
			if (startsWith(message.request.uri, "/wiki/edit/")) {
				isEdit = true;
				fileName = message.request.uri.substring("/wiki/edit/".length);
			} else {
				fileName = message.request.uri.substring("/wiki/".length);
			}
			if (!fileName) {
				fileName = "index";
			}
			
			if (message.request.method == "POST") {
				var form = decodeForm(message.request.body);
				parent.invoke({
				to: "system",
					action: "putData",
					fileName: fileName,
					contents: form.contents,
				}).then(function() {
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 303 See other\nLocation: /wiki/" + fileName + "\nContent-Type: text/plain\nConnection: Close\n\n",
						messageId: message.messageId,
					});
				});
			} else {
				render(message, fileName, isEdit);
			}
		}
	}
	return true;
}
