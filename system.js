var startupTasks = JSON.parse(readFile('startup.js'));
print(startupTasks);

var tasks = {};

for (var i in startupTasks) {
	var task = startupTasks[i];
	print(task);
	print(task.name + " => " + startupTasks[task.name]);
	tasks[task.name] = startScript(task.fileName, task.trusted);
	tasks[task.name].fileName = task.fileName;
	tasks[task.name].trusted = task.trusted;
	print(tasks[task.name]);
}

function packageFilePath(packageName, fileName) {
	if (packageName.indexOf("..") != -1 && fileName.indexOf(".." != -1)) {
		return null;
	} else {
		return 'packages/' + packageName + '/' + fileName;
	}
}

function getTaskName(task) {
	var name;
	for (var taskName in tasks) {
		if (tasks[taskName].id == task.id) {
			name = taskName;
		}
	}
	return name;
}

function onMessage(from, message) {
	print("system onMessage: " + JSON.stringify(from) + ", " + JSON.stringify(message));
	if (message.to == "system") {
		var fromName = getTaskName(from);
		print(fromName);

		// Let the editor get/update anything.
		if (fromName == "editor"
			|| (!message.taskName && message.action == "get")) {
			if (message.action == "stopTask") {
				print("killing " + message.taskName);
				print(tasks[message.taskName]);
				tasks[message.taskName].kill();
			} else if (message.action == "startTask" && tasks[message.taskName]) {
				var fileName = tasks[message.taskName].fileName;
				if (fileName) {
					print(fileName);
					var trusted = tasks[message.taskName].trusted;
					tasks[message.taskName] = startScript(fileName, trusted);
					tasks[message.taskName].fileName = fileName;
				}
			} else if (message.action == "restartTask" && tasks[message.taskName]) {
				print("killing " + message.taskName);
				print(tasks[message.taskName]);
				tasks[message.taskName].kill();
				var fileName = tasks[message.taskName].fileName;
				if (fileName) {
					print(fileName);
					var trusted = tasks[message.taskName].trusted;
					tasks[message.taskName] = startScript(fileName, trusted);
					tasks[message.taskName].fileName = fileName;
				}
			} else if (message.action == "put") {
				var fileName = packageFilePath(message.taskName, message.fileName);
				print("fileName = " + fileName);
				if (fileName) {
					print("write => " + writeFile(fileName, message.contents));
				}
			} else if (message.action == "get") {
				var fileName = packageFilePath(message.taskName || fromName, message.fileName);
				if (fileName) {
					return readFile(fileName);
				}
			} else if (message.action == "getPackageList") {
				var list = readDirectory("packages/");
				list.sort();
				var finalList = [];
				for (var i in list) {
					if (list[i][0] != ".") {
						finalList.push(list[i]);
					}
				}
				return finalList;
			} else if (message.action == "list") {
				var list = readDirectory(packageFilePath(message.taskName, ""));
				list.sort();
				var finalList = [];
				for (var i in list) {
					if (list[i][0] != ".") {
						finalList.push(list[i]);
					}
				}
				return finalList;
			}
		} else if (message.action == "getPackageContents") {
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
}
