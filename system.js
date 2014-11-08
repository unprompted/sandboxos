var startupTasks = JSON.parse(readFile('startup.js'));
print(startupTasks);

var tasks = {};

for (var i in startupTasks) {
	var task = startupTasks[i];
	print(task);
	print(task.name + " => " + startupTasks[task.name]);
	tasks[task.name] = startScript(task.fileName, task.trusted);
	tasks[task.name].fileName = task.fileName;
	print(tasks[task.name]);
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
			var fileName = tasks[message.taskName].fileName;
			print(fileName);
			print("write");
			print(writeFile(fileName, message.script));
			print("start");
			tasks[message.taskName] = startScript(fileName);
			tasks[message.taskName].fileName = fileName;
			print(tasks[message.taskName]);
			print("ok");
			return JSON.stringify("update!");
		} else if (message.action == "get") {
			if (message.taskName == "handler") {
				return readFile("handler.js");
			} else {
				return undefined;
			}
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
