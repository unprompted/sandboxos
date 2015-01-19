var kStaticFiles = [
	{uri: '/tasks', path: 'index.html', type: 'text/html'},
	{uri: '/tasks/frontend.js', path: 'frontend.js', type: 'text/javascript'},
	{uri: '/tasks/style.css', path: 'style.css', type: 'text/css'},
];

var gWatchers = [];

var packageFs;
imports.filesystem.getPackage().then(function(fs) { packageFs = fs; });

function decode(encoded) {
	var result = "";
	for (var i = 0; i < encoded.length; i++) {
		var c = encoded[i];
		if (c == "+") {
			result += " ";
		} else if (c == "%") {
			result += String.fromCharCode(parseInt(encoded.slice(i + 1, i + 3), 16));
			i += 2;
		} else {
			result += c;
		}
	}
	return result;
}

function decodeForm(encoded) {
	var result = {};
	if (encoded) {
		encoded = encoded.trim();
		var items = encoded.split('&');
		for (var i = 0; i < items.length; i++) {
			var item = items[i];
			var equals = item.indexOf('=');
			var key = decode(item.slice(0, equals));
			var value = decode(item.slice(equals + 1));
			result[key] = value;
		}
	}
	return result;
}

function sendLatestStatus(response) {
	imports.system.getPackageList().then(function(packages) {
		imports.system.getTasks().then(function(tasks) {
			response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
			response.end(JSON.stringify({tasks: tasks, packages: packages}));
		});
	});
}

function removeIfDisconnected(connection) {
	connection.isConnected().then(function (connected) {
		if (!connected) {
			var index = gWatchers.indexOf(connection);
			if (index != -1) {
				gWatchers.splice(index, 1);
			}
		}
	});
}

function handle(request, response) {
	var found = false;
	for (var i in kStaticFiles) {
		if (kStaticFiles[i].uri == request.uri) {
			found = true;
			var file = kStaticFiles[i];
			packageFs.readFile(file.path).then(function(data) {
				response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
				response.end(data);
			});
			break;
		}
	}
	if (!found) {
		if (request.uri == "/tasks/get") {
			sendLatestStatus(response);
		} else if (request.uri == "/tasks/start") {
			form = decodeForm(request.query);
			imports.system.startTask(form.taskName).then(function(data) {
				response.writeHead(200, {"Content-Type": "text", "Connection": "close"});
				response.end("OK");
			});
		} else if (request.uri == "/tasks/restart") {
			form = decodeForm(request.query);
			imports.system.restartTask(form.taskName).then(function(data) {
				response.writeHead(200, {"Content-Type": "text", "Connection": "close"});
				response.end("OK");
			});
		} else if (request.uri == "/tasks/stop") {
			form = decodeForm(request.query);
			imports.system.stopTask(form.taskName).then(function(data) {
				response.writeHead(200, {"Content-Type": "text", "Connection": "close"});
				response.end("OK");
			});
		} else if (request.uri == "/tasks/changes") {
			gWatchers.push(response);
			for (var i in gWatchers) {
				removeIfDisconnected(gWatchers[i]);
			}
		}
	}
}

imports.system.registerTaskStatusChanged(function(taskName, taskStatus) {
	for (var i in gWatchers) {
		sendLatestStatus(gWatchers[i]);
	}
	gWatchers.length = 0;
});

imports.shell.register("task", function(terminal, argv) {
	var showUsage = false;
	if (argv.length == 3) {
		if (argv[1] == "start") {
			imports.system.startTask(argv[2]).then(function() {
				terminal.print("Task " + argv[2] + " started.");
			}).catch(function(error) {
				terminal.print(error);
			});
		} else if (argv[1] == "stop") {
			imports.system.stopTask(argv[2]).then(function() {
				terminal.print("Task " + argv[2] + " stopped.");
			}).catch(function(error) {
				terminal.print(error);
			});
		} else if (argv[1] == "restart") {
			imports.system.restartTask(argv[2]).then(function() {
				terminal.print("Task " + argv[2] + " restarted");
			}).catch(function(error) {
				terminal.print(error);
			});
		} else {
			showUsage = true;
		}
	} else if (argv.length == 2 && argv[1] == "list") {
		Promise.all([
			imports.system.getPackageList(),
			imports.system.getTasks(),
		]).then(function(data) {
			terminal.print("packages: " + JSON.stringify(data[0]));
			terminal.print("tasks: " + JSON.stringify(data[1]));
		}).catch(function(error) {
			terminal.print(error);
		});
	} else {
		showUsage = true;
	}
	
	if (showUsage) {
		terminal.print("Usage: task start|stop|restart taskName");
		terminal.print("   or: task list");
	}
});

imports.httpd.get('/tasks', handle);