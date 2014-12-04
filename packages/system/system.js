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
			result[imports[i].name] = Object.create(imports[i].imports);
		}
		if (wantSystem) {
			result["system"] = Object.create(exports);
		}
		for (var i in result) {
			for (var j in result[i]) {
				result[i][j] = result[i][j].bind({taskName: task.manifest.name});
			}
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
			task.task.packageName = packageName;
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

function stopTask(taskName) {
	print("killing " + taskName);
	tasks[taskName].task.kill();
	delete tasks[taskName];
	notifyTaskStatusChanged(taskName, "stopped");
}

function restartTask(taskName) {
	print("killing " + taskName);
	var previousOnExit = tasks[taskName].task.onExit;
	tasks[taskName].task.onExit = function() {
		previousOnExit();
		startTask(taskName);
	}
	tasks[taskName].task.kill();
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

function getTasks() {
	var taskNames = [];
	var promises = [];
	for (var i in tasks) {
		if (tasks[i].task.statistics) {
			taskNames.push(i);
			promises.push(tasks[i].task.statistics());
		}
	}
	return Promise.all(promises).then(function(statistics) {
		var result = {};
		for (var i in taskNames) {
			result[taskNames[i]] = {statistics: statistics[i], manifest: tasks[taskNames[i]].manifest};
		}
		return result;
	});
}

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

function getData(fileName) {
	var finalPath = packageFilePath(this.taskName, "data/" + fileName);
	if (finalPath) {
		return readFile(finalPath);
	}
}

function putData(fileName, contents) {
	makeDirectory(packageFilePath(this.taskName, "data"));
	var finalPath = packageFilePath(this.taskName, "data/" + fileName);
	if (finalPath) {
		return writeFile(finalPath, contents);
	}
}

function getPackageFile(fileName, packageName) {
	var finalPath = packageFilePath(packageName || this.taskName, fileName);
	if (finalPath) {
		return readFile(finalPath);
	}
}

function putPackageFile(fileName, contents, packageName) {
	var finalPath = packageFilePath(packageName || this.taskName, fileName);
	if (finalPath) {
		return writeFile(finalPath, contents);
	}
}

function renamePackageFile(fromName, toName, packageName) {
	var oldName = packageFilePath(packageName || this.taskName, fromName);
	var newName = packageFilePath(packageName || this.taskName, toName);
	if (oldName && newName) {
		return renameFile(oldName, newName);
	}
}

function unlinkPackageFile(fileName, packageName) {
	var finalName = packageFilePath(packageName || this.taskName, fileName);
	if (finalName) {
		return unlinkFile(finalName);
	}
}

function createPackage(packageName) {
	var path = packageFilePath(packageName, "");
	if (path) {
		makeDirectory(path);
		return path;
	}
}

function listPackageFiles(packageName) {
	var list = readDirectory(packageFilePath(packageName || this.taskName, ""));
	list.sort();
	var finalList = [];
	for (var i in list) {
		if (list[i][0] != ".") {
			finalList.push(list[i]);
		}
	}
	return finalList;
}

function registerTaskStatusChanged(callback) {
	gStatusWatchers.push(callback);
}

start();

exports = {
	registerTaskStatusChanged: registerTaskStatusChanged,
	getData: getData,
	putData: putData,
	getPackageFile: getPackageFile,
	putPackageFile: putPackageFile,
	renamePackageFile: renamePackageFile,
	unlinkPackageFile: unlinkPackageFile,
	createPackage: createPackage,
	listPackageFiles: listPackageFiles,
	getPackageList: getPackageList,
	getTasks: getTasks,
};
