"use strict";

var kStaticFiles = [
	{uri: '', path: 'index.html', type: 'text/html; charset=utf-8'},
	{uri: '/edit', path: 'edit.html', type: 'text/html; charset=utf-8'},
	{uri: '/style.css', path: 'style.css', type: 'text/css; charset=utf-8'},
	{uri: '/favicon.png', path: 'favicon.png', type: 'image/png'},
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
	this._selected = null;
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
	var data = arguments;
	if (this._selected) {
		data = {
			terminal: this._selected,
			value: data
		};
	}
	this._lines.push(data);
	this._index++;
	if (this._lines.length >= Terminal.kBacklog * 2) {
		this._firstLine = this._index - Terminal.kBacklog;
		this._lines = this._lines.slice(this._lines.length - Terminal.kBacklog);
	}
	this.dispatch({index: this._index - 1, lines: [data]});
	this._lastWrite = new Date();
}

Terminal.prototype.notify = function(title, options) {
	this.print({action: "notify", title: title, options: options});
}

Terminal.prototype.setTitle = function(value) {
	this.print({action: "title", value: value});
}

Terminal.prototype.setPrompt = function(value) {
	this.print({action: "prompt", value: value});
}

Terminal.prototype.setHash = function(value) {
	this.print({action: "hash", value: value});
}

Terminal.prototype.notifyUpdate = function() {
	this.print({action: "update"});
}

Terminal.prototype.configure = function(name, options) {
	this.print({action: "configure", name: name, options: options});
}

Terminal.prototype.select = function(name) {
	this._selected = name;
}

Terminal.prototype.clear = function() {
	//this._lines.length = 0;
	//this._firstLine = this._index;
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
			resolve({index: terminal._index - 1, lines: terminal._lines.slice(Math.max(0, haveIndex + 1 - terminal._firstLine))});
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

function handler(request, response, packageOwner, packageName, uri) {
	var found = false;

	if (badName(packageOwner) || badName(packageName)) {
		var data = "File not found";
		response.writeHead(404, {"Content-Type": "text/plain; charset=utf-8", "Content-Length": data.length});
		response.end(data);
		found = true;
	}

	if (!found) {
		for (var i in kStaticFiles) {
			if (uri === kStaticFiles[i].uri) {
				found = true;
				var data = File.readFile("core/" + kStaticFiles[i].path);
				if (kStaticFiles[i].uri == "") {
					data = data.replace("$(VIEW_SOURCE)", "/~" + packageOwner + "/" + packageName + "/view");
					data = data.replace("$(EDIT_SOURCE)", "/~" + packageOwner + "/" + packageName + "/edit");
				} else if (kStaticFiles[i].uri == "/edit") {
					var source = File.readFile("packages/" + packageOwner + "/" + packageName + "/" + packageName + ".js") || "";
					source = source.replace(/([&<>"])/g, function(x, item) {
						return {'&': '&amp;', '"': '&quot;', '<': '&lt;', '>': '&gt;'}[item];
					});
					data = data.replace("$(SOURCE)", source);
				}
				response.writeHead(200, {"Content-Type": kStaticFiles[i].type, "Content-Length": data.length});
				response.end(data);
				break;
			}
		}
	}

	if (!found) {
		var process;
		if (uri === "/view") {
			var data = File.readFile("packages/" + packageOwner + "/" + packageName + "/" + packageName + ".js");
			response.writeHead(200, {"Content-Type": "text/javascript; charset=utf-8", "Content-Length": data.length});
			response.end(data);
		} else if (uri == "/save") {
			var credentials = auth.query(request.headers);
			var userName = credentials && credentials.session && credentials.session.name ? credentials.session.name : "guest";
			if (badName(packageName)) {
				response.writeHead(403, {"Content-Type": "text/plain; charset=utf-8"});
				response.end("Invalid package name: " + packageName);
			} else if (badName(userName)) {
				response.writeHead(403, {"Content-Type": "text/plain; charset=utf-8"});
				response.end("Invalid user name: " + userName);
			} else {
				File.makeDirectory("packages/" + userName);
				File.makeDirectory("packages/" + userName + "/" + packageName);
				if (!File.writeFile("packages/" + userName + "/" + packageName + "/" + packageName + ".js", request.body || "")) {
					response.writeHead(200, {"Content-Type": "text/plain; charset=utf-8"});
					response.end("/~" + userName + "/" + packageName);
					updateProcesses(userName, packageName);
				} else {
					response.writeHead(500, {"Content-Type": "text/plain; charset=utf-8"});
					response.end("Problem saving: " + packageName);
				}
			}
		} else {
			var options = {};
			var credentials = auth.query(request.headers);
			if (credentials && credentials.session) {
				options.userName = credentials.session.name;
			}
			options.credentials = credentials;
			var sessionId = form.decodeForm(request.query).sessionId;
			var isNewSession = false;
			if (!getSessionProcess(packageOwner, packageName, sessionId, {create: false})) {
				sessionId = makeSessionId();
				isNewSession = true;
			}
			process = getSessionProcess(packageOwner, packageName, sessionId, options);
			process.lastActive = Date.now();

			if (uri === "/send") {
				if (isNewSession) {
					response.writeHead(403, {"Content-Type": "text/plain; charset=utf-8"});
					response.end("Too soon.");
				} else {
					var command = JSON.parse(request.body);
					if (process.terminal._echo && typeof command == "string") {
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
				}
			} else if (uri === "/receive") {
				if (isNewSession) {
					var data = JSON.stringify({
						lines: [
							{
								action: "session",
								session: {
									sessionId: sessionId,
									credentials: credentials,
								}
							},
						]
					});
					response.writeHead(200, {
						"Content-Type": "text/plain; charset=utf-8",
						"Content-Length": data.length.toString(),
						"Cache-Control": "no-cache, no-store, must-revalidate",
						"Pragma": "no-cache",
						"Expires": "0",
					});
					process.ready.then(function() {
						process.terminal.print({action: "ready", ready: true});
					}).catch(function(error) {
						process.terminal.print({action: "ready", error: error});
					});
					response.end(data);
				} else {
					return process.terminal.getOutput(parseInt(request.body)).then(function(output) {
						var data = JSON.stringify(output);
						response.writeHead(200, {
							"Content-Type": "text/plain; charset=utf-8",
							"Content-Length": data.length.toString(),
							"Cache-Control": "no-cache, no-store, must-revalidate",
							"Pragma": "no-cache",
							"Expires": "0",
						});
						response.end(data);
					});
				}
			}
		}
	}
}

exports.handler = handler;
