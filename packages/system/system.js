var tasks = {};
var gStatusWatchers = [];

function start() {
	for (var i in tasks) {
		tasks[i].task.kill();
		delete tasks[i];
	}
	var packages = getPackageListInternal();
	for (var i in packages) {
		if (packages[i] != "system") {
			startTaskInternal(packages[i]);
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
			startTaskInternal(i);
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

function startTaskInternal(packageName) {
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

function startTask(packageName) {
	if (taskIsTrusted(this.taskName)) {
	} else {
		throw new Error("Permission denied.");
	}
}

function stopTask(taskName) {
	if (taskIsTrusted(this.taskName)) {
		print("killing " + taskName);
		tasks[taskName].task.kill();
		delete tasks[taskName];
		notifyTaskStatusChanged(taskName, "stopped");
	} else {
		throw new Error("Permission denied.");
	}
}

function restartTask(taskName) {
	if (taskIsTrusted(this.taskName)) {
		print("killing " + taskName);
		var previousOnExit = tasks[taskName].task.onExit;
		tasks[taskName].task.onExit = function() {
			previousOnExit();
			startTask(taskName);
		}
		tasks[taskName].task.kill();
	} else {
		throw new Error("Permission denied.");
	}
}

function packageFilePath(packageName, fileName) {
	if (packageName.indexOf("..") != -1 && fileName.indexOf(".." != -1)) {
		return null;
	} else {
		return 'packages/' + packageName + '/' + fileName;
	}
}

function getPackageListInternal() {
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

function getPackageList() {
	if (taskIsTrusted(this.taskName)) {
		return getPackageListInternal();
	} else {
		throw new Error("Permission denied.");
	}
}

function getTasks() {
	if (taskIsTrusted(this.taskName)) {
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
	} else {
		throw new Error("Permission denied.");
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

function taskIsTrusted(taskName) {
	return tasks[taskName] && tasks[taskName].manifest.trusted;
}

function accessRead(taskName, packageName) {
	return !packageName
		|| packageName == taskName
		|| tasks[taskName] && tasks[taskName].manifest.trusted;
}

function accessWrite(taskName, packageName) {
	return taskIsTrusted(taskName);
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
	if (accessRead(this.taskName, packageName)) {
		var finalPath = packageFilePath(packageName || this.taskName, fileName);
		if (finalPath) {
			return readFile(finalPath);
		}
	} else {
		throw new Error("Permission denied.");
	}
}

function putPackageFile(fileName, contents, packageName) {
	if (accessWrite(this.taskName, packageName)) {
		var finalPath = packageFilePath(packageName || this.taskName, fileName);
		if (finalPath) {
			return writeFile(finalPath, contents);
		}
	} else {
		throw new Error("Permission denied.");
	}
}

function renamePackageFile(fromName, toName, packageName) {
	if (accessWrite(this.taskName, packageName)) {
		var oldName = packageFilePath(packageName || this.taskName, fromName);
		var newName = packageFilePath(packageName || this.taskName, toName);
		if (oldName && newName) {
			return renameFile(oldName, newName);
		}
	} else {
		throw new Error("Permission denied.");
	}
}

function unlinkPackageFile(fileName, packageName) {
	if (accessWrite(this.taskName, packageName)) {
		var finalName = packageFilePath(packageName || this.taskName, fileName);
		if (finalName) {
			return unlinkFile(finalName);
		}
	} else {
		throw new Error("Permission denied.");
	}
}

function createPackage(packageName) {
	if (accessWrite(this.taskName, packageName)) {
		var path = packageFilePath(packageName, "");
		if (path) {
			makeDirectory(path);
			return path;
		}
	} else {
		throw new Error("Permission denied.");
	}
}

function listPackageFiles(packageName) {
	if (accessRead(this.taskName, packageName)) {
		var list = readDirectory(packageFilePath(packageName || this.taskName, ""));
		list.sort();
		var finalList = [];
		for (var i in list) {
			if (list[i][0] != ".") {
				finalList.push(list[i]);
			}
		}
		return finalList;
	} else {
		throw new Error("Permission denied.");
	}
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
