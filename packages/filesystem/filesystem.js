function normalize(path) {
	var parts = path.split(/[\/\\]+/);
	for (var i = parts.length - 1; i >= 0; i--) {
		if (parts[i] == ".") {
			parts.splice(i, 1);
		}
	}
	for (var i = 0; i < parts.length; i++) {
		if (parts[i] == ".." && i > 0 && parts[i - 1] != "..") {
			parts.splice(i - 1, 2);
			i -= 2;
		}
	}
	return parts.join("/");
}

function FileSystem(options) {
	var root = options.root || ".";
	var permissions = options.permissions || {};
	var fs = this;

	this.access = function(path) {
		var normalized = normalize(path);
		var result = {path: root + "/" + normalized, permissions: {}};
		if (normalized.charAt(0) != "." || normalized.charAt(1) != ".") {
			result.permissions = permissions;
		} else {
			throw new Error("Access denied for path: " + path);
		}
		return result;
	}

	this.readFile = function(fileName) {
		var access = fs.access(fileName);
		if (access.permissions.read) {
			return File.readFile(access.path);
		}
	}

	this.writeFile = function(fileName, contents) {
		var access = fs.access(fileName);
		if (access.permissions.write) {
			return File.writeFile(access.path, contents);
		}
	}

	this.renameFile = function(from, to) {
		var accessFrom = fs.access(from);
		var accessTo = fs.access(to);
		if (accessFrom.permissions.write && accessTo.permissions.write) {
			return File.renameFile(accessFrom.path, accessTo.path);
		}
	}

	this.unlinkFile = function(fileName) {
		var access = fs.access(fileName);
		if (access.permissions.write) {
			return File.unlinkFile(access.path);
		}
	}

	this.makeDirectory = function(path) {
		var access = fs.access(path);
		if (access.permissions.write) {
			return File.makeDirectory(access.path);
		}
	}

	this.listDirectory = function(path) {
		var access = fs.access(path);
		if (access.permissions.read) {
			var list = File.readDirectory(access.path);
			list.sort();
			var finalList = [];
			for (var i in list) {
				if (list[i][0] != ".") {
					finalList.push(list[i]);
				}
			}
			return finalList;
		}
	}

	this.chroot = function(path) {
		return new FileSystem({root: root + "/" + path, permissions: permissions});
	}

	this.ensureDirectoryTreeExists = function(path) {
		return fs.makeDirectory(path).catch(function(e) {
			if (e == -2) {
				// ENOENT
				return fs.ensureDirectoryTreeExists(path + "/..").then(function() { this.ensureDirectoryTreeExists(path); });
			} else if (e == -17) {
				// EEXIST
			}
		});
	}

	return this;
};

function isValidName(taskName) {
	return taskName && taskName.indexOf("..") == -1 && taskName.indexOf("/") == -1;
}

function makePackageDataFileSystem() {
	if (isValidName(this.taskName)) {
		return new FileSystem({root: "data/" + this.taskName, permissions: {read: true, write: true}});
	}
}

function makePackageFileSystem(packageName, permissions) {
	if (isValidName(this.taskName) && (!packageName || isValidName(packageName))) {
		if (this.taskName == "packager") {
			var granted = permissions || {read: true, write: true};
			return new FileSystem({root: "packages/" + (packageName || this.taskName), permissions: granted});
		} else {
			return new FileSystem({root: "packages/" + (packageName || this.taskName), permissions: {read: true}});
		}
	}
}

function writeToTarget(fileName, fs) {
	return function(data) {
		fs.writeFile(fileName, data);
	};
}

function copy(source, target) {
	return Promise.all([source.listDirectory("."), target.listDirectory(".")]).then(function(list) {
		var operations = [];
		var sourceFiles = list[0];
		var targetFiles = list[1];
		for (var i in sourceFiles) {
			operations.push(source.readFile(sourceFiles[i]).then(writeToTarget(sourceFiles[i], target)));
		}
		for (var i in targetFiles) {
			if (sourceFiles.indexOf(targetFiles[i]) == -1) {
				operations.push(target.unlinkFile(targetFiles[i]));
			}
		}
		return Promise.all(operations);
	});
}

exports = {
	getPackageData: makePackageDataFileSystem,
	getPackage: makePackageFileSystem,
	copy: copy,
};
