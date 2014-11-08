var startupTasks = JSON.parse(readFile('startup.js'));

var tasks = {};

for (var taskName in startupTasks) {
	print(taskName);
	tasks[taskName] = startScript(startupTasks[taskName]);
}

function onMessage(from, message) {
	print("system onMessage: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	if (message.to) {
		tasks[message.to].invoke(message);
	} else {
		for (var taskName in tasks) {
			if (from.id != taskName.id) {
				tasks[taskName].invoke(message);
			}
		}
	}
	return true;
}
