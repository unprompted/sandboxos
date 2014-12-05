var kStaticFiles = [
	{uri: '/wiki/frontend.js', path: 'frontend.js', type: 'text/javascript'},
	{uri: '/wiki/style.css', path: 'style.css', type: 'text/css'},
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

function wikiToHtmlLine(line) {
	return line
		.replace(/[<>&]/g, function(c) {	return {'<': '&lt;', '>': '&gt;', '&': '&amp;' }[c]; })
		.replace(/'''(.*?)'''/, "<b>$1</b>")
		.replace(/''(.*?)''/, "<em>$1</em>")
		.replace(/\[(\S+) ([^\]]+)\]/g, '<a href="$1">$2</a>')
		.replace(/~~([^~]+)~~/g, "<strike>$1</strike>");
}

function wikiToHtml(text) {
	var lines = text.split(/\r?\n/);
	var result = [];
	result.push("<p>");
	var justStartedNewParagraph = true;
	var inList = false;
	var inBlock = false;
	var shebang = false;
	var inRawHtml = false;
	for (var i in lines) {
		var line = lines[i];
		if (inBlock && !shebang) {
			shebang = true;
			inRawHtml = line == "#!html";
			if (inRawHtml) {
				result.push("<p>\n");
			} else {
				result.push("<blockquote>\n");
			}
		} else if (inBlock && inRawHtml && line != "}}}") {
			result.push(line);
		} else if (line.trim().length == 0) {
			if (!justStartedNewParagraph) {
				result.push("</p>\n");
				result.push("<p>");
				justStartedNewParagraph = true;
			}
		} else {
			justStartedNewParagraph = false;
			if (line.substring(0, 3) == " * ") {
				if (!inList) {
					result.push("\n<ul>\n");
					inList = true;
				}
				result.push("\t<li>" + wikiToHtmlLine(line.substring(3)) + "</li>\n");
			} else {
				if (inList) {
					result.push("\n</ul>\n");
					inList = false;
				}
				var match;
				if (match = new RegExp(/^(=+) (.*) \1$/).exec(line)) {
					var level = (1 + match[1].length).toString();
					result.push("<h" + level + ">" + match[2] + "</h" + level + ">\n");
				} else if (line == "----") {
					result.push("<hr>\n");
				} else if (line == "{{{") {
					inBlock = true;
					shebang = false;
				} else if (inBlock && line == "}}}") {
					inBlock = false;
					if (inRawHtml) {
						result.push("</p>\n");
						inRawHtml = false;
					} else {
						result.push("</blockquote>\n");
					}
				} else {
					result.push(wikiToHtmlLine(line));
				}
			}
		}
	}
	return result.join("");
}

function render(response, fileName, isEdit) {
	Promise.all([
		imports.system.getPackageFile(isEdit ? "edit.html" : "index.html"),
		imports.system.getData(fileName),
	]).then(function(data) {
		var html = data[0].replace(/\$\(CONTENTS\)/g, isEdit ? data[1] : wikiToHtml(data[1])).replace(/\$\(PAGE\)/g, fileName);
		response.writeHead(200, {"Content-Type": "text/html", "Connection": "close"});
		response.end(html);
	});
}

function handler(request, response) {
	var found = false;
	for (var i in kStaticFiles) {
		if (kStaticFiles[i].uri == request.uri) {
			found = true;
			var file = kStaticFiles[i];
			imports.system.getPackageFile(file.path).then(function(data) {
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
			imports.system.putData(fileName, form.contents).then(function() {
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
