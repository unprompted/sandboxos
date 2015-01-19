var gTerminals = {};
var gFileSystem;

var kStaticFiles = [
	{uri: '/terminal', path: 'index.html', type: 'text/html'},
	{uri: '/terminal/style.css', path: 'style.css', type: 'text/css'},
	{uri: '/terminal/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];
var kBacklog = 64;

imports.filesystem.getPackage().then(function(fs) { gFileSystem = fs; });

function Terminal() {
	this._waiting = [];
	this._index = 0;
	this._firstLine = 0;
	this._lines = [];
	this._lastRead = null;
	this._lastWrite = null;
	return this;
}

Terminal.prototype.dispatch = function(data) {
	for (var i in this._waiting) {
		this._waiting[i](data);
	}
	this._waiting.length = 0;
}

Terminal.prototype.print = function(line) {
	this._lines.push(line);
	this._index++;
	if (this._lines.length >= kBacklog * 2) {
		this._firstLine = this._index - kBacklog;
		this._lines = this._lines.slice(this._lines.length - kBacklog);
	}
	this.dispatch({index: this._index - 1, lines: [line]});
	this._lastWrite = new Date();
}

Terminal.prototype.clear = function() {
	this._lines.length = 0;
	this._firstLine = this._index;
	this.print({action: "clear"});
}

Terminal.prototype.getOutput = function(haveIndex) {
	var terminal = this;
	terminal._lastRead = new Date();
	return new Promise(function(resolve) {
		if (haveIndex < terminal._index - 1) {
			resolve({index: terminal._index - 1, lines: terminal._lines.slice(haveIndex + 1 - terminal._firstLine)});
		} else {
			terminal._waiting.push(resolve);
		}
	});
}

Terminal.who = function() {
	var result = {};
	for (var i in gTerminals) {
		result[i] = {lastRead: gTerminals[i]._lastRead.toString(), lastWrite: gTerminals[i]._lastWrite.toString()};
	}
	return result;
}

Terminal.prototype.send = function(user, message) {
	if (gTerminals[user]) {
		gTerminals[user].print("Message from " + this.owner + ": " + message);
	}
}

Terminal.prototype.exportInterface = function() {
	return {
		print: this.print.bind(this),
		clear: this.clear.bind(this),
		who: Terminal.who,
		send: this.send.bind(this),
	};
}

function getTerminal(session) {
	if (!gTerminals[session.name]) {
		gTerminals[session.name] = new Terminal();
		gTerminals[session.name].owner = session.name;
	}
	return gTerminals[session.name];
}

function sessionHandler(request, response, auth) {
	var found = false;
	for (var i in kStaticFiles) {
		if (kStaticFiles[i].uri == request.uri) {
			found = true;
			var file = kStaticFiles[i];
			gFileSystem.readFile(file.path).then(function(data) {
				response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
				response.end(data);
			}).catch(function(e) {
				response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close"});
				response.end("500 Internal Server Error\n" + e.toString());
			});
			break;
		}
	}
	if (!found) {
		var terminal = getTerminal(auth.session);
		if (request.uri == "/terminal/send") {
			var command = request.body;
			terminal.print("> " + command);
			imports.auth.getCredentials(request.headers, "shell").then(function(credentials) {
				return imports.shell.evaluate(terminal.exportInterface(), command, credentials);
			}).catch(function(error) {
				terminal.print(error);
			});
			response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
			response.end("OK");
		} else if (request.uri == "/terminal/receive") {
			terminal.getOutput(parseInt(request.body)).then(function(output) {
				response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
				response.end(JSON.stringify(output));
			});
		}
	}
}

function handler(request, response) {
	imports.auth.query(request.headers).then(function(auth) {
		if (auth) {
			sessionHandler(request, response, auth);
		} else {
			response.writeHead(303, {"Location": "/login?return=/terminal", "Connection": "close"});
			response.end();
		}
	});
}

imports.httpd.all("/terminal", handler);
