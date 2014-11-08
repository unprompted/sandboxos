var tasks = {};

function start() {
	for (var i in tasks) {
		tasks[i].task.kill();
		delete tasks[i];
	}
	var packages = getPackageList();
	for (var i in packages) {
		if (packages[i] != "system") {
			startTask(packages[i]);
		}
	}
}

function startTask(packageName) {
	var manifest;
	var task;
	try {
		manifest = JSON.parse(readFile(packageFilePath(packageName, "package.json")));
	} catch (error) {
		print(error);
	}

	if (manifest) {
		task = {}
		task.task = startScript(packageFilePath(packageName, manifest.start), manifest.trusted);
		task.manifest = manifest;
		tasks[packageName] = task
		broadcast(null, {action: "taskStarted", taskName: packageName});
	} else {
		print("Package " + packageName + " has no package.json.");
	}
	return task;
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
		if (tasks[taskName].task.id == task.id) {
			name = taskName;
		}
	}
	return name;
}

function getPackageList() {
	var list = readDirectory("packages/");
	list.sort();
	var finalList = [];
	for (var i in list) {
		if (list[i][0] != ".") {
			finalList.push(list[i]);
		}
	}
	return finalList;
}

var exports = {};

function broadcast(from, message) {
	for (var taskName in tasks) {
		if (from != tasks[taskName]) {
			tasks[taskName].task.invoke(message);
		}
	}
}

function onMessage(from, message) {
	try {
		print("system onMessage: " + JSON.stringify(from) + ", " + JSON.stringify(message));
		if (message.to == "system") {
			var fromName = getTaskName(from);

			if (tasks[fromName] && tasks[fromName].manifest.trusted
				|| (!message.taskName && message.action == "get")) {
				if (message.action == "stopTask") {
					print("killing " + message.taskName);
					tasks[message.taskName].task.kill();
				} else if (message.action == "startTask") {
					startTask(message.taskName);
				} else if (message.action == "restartTask" && tasks[message.taskName]) {
					print("killing " + message.taskName);
					print(tasks[message.taskName]);
					tasks[message.taskName].task.kill();
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
				} else if (message.action == "newPackage") {
					var path = packageFilePath(message.taskName, "");
					if (path) {
						makeDirectory(path);
					}
					return path;
				} else if (message.action == "getPackageList") {
					print(getPackageList());
					return getPackageList();
				} else if (message.action == "getManifest") {
					return tasks[message.taskName].manifest;
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
			} else {
				print("PERMISSION DENIED");
			}
		} else if (message.to) {
			tasks[message.to].task.invoke(message);
		} else {
			broadcast(tasks[fromName], message);
		}
	} catch (error) {
		return "ERROR: " + error.message;
	}
}

start();
