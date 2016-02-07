"use strict";

//! {"permissions": ["administration"]}

terminal.print("Administration");
if (core.user.credentials.permissions &&
	core.user.credentials.permissions.administration) {
	core.register("onInput", onInput);
	terminal.print("Welcome, administrator.");
	terminal.print("Usage:");
	let kCommands = [
		[
			"set",
			 "List all global settings.",
		],
		[
			["set ", {class: "cyan", value: "key value"}],
			["Set global setting key to value."],
		],
		[
			"permission list",
			"List all permissions."
		],
		[
			["permission add ", {class: "cyan", value: "user action1 action2 ..."}],
			["Grant permission for ", {class: "cyan", value: "action1"}, ", ", {class: "cyan", value: "action2"}, ", ", {class: "cyan", value: "..."}, " to ", {class: "cyan", value: "user"}, "."],
		],
		[
			["permission remove ", {class: "cyan", value: "user action1 action2 ..."}],
			["Revoke permission for ", {class: "cyan", value: "action1"}, ", ", {class: "cyan", value: "action2"}, ", ", {class: "cyan", value: "..."}, " from ", {class: "cyan", value: "user"}, "."],
		],
	];
	for (var i = 0; i < kCommands.length; i++) {
		terminal.print({class: "yellow", value: kCommands[i][0]});
		terminal.print({style: "display: block; margin-left: 2em", value: kCommands[i][1]});
	}
} else {
	terminal.print("You are not an administrator.");
}

var kSimpleSettings = [
	'httpPort',
	'httpsPort',
	'index',
];

function printSettings(settings) {
	terminal.print("Current settings:");
	for (let i = 0; i < kSimpleSettings.length; i++) {
		terminal.print("  ", {class: "magenta", value: kSimpleSettings[i]}, " = ", {class: "yellow", value: settings[kSimpleSettings[i]]});
	}
}

function printPermissions(settings) {
	terminal.print("Current permissions:");
	let permissions = settings.permissions || {};
	for (let entry in permissions) {
		terminal.print("  ", {class: "magenta", value: entry}, ": ", {class: "yellow", value: permissions[entry].join(" ")});
	}
}

function onInput(input) {
	try {
		let match;
		if (input == "set") {
			administration.getGlobalSettings().then(printSettings);
		} else if (match = /^\s*set\s+(\w+)\s+(.*)/.exec(input)) {
			var key = match[1];
			var value = match[2];
			administration.getGlobalSettings().then(function(settings) {
				if (kSimpleSettings.indexOf(key) != -1) {
					settings[key] = value;
					administration.setGlobalSettings(settings).then(function() {
						administration.getGlobalSettings().then(printSettings);
					}).catch(function(error) {
						terminal.print("Error updating settings: " + JSON.stringify(error));
					});
				} else {
					terminal.print("Unknown setting: " + key);
				}
			});
		} else if (match = /^\s*permission\s+(\w+)(?:\s+(.*))?/.exec(input)) {
			var command = match[1];
			var remaining = (match[2] || "").split(/\s+/);
			if (command == "list") {
				administration.getGlobalSettings().then(printPermissions);
			} else if (command == "add") {
				var user = remaining[0];
				administration.getGlobalSettings().then(function(settings) {
					settings.permissions = settings.permissions || {};
					settings.permissions[user] = settings.permissions[user] || [];
					for (var i = 1; i < remaining.length; i++) {
						if (settings.permissions[user].indexOf(remaining[i]) == -1) {
							settings.permissions[user].push(remaining[i]);
						}
					}
					settings.permissions[user].sort();
					administration.setGlobalSettings(settings).then(function() {
						administration.getGlobalSettings().then(printPermissions);
					}).catch(function(error) {
						terminal.print("Error updating permissions: " + JSON.stringify(error));
					});
				});
			} else if (command == "remove") {
				var user = remaining[0];
				administration.getGlobalSettings().then(function(settings) {
					if (settings.permissions && settings.permissions[user]) {
						for (var i = 1; i < remaining.length; i++) {
							settings.permissions[user] = settings.permissions[user].filter(x => x != remaining[i]);
						}
						if (settings.permissions[user].length == 0) {
							delete settings.permissions[user];
						}
					}
					administration.setGlobalSettings(settings).then(function() {
						administration.getGlobalSettings().then(printPermissions);
					}).catch(function(error) {
						terminal.print("Error updating permissions: " + JSON.stringify(error));
					});
				});
			}
		} else {
			terminal.print("I didn't understand that.");
		}
	} catch (error) {
		terminal.print("error: " + error);
	}
}
