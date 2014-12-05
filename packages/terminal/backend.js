var gTerminals = {};

var kStaticFiles = [
	{uri: '/terminal', path: 'index.html', type: 'text/html'},
	{uri: '/terminal/style.css', path: 'style.css', type: 'text/css'},
	{uri: '/terminal/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];
var kBacklog = 8;

function Terminal() {
	this._waiting = [];
	this._index = 0;
	this._firstLine = 0;
	this._lines = [];
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
	if (this._lines.length > kBacklog * 2) {
		this._lines = this._lines.slice(this._lines.length - kBacklog);
		this._firstLine = this._index - kBacklog;
	}
	this.dispatch({index: this._index, lines: [line]});
	this._index++;
}

Terminal.prototype.clear = function() {
	this._lines.length = 0;
	this._firstLine = this._index;
	this.print({action: "clear"});
}

Terminal.prototype.getOutput = function(haveIndex) {
	var terminal = this;
	return new Promise(function(resolve) {
		if (haveIndex + 1 < terminal._index) {
			resolve({index: terminal._index - 1, lines: terminal._lines.slice(haveIndex + 1 - terminal._firstLine)});
		} else {
			terminal._waiting.push(resolve);
		}
	});
}

Terminal.prototype.exportInterface = function() {
	return {
		print: this.print.bind(this),
		clear: this.clear.bind(this),
	};
}

function getTerminal() {
	if (!gTerminals.test) {
		gTerminals.test = new Terminal();
	}
	return gTerminals.test;
}

function sessionHandler(request, response, session) {
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
		var terminal = getTerminal();
		if (request.uri == "/terminal/send") {
			var command = request.body;
			terminal.print("> " + command);
			imports.shell.evaluate(terminal.exportInterface(), command);
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
	imports.auth.query(request.headers).then(function(authResponse) {
		if (authResponse) {
			sessionHandler(request, response, authResponse.session);
		} else {
			response.writeHead(303, {"Location": "/login?return=/terminal", "Connection": "close"});
			response.end();
		}
	});
}

imports.httpd.all("/terminal", handler);
