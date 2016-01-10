"use strict";

var terminal = require("terminal");

var gProcessIndex = 0;
var gProcesses = {};

var kPingInterval = 480 * 1000;

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
	for (var i in gProcesses) {
		var process = gProcesses[i];
		if (process != sender && process.packageName == sender.packageName) {
			promises.push(invoke(process.eventHandlers['onMessage'], [message]));
		}
	}
	return Promise.all(promises);
}

function sendToLeader(message) {
	var sender = this;
	var promises = [];
	for (var i in gProcesses) {
		var process = gProcesses[i];
		if (process.packageName == sender.packageName) {
			promises.push(invoke(process.eventHandlers['onMessage'], [message]));
			break;
		}
	}
	return Promise.all(promises);
}

function getDatabase(process) {
	if (!process.database) {
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

function getUsers(packageName) {
	var result = [];
	for (var session in gProcesses) {
		var process = gProcesses[session];
		if (!packageName || process.packageName == packageName) {
			result.push({
				index: process.index,
				packageName: process.packageName,
			});
		}
	}
	return result;
}

function ping() {
	var process = this;
	var now = Date.now();
	var again = true;
	if (now - process.lastActive < kPingInterval) {
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
		setTimeout(ping.bind(process), kPingInterval);
	}
}

function getProcess(packageName, session) {
	var process = gProcesses[session];
	if (!process) {
		print("Creating task for " + packageName + " session " + session);
		process = {};
		process.index = gProcessIndex++;
		process.task = new Task();
		process.eventHandlers = {};
		process.packageName = packageName;
		process.terminal = new Terminal();
		process.database = null;
		process.lastActive = Date.now();
		process.lastPing = null;
		gProcesses[session] = process;
		process.task.onExit = function(exitCode, terminationSignal) {
			broadcastEvent('onSessionEnd', [{packageName: process.packageName, index: process.index}]);
			if (terminationSignal) {
				process.terminal.print("Process terminated with signal " + terminationSignal + ".");
			} else {
				process.terminal.print("Process ended with exit code " + exitCode + ".");
			}
			delete gProcesses[session];
		};
		setTimeout(ping.bind(process), kPingInterval);
		process.task.setImports({
			'core': {
				'broadcast': broadcast.bind(process),
				'sendToLeader': sendToLeader.bind(process),
				'getPackages': getPackages.bind(process),
				'getUsers': getUsers.bind(process),
			},
			'database': {
				'get': databaseGet.bind(process),
				'set': databaseSet.bind(process),
				'remove': databaseRemove.bind(process),
				'getAll': databaseGetAll.bind(process),
			},
			'terminal': {
				'print': process.terminal.print.bind(process.terminal),
				'clear': process.terminal.clear.bind(process.terminal),
				'register': function(eventName, handler) {
					if (!process.eventHandlers[eventName]) {
						process.eventHandlers[eventName] = [];
					}
					process.eventHandlers[eventName].push(handler);
				},
			},
		});
		print("Activating task");
		process.task.activate();
		print("Executing task");
		try {
			process.task.execute(packageFilePath(packageName, packageName + ".js")).then(function() {
				print("Task ready");
				broadcastEvent('onSessionBegin', [{packageName: process.packageName, index: process.index}]);
			}).catch(function(error) {
				printError(process.terminal, error);
			});
		} catch (error) {
			printError(process.terminal, error);
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

var httpd = require("httpd");
httpd.all("", function(request, response) {
	var packageName = request.uri.split("/")[1];
	var basePath = "/" + packageName;
	return terminal.handler(request, response, basePath);
});
