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

function render(response, fileName, isEdit) {
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
			response.writeHead(200, {"Content-Type": "text/html", "Connection": "close"});
			response.end(html);
		});
	});
}

function handler(request, response) {
	var found = false;
	for (var i in kStaticFiles) {
		if (kStaticFiles[i].uri == request.uri) {
			found = true;
			var file = kStaticFiles[i];
			parent.invoke({
				to: "system",
				action: "get",
				fileName: file.path,
			}).then(function(data) {
				response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
				response.end(data);
			});
			break;
		}
	}

	if (!found) {
		var isEdit = false;
		var fileName;
		if (startsWith(request.uri, "/wiki/edit/")) {
			isEdit = true;
			fileName = request.uri.substring("/wiki/edit/".length);
		} else {
			fileName = request.uri.substring("/wiki/".length);
		}
		if (!fileName) {
			fileName = "index";
		}

		if (request.method == "POST") {
			var form = decodeForm(request.body);
			parent.invoke({
			to: "system",
				action: "putData",
				fileName: fileName,
				contents: form.contents,
			}).then(function() {
				response.writeHead(303, {"Content-Type": "text/plain", "Connection": "close", "Location": "/wiki/" + fileName});
				response.end("");
			});
		} else {
			render(response, fileName, isEdit);
		}
	}
	return true;
}

imports.httpd.all('/wiki', handler);
