"use strict";

var kStaticFiles = [
	{uri: '', path: 'index.html', type: 'text/html; charset=utf-8'},
	{uri: '/edit', path: 'edit.html', type: 'text/html; charset=utf-8'},
	{uri: '/style.css', path: 'style.css', type: 'text/css; charset=utf-8'},
	{uri: '/client.js', path: 'client.js', type: 'text/javascript; charset=utf-8'},
	{uri: '/editor.js', path: 'editor.js', type: 'text/javascript; charset=utf-8'},
];

var auth = require('auth');
var form = require('form');

function Terminal() {
	this._waiting = [];
	this._index = 0;
	this._firstLine = 0;
	this._lines = [];
	this._lastRead = null;
	this._lastWrite = null;
	this._echo = true;
	this._readLine = null;
	return this;
}

Terminal.kBacklog = 64;

Terminal.prototype.dispatch = function(data) {
	for (var i in this._waiting) {
		this._waiting[i](data);
	}
	this._waiting.length = 0;
}

Terminal.prototype.print = function() {
	this._lines.push(arguments);
	this._index++;
	if (this._lines.length >= Terminal.kBacklog * 2) {
		this._firstLine = this._index - Terminal.kBacklog;
		this._lines = this._lines.slice(this._lines.length - Terminal.kBacklog);
	}
	this.dispatch({index: this._index - 1, lines: [arguments]});
	this._lastWrite = new Date();
}

Terminal.prototype.notify = function(title, options) {
	this.print({action: "notify", title: title, options: options});
}

Terminal.prototype.clear = function() {
	this._lines.length = 0;
	this._firstLine = this._index;
	this.print({action: "clear"});
}

Terminal.prototype.ping = function() {
	this.dispatch({index: this._index - 1, lines: [{action: "ping"}]});
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

Terminal.prototype.setEcho = function(echo) {
	this._echo = echo;
}

Terminal.prototype.readLine = function() {
	var self = this;
	if (self._readLine) {
		self._readLine[1]();
	}
	return new Promise(function(resolve, reject) {
		self._readLine = [resolve, reject];
	});
}

function invoke(handlers, argv) {
	var promises = [];
	if (handlers) {
		for (var i = 0; i < handlers.length; ++i) {
			promises.push(handlers[i].apply({}, argv));
		}
	}
	return Promise.all(promises);
}

function handler(request, response, basePath) {
	var found = false;
	var formData = form.decodeForm(request.query);
	var packageName = basePath.substring(1) || "index";
	var process;

	if (formData.sessionId) {
		var options = {};
		var credentials = auth.query(request.headers);
		if (credentials && credentials.session) {
			options.userName = credentials.session.name;
		}
		process = getSessionProcess(packageName, formData.sessionId, options);
		process.lastActive = Date.now();
	}

	for (var i in kStaticFiles) {
		if (("/terminal" + kStaticFiles[i].uri === request.uri) ||
			(basePath + kStaticFiles[i].uri === request.uri) ||
			request.uri == "/") {
			found = true;
			var data = File.readFile("core/" + kStaticFiles[i].path);
			if (kStaticFiles[i].uri == "") {
				data = data.replace("$(VIEW_SOURCE)", "/" + packageName + "/view");
				data = data.replace("$(EDIT_SOURCE)", "/" + packageName + "/edit");
			} else if (kStaticFiles[i].uri == "/edit") {
				var source = File.readFile("packages/" + packageName + "/" + packageName + ".js") || "";
				source = source.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;');
				data = data.replace("$(SOURCE)", source);
			}
			response.writeHead(200, {"Content-Type": kStaticFiles[i].type, "Content-Length": data.length});
			response.end(data);
			break;
		}
	}
	if (!found) {
		if (request.uri == basePath + "/send") {
			var command = request.body;
			if (process.terminal._echo) {
				process.terminal.print("> " + command);
			}
			if (process.terminal._readLine) {
				let promise = process.terminal._readLine;
				process.terminal._readLine = null;
				promise[0](command);
			}
			return invoke(process.eventHandlers['onInput'], [command]).then(function() {
				response.writeHead(200, {
					"Content-Type": "text/plain; charset=utf-8",
					"Content-Length": "0",
					"Cache-Control": "no-cache, no-store, must-revalidate",
					"Pragma": "no-cache",
					"Expires": "0",
				});
				response.end("");
			}).catch(function(error) {
				process.terminal.print(error);
			});
		} else if (request.uri == basePath + "/receive") {
			process.terminal.getOutput(parseInt(request.body)).then(function(output) {
				var data = JSON.stringify(output);
				response.writeHead(200, {
					"Content-Type": "text/plain; charset=utf-8",
					"Content-Length": data.length.toString(),
					"Cache-Control": "no-cache, no-store, must-revalidate",
					"Pragma": "no-cache",
					"Expires": "0",
				});
				response.end(data);
			}).catch(function(error) {
				print("ERROR GETTING OUTPUT!");
			});
		} else if (request.uri == basePath + "/view") {
			var data = File.readFile("packages/" + packageName + "/" + packageName + ".js");
			response.writeHead(200, {"Content-Type": "text/javascript; charset=utf-8", "Content-Length": data.length});
			response.end(data);
		} else if (request.uri == basePath + "/save") {
			if (packageName == "core" ||
				packageName.indexOf(".") != -1 ||
				packageName.indexOf("/") != -1)
			{
				response.writeHead(403, {"Content-Type": "text/plain; charset=utf-8"});
				response.end("Invalid package name: " + packageName);
			} else {
				File.makeDirectory("packages/" + packageName);
				if (!File.writeFile("packages/" + packageName + "/" + packageName + ".js", request.body || "")) {
					response.writeHead(200, {"Content-Type": "text/plain; charset=utf-8"});
					response.end();
					updateProcesses(packageName);
				} else {
					response.writeHead(500, {"Content-Type": "text/plain; charset=utf-8"});
					response.end("Problem saving: " + packageName);
				}
			}
		} else if (request.uri == basePath + "/newSession") {
			var credentials = auth.query(request.headers);
			var result = JSON.stringify({'sessionId': makeSessionId(), 'credentials': credentials});
			response.writeHead(200, {
				"Content-Type": "text/javascript; charset=utf-8",
				"Content-Length": result.length,
				"Cache-Control": "no-cache, no-store, must-revalidate",
				"Pragma": "no-cache",
				"Expires": "0",
			});
			response.end(result);
		} else {
			response.writeHead(404, {"Content-Type": "text/plain; charset=utf-8"});
			response.end("404 File not found");
		}
	}
}

exports.handler = handler;