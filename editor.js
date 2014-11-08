function escapeHtml(value) {
	var kMap = {
		"&": "&amp;",
		"<": "&lt;",
		">": "&gt;",
	};
	return value.replace(/&<>/g, function(v) { return kMap[v]; });
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

function onMessage(from, message) {
	print("editor received: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	if (message.request.uri == "/editor") {
		var contents = "";
		contents += "<html>\n";
		contents += "<head>\n";
		contents += "\t<title>Editor</title>\n";
		contents += "\t<script src=\"http://code.jquery.com/jquery-1.11.0.min.js\"></script>\n";
		contents += "\t<script language=\"javascript\">\n";
		contents += "\t\tfunction submit() {\n";
		contents += "\t\t\t$.ajax({\n";
		contents += "\t\t\t\ttype: \"POST\",\n";
		contents += "\t\t\t\turl: \"/editor/update\",\n";
		contents += "\t\t\t\tdata: {script: JSON.stringify($(\"#edit\").val())},\n";
		contents += "\t\t\t\tdataType: \"JSON\",\n";
		contents += "\t\t\t}).done(function(data) {\n";
		contents += "\t\t\t\t$(\"#iframe\")[0].src = \"/handler\";\n";
		contents += "\t\t\t\tconsole.debug(data);\n";
		contents += "\t\t\t});\n";
		contents += "\t\t}\n";
		contents += "\t</script>\n";
		contents += "</head>\n";
		contents += "<body>\n";
		contents += "<h1>Editor</h1>\n";
		contents += "<textarea id=\"edit\" rows=\"20\" cols=\"80\">";
		contents += escapeHtml(readFile("handler.js"));
		contents += "</textarea>\n";
		contents += "<input type=\"button\" value=\"Update\" onclick=\"submit()\"></input>\n";
		contents += "<iframe id=\"iframe\"></iframe>\n";
		contents += "</body>\n";
		contents += "</html>\n";
		parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/html\nConnection: close\n\n" + contents, messageId: message.messageId});
	} else if (message.request.uri == "/editor/update") {
		var form = decodeForm(message.request.body);
		parent.invoke({to: "system", action: "update", taskName: "handler", script: JSON.parse(form.script)}).then(function(result) {
			parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + result, messageId: message.messageId});
		});
	} else {
		var contents = "huh?";
		parent.invoke({to: "httpd", response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + contents, messageId: message.messageId});
	}
	return true;
}
