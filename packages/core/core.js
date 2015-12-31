"use strict";

var terminal = require("terminal");

var gProcesses = {};

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

function getProcess(packageName, session) {
	var process = gProcesses[session];
	if (!process) {
		print("Creating task for " + packageName + " session " + session);
		process = {};
		process.task = new Task();
		process.eventHandlers = {'onInput': []};
		process.packageName = packageName;
		gProcesses[session] = process;
		process.task.onExit = function(exitCode, terminationSignal) {
			var instance = terminal.getTerminal("/" + packageName, session);
			if (terminationSignal) {
				instance.print("Process terminated with signal " + terminationSignal + ".");
			} else {
				instance.print("Process ended with exit code " + exitCode + ".");
			}
			delete gProcesses[session];
		};
		var instance = terminal.getTerminal("/" + packageName, session);
		process.task.setImports({'terminal': {
			'print': instance.print.bind(instance),
			'register': function(eventName, handler) {
				process.eventHandlers[eventName].push(handler);
			},
		}});
		print("Activating task");
		process.task.activate();
		print("Executing task");
		try {
			process.task.execute(packageFilePath(packageName, packageName + ".js")).then(function() {
				print("Task ready");
			}).catch(function(error) {
				printError(instance, error);
			});
		} catch (error) {
			printError(instance, error);
		}
	}
	return process;
}

function updateProcesses(packageName) {
	for (var i in gProcesses) {
		var process = gProcesses[i];
		if (process.packageName == packageName) {
			process.task.kill();
			process.task = null;
		}
	}
}

var kIgnore = ["/favicon.ico"];

var httpd = require("httpd");
httpd.all("", function(request, response) {
	var packageName = request.uri.split("/")[1];
	var basePath = "/" + packageName;
	var session = getCookies(request.headers).terminalSession || makeSessionId();
	var process = null;
	if (packageName != "terminal" &&
		kIgnore.indexOf(request.uri) == -1) {
		process = getProcess(packageName, session);
	}

	return terminal.handler(request, response, basePath, session, process);
});
