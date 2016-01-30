core.register("onSessionBegin", index);
core.register("onSessionEnd", index);
function index() {
	Promise.all([core.getPackages(), core.getUsers()]).then(function(values) {
		var packages = values[0];
		var users = values[1];
		var usersByApp = {};
		for (var i in users) {
			var user = users[i];
			if (!usersByApp["/~" + user.packageOwner + "/" + user.packageName]) {
				usersByApp["/~" + user.packageOwner + "/" + user.packageName] = [];
			}
			usersByApp["/~" + user.packageOwner + "/" + user.packageName].push(user.name);
		}

		terminal.clear();
		terminal.print("Available applications [active users]:");
		packages.sort().forEach(function(package) {
			var users = usersByApp["/~" + package.owner + "/" + package.name];
			terminal.print(
				"* ",
				{href: "/~" + package.owner + "/" + package.name},
				users ? " [" + users.sort().join(", ") + "]" : "");
		});
	});
}

index();
