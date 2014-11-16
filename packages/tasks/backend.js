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

function sendLatestStatus(messageId) {
	parent.invoke({
		to: "system",
		action: "getPackageList",
	}).then(function(packages) {
		parent.invoke({
			to: "system",
			action: "getTasks",
		}).then(function(tasks) {
			parent.invoke({
				to: "httpd",
				response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + JSON.stringify({tasks: tasks, packages: packages}),
				messageId: messageId,
			});
		});
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
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 200 OK\nContent-Type: " + file.type + "\nConnection: close\n\n" + data,
						messageId: message.messageId,
					});
				});
				break;
			}
		}
		if (!found) {
			if (message.request.uri == "/tasks/get") {
				sendLatestStatus(message.messageId);
			} else if (message.request.uri == "/tasks/start") {
				form = decodeForm(message.request.query);
				parent.invoke({
					to: "system",
					action: "startTask",
					taskName: form.taskName,
				}).then(function(data) {
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\nOK",
						messageId: message.messageId,
					});
				});
			} else if (message.request.uri == "/tasks/restart") {
				form = decodeForm(message.request.query);
				parent.invoke({
					to: "system",
					action: "restartTask",
					taskName: form.taskName,
				}).then(function(data) {
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\nOK",
						messageId: message.messageId,
					});
				});
			} else if (message.request.uri == "/tasks/stop") {
				form = decodeForm(message.request.query);
				parent.invoke({
					to: "system",
					action: "stopTask",
					taskName: form.taskName,
				}).then(function(data) {
					parent.invoke({
						to: "httpd",
						response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\nOK",
						messageId: message.messageId,
					});
				});
			} else if (message.request.uri == "/tasks/changes") {
				gWatchers.push(message.messageId);
			}
		}
	} else if (message.action == "add") {
		print("I got an add request");
		message.data(2, 3).then(function (result) {
			print(result);
			for (var i in result) {
				print(i);
			}
			print("I think I got a result: " + JSON.stringify(result));
		}).catch(function (e) {
			print("Guess it failed: " + e);
		});
		print("I think I called a remote function.");
	} else if (message.action == "taskStarted" || message.action == "updateTaskStatus") {
		for (var i in gWatchers) {
			sendLatestStatus(gWatchers[i]);
		}
	}
}
