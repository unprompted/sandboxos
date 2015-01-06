var gHandlers = [];

function addHandler(handler) {
	var added = false;
	for (var i in gHandlers) {
		if (gHandlers[i].method == handler.method && gHandlers[i].path == handler.path) {
			gHandlers[i] = handler;
			added = true;
			break;
		}
	}
	if (!added) {
		gHandlers.push(handler);
		added = true;
	}
}

function get(prefix, handler) {
	addHandler({
		owner: this,
		method: "GET",
		path: prefix,
		invoke: handler,
	});
}

function all(prefix, handler) {
	addHandler({
		owner: this,
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
	};
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
				client.write(data);
			}
			client.shutdown();
		},
		isConnected: function() { return client.isConnected; },
	};
}

function handleRequest(request, response) {
	var  handler = findHandler(request);

	if (request.uri != "/log/get") {
		imports.log.append(request.client.peerName + " - - [" + new Date() + "] " + request.method + " " + request.uri + " " + request.version + " \"" + request.headers["user-agent"] + "\"");
	}

	if (handler) {
		handler.invoke(request, response);
	} else {
		response.writeHead(200, {"Content-Type": "text/plain; encoding=utf-8", "Connection": "close"});
		response.end("No handler found for request: " + request.uri);
	}
}

function handleConnection(client) {
	var inputBuffer = "";
	var request;
	var headers = {};
	var lineByLine = true;
	var bodyToRead = -1;
	var body;

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
		if (data) {
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

function runServer(socket) {
	var listenResult = socket.listen(kBacklog, function() {
		handleConnection(socket.accept());
	});
	if (listenResult !== 0) {
		throw new Error("listen failed: " + listenResult);
	}
}

var socket = new Socket();
socket.bind("0.0.0.0", 12345).then(function() {
	runServer(socket);
});

/*
function runSecureServer(socket) {
	var privateKey = File.readFile("privatekey.pem");
	var certificate = File.readFile("certificate.pem")
	var listenResult = socket.listen(kBacklog, function() {
		var client = socket.accept();
		handleConnection(client);
		client.startTls(privateKey, certificate).catch(function(e) {
			print("tls failed: " + e);
		});
	});
	if (listenResult !== 0) {
		throw new Error("listen failed: " + listenResult);
	}
}

var secureSocket = new Socket();
secureSocket.bind("0.0.0.0", 12346).then(function() {
	runSecureServer(secureSocket);
});
*/

exports = {
	all: all,
	get: get,
};
