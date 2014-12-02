var kStaticFiles = [
	{uri: '/tasks', path: 'index.html', type: 'text/html'},
	{uri: '/tasks/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

var gWatchers = [];

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

function sendLatestStatus(message) {
	parent.invoke({
		to: "system",
		action: "getPackageList",
	}).then(function(packages) {
		parent.invoke({
			to: "system",
			action: "getTasks",
		}).then(function(tasks) {
			message.response.writeHead(200, {"Content-Type": "text/plain", "Connection": "close"});
			message.response.end(JSON.stringify({tasks: tasks, packages: packages}));
		});
	});
}

function removeIfDisconnected(connection) {
	connection.response.isConnected().then(function (connected) {
		if (!connected) {
			var index = gWatchers.indexOf(connection);
			if (index != -1) {
				gWatchers.splice(index, 1);
			}
		}
	});
}

function onMessage(from, message) {
	if (message.request) {
		var found = false;
		for (var i in kStaticFiles) {
			if (kStaticFiles[i].uri == message.request.uri) {
				found = true;
				var file = kStaticFiles[i];
				parent.invoke({
					to: "system",
					action: "get",
					fileName: file.path,
				}).then(function(data) {
					message.response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
					message.response.end(data);
				});
				break;
			}
		}
		if (!found) {
			if (message.request.uri == "/tasks/get") {
				sendLatestStatus(message);
			} else if (message.request.uri == "/tasks/start") {
				form = decodeForm(message.request.query);
				parent.invoke({
					to: "system",
					action: "startTask",
					taskName: form.taskName,
				}).then(function(data) {
					message.response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
					message.response.end("OK");
				});
			} else if (message.request.uri == "/tasks/restart") {
				form = decodeForm(message.request.query);
				parent.invoke({
					to: "system",
					action: "restartTask",
					taskName: form.taskName,
				}).then(function(data) {
					message.response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
					message.response.end("OK");
				});
			} else if (message.request.uri == "/tasks/stop") {
				form = decodeForm(message.request.query);
				parent.invoke({
					to: "system",
					action: "stopTask",
					taskName: form.taskName,
				}).then(function(data) {
					message.response.writeHead(200, {"Content-Type": file.type, "Connection": "close"});
					message.response.end("OK");
				});
			} else if (message.request.uri == "/tasks/changes") {
				gWatchers.push(message);
				for (var i in gWatchers) {
					removeIfDisconnected(gWatchers[i]);
				}
			}
		}
	} else if (message.action == "taskStarted" || message.action == "updateTaskStatus") {
		for (var i in gWatchers) {
			sendLatestStatus(gWatchers[i]);
		}
		gWatchers.length = 0;
	}
}
