"use strict";

var kStaticFiles = [
	{uri: '', path: 'index.html', type: 'text/html'},
	{uri: '/edit', path: 'edit.html', type: 'text/html'},
	{uri: '/style.css', path: 'style.css', type: 'text/css'},
	{uri: '/client.js', path: 'client.js', type: 'text/javascript'},
	{uri: '/editor.js', path: 'editor.js', type: 'text/javascript'},
];
var kBacklog = 64;

var form = require('form');

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

function invoke(handlers, argv) {
	var promises = [];
	for (var i = 0; i < handlers.length; ++i) {
		promises.push(handlers[i].apply(null, argv));
	}
	return Promise.all(promises);
}

function handler(request, response, basePath, session, process) {
	var found = false;
	var sessionCookie = "terminalSession=" + session + "; path=" + basePath + "; Max-Age=604800";

	for (var i in kStaticFiles) {
		if (("/terminal" + kStaticFiles[i].uri === request.uri) ||
			(basePath + kStaticFiles[i].uri === request.uri)) {
			found = true;
			var data = File.readFile("packages/core/" + kStaticFiles[i].path);
			if (kStaticFiles[i].uri == "") {
				data = data.replace("$(VIEW_SOURCE)", basePath + "/view");
				data = data.replace("$(EDIT_SOURCE)", basePath + "/edit");
			} else if (kStaticFiles[i].uri == "/edit") {
				var packageName = basePath.substring(1);
				var source = File.readFile("packages/" + packageName + "/" + packageName + ".js") || "";
				source = source.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;');
				data = data.replace("$(SOURCE)", source);
			}
			response.writeHead(200, {"Content-Type": kStaticFiles[i].type, "Connection": "close", "Set-Cookie": sessionCookie});
			response.end(data);
			break;
		}
	}
	if (!found) {
		if (!process.terminal) {
			process.terminal = new Terminal();
		}
		if (request.uri == basePath + "/send") {
			var command = request.body;
			process.terminal.print("> " + command);
			invoke(process.eventHandlers['onInput'], [command]).then(function() {
				response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close", "Set-Cookie": sessionCookie});
				response.end("");
			}).catch(function(error) {
				process.terminal.print(error);
			});
		} else if (request.uri == basePath + "/receive") {
			process.terminal.getOutput(parseInt(request.body)).then(function(output) {
				response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close", "Set-Cookie": sessionCookie});
				response.end(JSON.stringify(output));
			});
		} else if (request.uri == basePath + "/view") {
			var packageName = basePath.substring(1);
			var data = File.readFile("packages/" + packageName + "/" + packageName + ".js");
			response.writeHead(200, {"Content-Type": "text/javascript", "Connection": "close", "Set-Cookie": sessionCookie});
			response.end(data);
		} else if (request.uri == basePath + "/save") {
			var packageName = basePath.substring(1);
			if (packageName == "core" ||
				packageName.indexOf(".") != -1 ||
				packageName.indexOf("/") != -1)
			{
				response.writeHead(403, {"Content-Type": "text/plain", "Connection": "close", "Set-Cookie": sessionCookie});
				response.end("Invalid package name: " + packageName);
			} else {
				File.makeDirectory("packages/" + packageName)
				if (!File.writeFile("packages/" + packageName + "/" + packageName + ".js", request.body || "")) {
					response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close", "Set-Cookie": sessionCookie});
					response.end();
					updateProcesses(packageName);
				} else {
					response.writeHead(500, {"Content-Type": "text/plain", "Connection": "close", "Set-Cookie": sessionCookie});
					response.end("Problem saving: " + packageName);
				}
			}
		} else {
			response.writeHead(404, {"Content-Type": "text/plain", "Connection": "close", "Set-Content": sessionCookie});
			response.end("404 File not found");
		}
	}
}

exports.handler = handler;
