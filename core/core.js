"use strict";

var terminal = require("terminal");
var auth = require("auth");

var gProcessIndex = 0;
var gProcesses = {};

var kPingInterval = 60 * 1000;

function getCookies(headers) {
	var cookies = {};

	if (headers.cookie) {
		var parts = headers.cookie.split(/,|;/);
		for (var i in parts) {
			var equals = parts[i].indexOf("=");
			var name = parts[i].substring(0, equals).trim();
			var value = parts[i].substring(equals + 1).trim();
			cookies[name] = value;
		}
	}

	return cookies;
}

function packageFilePath(packageName, fileName) {
	if (packageName.indexOf("..") != -1 && fileName.indexOf("..") != -1) {
		return null;
	} else {
		return 'packages/' + packageName + '/' + fileName;
	}
}

function makeSessionId() {
	var id = "";
	for (var i = 0; i < 64; i++) {
		id += (Math.floor(Math.random() * 16)).toString(16);
	}
	return id;
}

function printError(out, error) {
	if (error.stackTrace) {
		out.print(error.fileName + ":" + error.lineNumber + ": " + error.message);
		out.print(error.stackTrace);
	} else {
		for (var i in error) {
			out.print(i);
		}
		out.print(error.toString());
	}
}

function broadcastEvent(eventName, argv) {
	var promises = [];
	for (var i in gProcesses) {
		var process = gProcesses[i];
		promises.push(invoke(process.eventHandlers[eventName], argv));
	}
	return Promise.all(promises);
}

function broadcast(message) {
	var sender = this;
	var promises = [];
	var from = getUser(sender);
	for (var i in gProcesses) {
		var process = gProcesses[i];
		if (process != sender && process.packageName == sender.packageName) {
			promises.push(invoke(process.eventHandlers['onMessage'], [from, message]));
		}
	}
	return Promise.all(promises);
}

function getDatabase(process) {
	if (!process.database && process.packageName != "auth") {
		File.makeDirectory('data');
		File.makeDirectory('data/' + process.packageName);
		File.makeDirectory('data/' + process.packageName + "/db");
		process.database = new Database('data/' + process.packageName + '/db');
	}
	return process.database;
}

function databaseGet(key) {
	var db = getDatabase(this);
	return db.get(key);
}

function databaseSet(key, value) {
	var db = getDatabase(this);
	return db.set(key, value);
}

function databaseRemove(key) {
	var db = getDatabase(this);
	return db.remove(key);
}

function databaseGetAll() {
	var db = getDatabase(this);
	return db.getAll();
}

function getPackages() {
	return File.readDirectory('packages/').filter(function(name) { return name.charAt(0) != '.'; });
}

function getUser(process) {
	return {
		name: process.userName,
		index: process.index,
		packageName: process.packageName,
	};
}

function getUsers(packageName) {
	var result = [];
	for (var key in gProcesses) {
		var process = gProcesses[key];
		if (!packageName || process.packageName == packageName) {
			result.push(getUser(process));
		}
	}
	return result;
}

function ping() {
	var process = this;
	var now = Date.now();
	var again = true;
	if (now - process.lastActive < process.timeout) {
		// Active.
	} else if (process.lastPing > process.lastActive) {
		// We lost them.
		process.task.kill();
		again = false;
	} else {
		// Idle.  Ping them.
		process.terminal.ping();
		process.lastPing = now;
	}

	if (again) {
		setTimeout(ping.bind(process), process.timeout);
	}
}

function postMessage(from, message) {
	var process = this;
	return invoke(process.eventHandlers['onMessage'], [getUser(from), message]);
}

function getService(service) {
	var process = this;
	var serviceProcess = getServiceProcess(process.packageName, service);
	return serviceProcess.ready.then(function() {
		return {
			postMessage: postMessage.bind(serviceProcess, process),
		}
	});
}

function getSessionProcess(packageName, session, options) {
	var actualOptions = {terminal: true, timeout: kPingInterval};
	if (options) {
		for (var i in options) {
			actualOptions[i] = options[i];
		}
	}
	return getProcess(packageName, 'session_' + session, actualOptions);
}

function getServiceProcess(packageName, service, options) {
	return getProcess(packageName, 'service_' + packageName + '_' + service, options || {});
}

function getProcess(packageName, key, options) {
	var process = gProcesses[key];
	if (!process && !(options && "create" in options && !options.create)) {
		print("Creating task for " + packageName + " " + key);
		process = {};
		process.index = gProcessIndex++;
		process.userName = options.userName || ('user' + process.index);
		process.task = new Task();
		process.eventHandlers = {};
		process.packageName = packageName;
		process.terminal = new Terminal();
		process.database = null;
		process.lastActive = Date.now();
		process.lastPing = null;
		process.timeout = options.timeout;
		var resolveReady;
		var rejectReady;
		process.ready = new Promise(function(resolve, reject) {
			resolveReady = resolve;
			rejectReady = reject;
		});
		gProcesses[key] = process;
		process.task.onExit = function(exitCode, terminationSignal) {
			broadcastEvent('onSessionEnd', [getUser(process)]);
			if (terminationSignal) {
				process.terminal.print("Process terminated with signal " + terminationSignal + ".");
			} else {
				process.terminal.print("Process ended with exit code " + exitCode + ".");
			}
			delete gProcesses[key];
		};
		if (process.timeout > 0) {
			setTimeout(ping.bind(process), process.timeout);
		}
		var imports = {
			'core': {
				'broadcast': broadcast.bind(process),
				'getService': getService.bind(process),
				'getPackages': getPackages.bind(process),
				'getUsers': getUsers.bind(process),
				'register': function(eventName, handler) {
					if (!process.eventHandlers[eventName]) {
						process.eventHandlers[eventName] = [];
					}
					process.eventHandlers[eventName].push(handler);
				},
				'getUser': getUser.bind(process, process),
				'user': getUser(process),
			},
			'database': {
				'get': databaseGet.bind(process),
				'set': databaseSet.bind(process),
				'remove': databaseRemove.bind(process),
				'getAll': databaseGetAll.bind(process),
			},
		};
		if (options.terminal) {
			imports.terminal = {
				'print': process.terminal.print.bind(process.terminal),
				'clear': process.terminal.clear.bind(process.terminal),
				'readLine': process.terminal.readLine.bind(process.terminal),
				'setEcho': process.terminal.setEcho.bind(process.terminal),
				'notify': process.terminal.notify.bind(process.terminal),
			};
		}
		process.task.setImports(imports);
		print("Activating task");
		process.task.activate();
		print("Executing task");
		try {
			process.task.execute(packageFilePath(packageName, packageName + ".js")).then(function() {
				print("Task ready");
				broadcastEvent('onSessionBegin', [getUser(process)]);
				resolveReady(process);
			}).catch(function(error) {
				printError(process.terminal, error);
				rejectReady();
			});
		} catch (error) {
			printError(process.terminal, error);
			rejectReady();
		}
	}
	return process;
}

function updateProcesses(packageName) {
	for (var i in gProcesses) {
		var process = gProcesses[i];
		if (process.packageName == packageName) {
			process.task.kill();
		}
	}
}

var kIgnore = ["/favicon.ico"];

var auth = require("auth");
var httpd = require("httpd");
httpd.all("/login", auth.handler);
httpd.all("", function(request, response) {
	var packageName = request.uri.split("/")[1];
	var basePath = "/" + packageName;
	return terminal.handler(request, response, basePath);
});
