var gHandlers = [];

function get(prefix, handler) {
	gHandlers.push({
		method: "GET",
		path: prefix,
		invoke: handler,
	});
}

function all(prefix, handler) {
	gHandlers.push({
		path: prefix,
		invoke: handler,
	});
}

function Request(method, uri, version, headers, body, client) {
	this.method = method;
	var index = uri.indexOf("?");
	if (index != -1) {
		this.uri = uri.slice(0, index);
		this.query = uri.slice(index + 1);
	} else {
		this.uri = uri;
		this.query = undefined;
	}
	this.version = version;
	this.headers = headers;
	this.client = {peerName: client.peerName};
	this.body = body;
	return this;
}

function findHandler(request) {
	var matchedHandler = null;
	for (var name in gHandlers) {
		var handler = gHandlers[name];
		if ((!handler.method || handler.method == request.method)
			&& (request.uri == handler.path || request.uri.slice(0, handler.path.length + 1) == handler.path + '/')) {
			matchedHandler = handler;
			break;
		}
	}
	return matchedHandler;
}

function Response(client) {
	var kStatusText = {
		200: 'OK',
		303: 'See other',
		404: 'File not found',
		500: 'Internal server error',
	}
	return {
		writeHead: function(status) {
			var reason;
			var headers;
			if (arguments.length == 3) {
				reason = arguments[1];
				headers = arguments[2];
			} else {
				reason = kStatusText[status];
				headers = arguments[1];
			}
			client.write("HTTP/1.0 " + status + " " + reason + "\n");
			for (var i in headers) {
				client.write(i + ": " + headers[i] + "\n");
			}
			client.write("\n");
		},
		/*write: function(data) {
			client.write(data);
		},*/
		end: function(data) {
			if (data) {
				client.write(data).then(function() { client.close(); });
			} else {
				client.close();
			}
		},
		isConnected: function() { return client.isConnected; },
	};
}

function handleRequest(request, response) {
	var  handler = findHandler(request);

	if (request.uri != "/log/get") {
		parent.invoke({
			to: "log",
			payload: request.client.peerName + " - - [" + new Date() + "] " + request.method + " " + request.uri + " " + request.version + " \"" + request.headers["user-agent"] + "\"",
		});
	}

	if (handler) {
		handler.invoke(request, response);
	} else {
		response.writeHead(200, {"Content-Type": "text/plain; encoding=utf-8", "Connection": "close"});
		response.end("No handler found for request: " + request.uri);
	}
}

function handleConnection(client) {
	print("New connection.");
	var inputBuffer = "";
	var request;
	var headers = {};
	var lineByLine = true;
	var bodyToRead = -1;
	var body = undefined;

	function finish() {
		handleRequest(new Request(request[0], request[1], request[2], headers, body, client), new Response(client));
	}

	function handleLine(line, length) {
		if (bodyToRead == -1) {
			if (!request) {
				request = line.split(' ');
				return true;
			} else if (line) {
				var colon = line.indexOf(':');
				var key = line.slice(0, colon).trim();
				var value = line.slice(colon + 1).trim();
				headers[key.toLowerCase()] = value;
				return true;
			} else {
				if (headers["content-length"] > 0) {
					bodyToRead = headers["content-length"];
					lineByLine = false;
					body = "";
					return true;
				} else {
					handleRequest(new Request(request[0], request[1], request[2], headers, body, client), new Response(client));
					return false;
				}
			}
		} else {
			body += line;
			bodyToRead -= length;
			if (bodyToRead <= 0) {
				finish();
			}
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
				if (lineByLine) {
					more = false;
					var end = inputBuffer.indexOf('\n');
					var realEnd = end;
					while  (end > 0 && inputBuffer[end - 1] == '\r') {
						--end;
					}
					if (end != -1) {
						var line = inputBuffer.slice(0, end);
						inputBuffer = inputBuffer.slice(realEnd + 1);
						more = handleLine(line, realEnd + 1);
					}
				} else {
					more = handleLine(inputBuffer, inputBuffer.length);
					inputBuffer = "";
				}
			}
		}
	});
}

var kBacklog = 8;

function runServer(ip, port) {
	var socket = new Socket();
	var bindResult = socket.bind(ip, port);
	if (bindResult != 0) {
		throw "bind failed: " + bindResult;
	}
	var listenResult = socket.listen(kBacklog, function() {
		handleConnection(socket.accept());
	});
	if (listenResult != 0) {
		throw "listen failed: " + listenResult;
	}
}

runServer("0.0.0.0", 12345);

exports = {
	all: all,
	get: get,
};
print(exports);
