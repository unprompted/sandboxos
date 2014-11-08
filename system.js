var startupTasks = JSON.parse(readFile('startup.js'));

var tasks = {};

for (var taskName in startupTasks) {
	print(taskName + " => " + startupTasks[taskName]);
	tasks[taskName] = startScript(startupTasks[taskName]);
	print(tasks[taskName]);
}

function onMessage(from, message) {
	print("system onMessage: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	if (message.to == "system") {
		if (message.action == "update") {
			print("system responding");
			print("kill");
			print(message.taskName);
			print(tasks[message.taskName].kill());
			print("fileName");
			var fileName = startupTasks[message.taskName];
			print(fileName);
			print("write");
			print(writeFile(fileName, message.script));
			print("start");
			tasks[message.taskName] = startScript(fileName);
			print(tasks[message.taskName]);
			print("ok");
			return JSON.stringify("update!");
		}
	} else if (message.to) {
		print("invoking on " + tasks[message.to]);
		tasks[message.to].invoke(message);
		print("invoked?");
	} else {
		for (var taskName in tasks) {
			if (from.id != taskName.id) {
				tasks[taskName].invoke(message);
			}
		}
	}
	return true;
}
