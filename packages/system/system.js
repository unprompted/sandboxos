var tasks = {};
var gStatusWatchers = {};

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
		manifest = JSON.parse(File.readFile(packageFilePath(packageName, "package.json")));
	} catch (error) {
		print(error);
	}

	return new Promise(function(resolve, reject) {
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
						resolve();
					}).catch(function(error) {
						task.error = error;
						notifyTaskStatusChanged(packageName, "error");
						reject(error);
					});
				});
			}
		} else {
			reject(new Error("Package " + packageName + " has no package.json."));
		}
	});
}

function startTask(packageName) {
	if (taskIsTrusted(this.taskName)) {
		startTaskInternal(packageName);
	} else {
		throw new Error("Permission denied to start task " + packageName + " from " + this.taskName + ".");
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
	var originatingTask = this;
	return new Promise(function(resolve, reject) {
		if (taskIsTrusted(originatingTask.taskName)) {
			print("killing " + taskName);
			var previousOnExit = tasks[taskName].task.onExit;
			tasks[taskName].task.onExit = function() {
				previousOnExit();
				startTaskInternal(taskName).then(resolve, reject);
			}
			tasks[taskName].task.kill();
		} else {
			throw new Error("Permission denied.");
		}
	});
}

function packageFilePath(packageName, fileName) {
	if (packageName.indexOf("..") != -1 && fileName.indexOf("..") != -1) {
		return null;
	} else {
		return 'packages/' + packageName + '/' + fileName;
	}
}

function getPackageListInternal() {
	var list = File.readDirectory("packages/");
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
			if (tasks[i].task && tasks[i].task.statistics) {
				taskNames.push(i);
				promises.push(tasks[i].task.statistics());
			}
		}
		return Promise.all(promises).then(function(statistics) {
			var result = {};
			for (var i in taskNames) {
				result[taskNames[i]] = {
					statistics: statistics[i],
					manifest: tasks[taskNames[i]].manifest,
					status: tasks[taskNames[i]].status,
					error: tasks[taskNames[i]].error,
				};
			}
			return result;
		});
	} else {
		throw new Error("Permission denied.");
	}
}

function notifyTaskStatusChanged(taskName, taskStatus) {
	if (tasks[taskName]) {
		tasks[taskName].status = taskStatus;
	}
	for (var i in gStatusWatchers) {
		if (gStatusWatchers[i]) {
			try {
				gStatusWatchers[i](taskName, taskStatus);
			} catch (e) {
				delete gStatusWatchers[i];
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

function registerTaskStatusChanged(callback) {
	gStatusWatchers[this.taskName] = callback;
}

start();

exports = {
	registerTaskStatusChanged: registerTaskStatusChanged,
	getPackageList: getPackageList,
	getTasks: getTasks,
	startTask: startTask,
	stopTask: stopTask,
	restartTask: restartTask,
};
