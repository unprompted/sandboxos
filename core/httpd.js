var gHandlers = [];

function logError(error) {
	print("ERROR " + error);
}

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

function Response(request, client) {
	var kStatusText = {
		200: 'OK',
		303: 'See other',
		404: 'File not found',
		500: 'Internal server error',
	};
	var _started = false;
	var _finished = false;
	var _keepAlive = false;
	var _chunked = false;
	return {
		writeHead: function(status) {
			if (_started) {
				throw new Error("Response.writeHead called multiple times.");
			}
			var reason;
			var headers;
			if (arguments.length == 3) {
				reason = arguments[1];
				headers = arguments[2];
			} else {
				reason = kStatusText[status];
				headers = arguments[1];
			}
			var lowerHeaders = {};
			var requestVersion = request.version.split("/")[1].split(".");
			var responseVersion = (requestVersion[0] >= 1 && requestVersion[0] >= 1) ? "1.1" : "1.0";
			var headerString = "HTTP/" + responseVersion + " " + status + " " + reason + "\r\n";
			for (var i in headers) {
				headerString += i + ": " + headers[i] + "\r\n";
				lowerHeaders[i.toLowerCase()] = headers[i];
			}
			if ("connection" in lowerHeaders) {
				_keepAlive = lowerHeaders["connection"].toLowerCase() == "keep-alive";
			} else {
				_keepAlive = ((request.version == "HTTP/1.0" && ("connection" in lowerHeaders && lowerHeaders["connection"].toLowerCase() == "keep-alive")) ||
					(request.version == "HTTP/1.1" && (!("connection" in lowerHeaders) || lowerHeaders["connection"].toLowerCase() != "close")));
				headerString += "Connection: " + (_keepAlive ? "keep-alive" : "close") + "\r\n";
			}
			_chunked = _keepAlive && !("content-length" in lowerHeaders);
			if (_chunked) {
				headerString += "Transfer-Encoding: chunked\r\n";
			}
			headerString += "\r\n";
			_started = true;
			client.write(headerString);
		},
		end: function(data) {
			if (_finished) {
				throw new Error("Response.end called multiple times.");
			}
			if (data) {
				if (_chunked) {
					client.write(data.length.toString(16) + "\r\n" + data + "\r\n" + "0\r\n\r\n");
				} else {
					client.write(data);
				}
			} else if (_chunked) {
				client.write("0\r\n\r\n");
			}
			_finished = true;
			if (!_keepAlive) {
				client.shutdown();
			}
		},
		reportError: function(error) {
			if (!_started) {
				client.write("HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n");
			}
			if (!_finished) {
				client.write("500 Internal Server Error\r\n\r\n" + error.stackTrace);
				client.shutdown();
			}
			logError(client.peerName + " - - [" + new Date() + "] " + error);
		},
		isConnected: function() { return client.isConnected; },
	};
}

function handleRequest(request, response) {
	var handler = findHandler(request);

	print(request.client.peerName + " - - [" + new Date() + "] " + request.method + " " + request.uri + " " + request.version + " \"" + request.headers["user-agent"] + "\"");

	if (handler) {
		try {
			handler.invoke(request, response);
		} catch (error) {
			response.reportError(error);
		}
	} else {
		response.writeHead(200, {"Content-Type": "text/plain; charset=utf-8"});
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

	function reset() {
		inputBuffer = "";
		request = undefined;
		headers = {};
		lineByLine = true;
		bodyToRead = -1;
		body = undefined;
	}

	function finish() {
		try {
			var requestObject = new Request(request[0], request[1], request[2], headers, body, client);
			var response = new Response(requestObject, client);
			handleRequest(requestObject, response)
			if (client.isConnected) {
				reset();
			}
		} catch (error) {
			response.reportError(error);
		}
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
				if (headers["content-length"] != undefined) {
					bodyToRead = parseInt(headers["content-length"]);
					lineByLine = false;
					body = "";
					return true;
				} else {
					finish();
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

	client.onError(function(error) {
		logError(client.peerName + " - - [" + new Date() + "] " + error);
	});

	client.read(function(data) {
		if (data) {
			inputBuffer += data;
			var more = true;
			while (more) {
				if (lineByLine) {
					more = false;
					var end = inputBuffer.indexOf('\n');
					var realEnd = end;
					if (end > 0 && inputBuffer[end - 1] == '\r') {
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
var kHost = "0.0.0.0"
var kHttpPort = 12347;
var kHttpsPort = 12348;

var socket = new Socket();
socket.bind(kHost, kHttpPort).then(function() {
	var listenResult = socket.listen(kBacklog, function() {
		socket.accept().then(handleConnection).catch(function(error) {
			logError("[" + new Date() + "] " + error);
		});
	});
}).catch(function(error) {
	logError("[" + new Date() + "] " + error);
});

var privateKey = File.readFile("data/httpd/privatekey.pem");
var certificate = File.readFile("data/httpd/certificate.pem");

if (privateKey && certificate) {
	var tls = new TlsContext();
	tls.setPrivateKey(privateKey);
	tls.setCertificate(certificate);

	var secureSocket = new Socket();
	secureSocket.bind(kHost, kHttpsPort).then(function() {
		secureSocket.listen(kBacklog, function() {
			secureSocket.accept().then(function(client) {
				handleConnection(client);
				client.startTls(tls).catch(function(error) {
					logError("[" + new Date() + "] [" + client.peerName + "] " + error);
				});
			}).catch(function(error) {
				logError("[" + new Date() + "] " + error);
			});
		});
	});
}

exports.all = all;
exports.get = get;
