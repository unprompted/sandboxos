imports.core.register("onSessionBegin", index);
imports.core.register("onSessionEnd", index);

function index() {
	Promise.all([imports.core.getPackages(), imports.core.getUsers()]).then(function(values) {
		var packages = values[0];
		var users = values[1];
		var usersByApp = {};
		for (var i in users) {
			var user = users[i];
			if (!usersByApp[user.packageName]) {
				usersByApp[user.packageName] = [];
			}
			usersByApp[user.packageName].push(user.index.toString());
		}

		imports.terminal.clear();
		imports.terminal.print("Available applications [active users]:");
		packages.sort().forEach(function(name) {
			imports.terminal.print("* ", {href: "/" + name, value: name}, usersByApp[name] ? " [" + usersByApp[name].map(x => "user" + x).join(", ") + "]" : "");
		});
	});
}

index();
