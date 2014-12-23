function installAllowed(oldManifest, newManifest, user, permissions) {
	return (!oldManifest || !oldManifest.trusted) && !newManifest.trusted
		|| permissions.administrator;
}

function install(fs, credentials) {
	return imports.auth.verifyCredentials(credentials).then(function(user) {
		return fs.readFile("package.json").then(function(text) {
			var manifest = JSON.parse(text);
			var packageName = manifest.name;

			return imports.filesystem.getPackage(packageName, {read: true, write: true}).then(function(packageFs) {
				var oldManifestPromise = packageFs.readFile("package.json").then(function(oldText) {
					return JSON.parse(oldText);
				}).catch(function(e) {
					return null;
				});

				return oldManifestPromise.then(function(oldManifest) {
					if (!installAllowed(oldManifest, manifest, credentials.user, user.permissions)) {
						throw new Error("Permission denied to install.");
					}
					return packageFs.ensureDirectoryTreeExists(".").then(function() {
						return imports.filesystem.copy(fs, packageFs);
					});
				});
			});
		});
	});
}

function read(packageName) {
	return imports.filesystem.getPackage(packageName, {read: true, write: false});
}

function restartTask(packageName, credentials) {
	return imports.auth.verifyCredentials(credentials).then(function(user) {
		return imports.system.restartTask(packageName);
	});
}

exports = {
	install: install,
	read: read,
	restartTask: restartTask,
};
