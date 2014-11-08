function Request(method, uri, version, headers, client) {
	this.method = method;
	this.uri = uri;
	this.version = version;
	this.headers = headers;
	this.client = client;
	return this;
}

function handleRequest(request) {
	request.client.write("HTTP/1.0 OK\n");
	request.client.write("Content-Type: text/plain\n");
	request.client.write("Connection: close\n\n");
	request.client.write("Hello, world!");
	request.client.close();
}

function handleConnection(client) {
	print("New connection.");
	var inputBuffer = "";
	var request;
	var headers = {};

	function handleLine(line) {
		if (!request) {
			request = line.split(' ');
			return true;
		} else if (line) {
			var colon = line.indexOf(':');
			var key = line.slice(0, colon).trim();
			var value = line.slice(colon + 1).trim();
			headers[key] = value;
			return true;
		} else {
			handleRequest(new Request(request[0], request[1], request[2], headers, client));
			return false;
		}
	}

	client.read(function(data) {
		if (!data) {
			client.close();
			print("Connection closed.");
		} else {
			inputBuffer += data;
			var more = true;
			while (more) {
				more = false;
				var end = inputBuffer.indexOf('\n');
				var realEnd = end;
				while  (end > 0 && inputBuffer[end - 1] == '\r') {
					--end;
				}
				if (end != -1) {
					var line = inputBuffer.slice(0, end);
					inputBuffer = inputBuffer.slice(realEnd + 1);
					more = handleLine(line);
				}
			}
		}
	});
}

function runServer(ip, port) {
	var socket = new Socket();
	socket.bind(ip, port);
	socket.listen(8, function() {
		handleConnection(socket.accept());
	});
}

runServer("0.0.0.0", 12345);
