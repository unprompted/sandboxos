"use strict";

core.register("onSessionBegin", index);
core.register("onSessionEnd", index);

function index() {
	Promise.all([core.getPackages(), core.getUsers()]).then(function(values) {
		let packages = values[0];
		let users = values[1];
		let usersByApp = {};
		for (let i in users) {
			let user = users[i];
			if (!usersByApp["/~" + user.packageOwner + "/" + user.packageName]) {
				usersByApp["/~" + user.packageOwner + "/" + user.packageName] = [];
			}
			usersByApp["/~" + user.packageOwner + "/" + user.packageName].push(user.name);
		}

		terminal.clear();
		terminal.print("Available applications [active users]:");
		packages.sort(function(x, y) {
			return Math.sign(x.owner.localeCompare(y.owner)) * 10 + Math.sign(x.name.localeCompare(y.name)) * 1;
		}).forEach(function(app) {
			let users = usersByApp["/~" + app.owner + "/" + app.name];
			let message = [];
			if (users) {
				message.push(" [");
				let counts = {};
				for (let i = 0; i < users.length; i++) {
					counts[users[i]] = (counts[users[i]] || 0) + 1;
				}
				let names = Object.keys(counts).sort();
				for (let i = 0; i < names.length; i++) {
					var name = names[i];
					if (message.length > 1) {
						message.push(", ");
					}
					message.push({class: "orange", value: name});
					if (counts[name] > 1) {
						message.push({class: "base01", value: "(x" + counts[name] + ")"});
					}
				}
				message.push("]");
			}
			terminal.print(
				"* ",
				{href: "/~" + app.owner + "/" + app.name},
				message);
		});
	});
}

index();
