var gHandlers = [];

function updatePackage(packageName) {
	var newHandlers = [];
	for (var i in gHandlers) {
		if (gHandlers[i].taskName != packageName) {
			newHandlers.push(gHandlers[i]);
		}
	}
	gHandlers = newHandlers;
	parent.invoke({to: "system", action: "getManifest", taskName: packageName}).then(function(manifest) {
		if (manifest && manifest.httpd && manifest.httpd.root) {
			gHandlers.push({
				path: manifest.httpd.root,
				taskName: packageName,
			});
			print(gHandlers);
		}
	});
}

parent.invoke({to: "system", action: "getPackageList"}).then(function(packages) {
	print(packages);
	for (var i in packages) {
		updatePackage(packages[i]);
	}
});

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

function onMessage(from, message) {
	if (message.action == "taskStarted") {
		updatePackage(message.taskName);
	}
}

function findHandler(uri) {
	var matchedHandler = null;
	for (var name in gHandlers) {
		var handler = gHandlers[name];
		if (uri == handler.path ||
			uri.slice(0, handler.path.length + 1) == handler.path + '/') {
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
	};
}

function handleRequest(request, response) {
	var  handler = findHandler(request.uri);
	print(request);

	if (request.uri != "/log/get") {
		parent.invoke({
			to: "log",
			request: request.client.peerName + " - - [" + new Date() + "] " + request.method + " " + request.uri + " " + request.version,
		});
	}

	if (handler) {
		parent.invoke({to: handler.taskName, action: "handleRequest", request: request, response: response});
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
				headers[key] = value;
				return true;
			} else {
				if (headers["Content-Length"] > 0) {
					bodyToRead = headers["Content-Length"];
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
