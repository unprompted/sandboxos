var tasks = {};
var gStatusWatchers = [];

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

function importsReady(task) {
	var ready = true;
	if (task.manifest.imports) {
		for (var i in task.manifest.imports) {
			if (!tasks[task.manifest.imports[i]]
			 || !tasks[task.manifest.imports[i]].task
			 || !tasks[task.manifest.imports[i]].started) {
				if (task.manifest.imports[i] != "system") {
					ready = false;
				}
			}
		}
	}
	return ready;
}

function updatePendingTasks() {
	for (var i in tasks) {
		if (tasks[i].pending && importsReady(tasks[i])) {
			startTask(i);
		}
	}
}

function nameExports(name) {
	return function(exports) {
		return {name: name, imports: exports};
	};
}

function gatherImports(task) {
	var promises = [];
	var wantSystem = false;
	if (task.manifest.imports) {
		for (var i in task.manifest.imports) {
			var name = task.manifest.imports[i];
			if (name == "system") {
				wantSystem = true;
			} else {
				var other = tasks[name];
				promises.push(other.task.getExports().then(nameExports(name)));
			}
		}
	}
	return Promise.all(promises).then(function(imports) {
		var result = {};
		for (var i in imports) {
			result[imports[i].name] = imports[i].imports;
		}
		if (wantSystem) {
			result["system"] = exports;
		}
		return task.task.setImports(result);
	});
}

function updateDependentTasks(taskName) {
	for (var i in tasks) {
		var dependent = false;
		for (var j in tasks[i].manifest.imports) {
			if (taskName == tasks[i].manifest.imports[j]) {
				dependent = true;
			}
		}
		if (dependent && tasks[i].started) {
			gatherImports(tasks[i]);
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
		task.manifest = manifest;
		task.pending = !importsReady(task);
		tasks[packageName] = task

		if (!task.pending) {
			notifyTaskStatusChanged(packageName, "starting");
			task.task = new Task();
			task.task.trusted = true;
			task.task.onExit = function(exitCode, terminationSignal) {
				if (terminationSignal) {
					print("Task " + packageName + " terminated with signal " + terminationSignal + ".");
				} else {
					print("Task " + packageName + " returned " + exitCode + ".");
				}
				delete tasks[packageName];
				notifyTaskStatusChanged(packageName, "stopped");
			};
			task.task.activate();
			gatherImports(task).then(function() {
				task.task.execute(packageFilePath(packageName, manifest.start)).then(function() {
					task.started = true;
					notifyTaskStatusChanged(packageName, "started");
					updateDependentTasks(packageName);
					updatePendingTasks();
				});
			});
		}
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
		if (tasks[taskName].task == task) {
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
		if (from != tasks[taskName] && tasks[taskName].task) {
			tasks[taskName].task.invoke(message);
		}
	}
}

function notifyTaskStatusChanged(taskName, taskStatus) {
	for (var i in gStatusWatchers) {
		if (gStatusWatchers[i]) {
			try {
				gStatusWatchers[i](taskName, taskStatus);
			} catch (e) {
				gStatusWatchers[i] = null;
			}
		}
	}
}

function onMessage(from, message) {
	try {
		print("system onMessage: " + JSON.stringify(from) + ", " + JSON.stringify(message));
		if (message.to == "system") {
			var fromName = getTaskName(from);

			if (message.action == "getData") {
				var fileName = packageFilePath(fromName, "data/" + message.fileName);
				if (fileName) {
					return readFile(fileName);
				}
			} else if (message.action == "putData") {
				makeDirectory(packageFilePath(fromName, "data"));
				var fileName = packageFilePath(fromName, "data/" + message.fileName);
				if (fileName) {
					print("writeFile(" + fileName + ") => " + writeFile(fileName, message.contents));
				}
			// some task management stuff
			} else if (tasks[fromName] && tasks[fromName].manifest.trusted
				|| (!message.taskName && message.action == "get")) {
				if (message.action == "stopTask") {
					print("killing " + message.taskName);
					tasks[message.taskName].task.kill();
					delete tasks[message.taskName];
					notifyTaskStatusChanged(message.taskName, "stopped");
				} else if (message.action == "startTask") {
					startTask(message.taskName);
				} else if (message.action == "restartTask" && tasks[message.taskName]) {
					print("killing " + message.taskName);
					print(tasks[message.taskName]);
					var previousOnExit = tasks[message.taskName].task.onExit;
					tasks[message.taskName].task.onExit = function() {
						previousOnExit();
						startTask(message.taskName);
					}
					tasks[message.taskName].task.kill();
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
				} else if (message.action == "rename") {
					var oldName = packageFilePath(message.taskName || fromName, message.fileName);
					var newName = packageFilePath(message.taskName || fromName, message.newName);
					if (oldName && newName) {
						return renameFile(oldName, newName);
					}
				} else if (message.action == "unlink") {
					var fileName = packageFilePath(message.taskName || fromName, message.fileName);
					if (fileName) {
						return unlinkFile(fileName);
					}
				} else if (message.action == "newPackage") {
					var path = packageFilePath(message.taskName, "");
					if (path) {
						makeDirectory(path);
					}
					return path;
				} else if (message.action == "getPackageList") {
					return getPackageList();
				} else if (message.action == "getTasks") {
					var taskNames = [];
					var promises = [];
					for (var i in tasks) {
						if (tasks[i].task.statistics) {
							taskNames.push(i);
							promises.push(tasks[i].task.statistics());
						}
					}
					return Promise.all(promises).then(function(statistics) {
						print("STATISTICS: " + JSON.stringify(statistics));
						var result = {};
						for (var i in taskNames) {
							result[taskNames[i]] = {statistics: statistics[i], manifest: tasks[taskNames[i]].manifest};
						}
						return result;
					});
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
				print("PERMISSION DENIED for: " + fromName);
			}
		} else if (message.to) {
			return tasks[message.to].task.invoke(message);
		} else {
			broadcast(tasks[fromName], message);
		}
	} catch (error) {
		return "ERROR: " + error.message;
	}
}

function registerTaskStatusChanged(callback) {
	gStatusWatchers.push(callback);
}

start();

exports = {
	registerTaskStatusChanged: registerTaskStatusChanged,
};
