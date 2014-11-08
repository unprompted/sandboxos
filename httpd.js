var gHandlers = [
	{
		path: '/test',
		taskName: 'handler',
	},
	{
		path: '/editor',
		taskName: 'editor',
	},
];

function Request(method, uri, version, headers, client) {
	this.method = method;
	this.uri = uri;
	this.version = version;
	this.headers = headers;
	this.client = client;
	return this;
}

var gMessageId = 0;
var gMessages = {};

function onMessage(from, message) {
	print("httpd onMessage: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	var promise = gMessages[message.messageId];
	print(promise);
	if (promise) {
		print("resolving promise?");
		promise[0](message);
		delete gMessages[message.messageId];
	}
	return true;
}

function invoke(message) {
	return new Promise(function(resolve, reject) {
		var id = ++gMessageId;
		gMessages[id] = [resolve, reject];
		message.messageId = id;
		parent.invoke(message);
	});
}

function handleRequest(request) {
	var matchedHandler = null;

	for (var name in gHandlers) {
		var handler = gHandlers[name];
		if (handler.path == request.uri.slice(0, handler.path.length)) {
			matchedHandler = handler;
			break;
		}
	}

	if (matchedHandler) {
		print(matchedHandler.taskName);
		invoke({to: matchedHandler.taskName, action: "handleRequest", request: request}).then(function(data) {
			print("INVOKE -> " + JSON.stringify(data));
			request.client.write(data.response);
			request.client.close();
		});
	} else {
		request.client.write("HTTP/1.0 OK\n");
		request.client.write("Content-Type: text/plain; encoding=utf-8\n");
		request.client.write("Connection: close\n\n");
		request.client.write("No handler for request: " + request.uri);
		request.client.close();
	}
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

var kBacklog = 8;

function runServer(ip, port) {
	var socket = new Socket();
	socket.bind(ip, port);
	socket.listen(kBacklog, function() {
		handleConnection(socket.accept());
	});
}

runServer("0.0.0.0", 12345);
