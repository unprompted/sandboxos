var kStaticFiles = [
	{uri: '/tasks', path: 'index.html', type: 'text/html'},
	{uri: '/tasks/frontend.js', path: 'frontend.js', type: 'text/javascript'},
];

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
							messageId: message.messageId,
						});
					});
				});
			}
		}
	}
}