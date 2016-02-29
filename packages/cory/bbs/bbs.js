"use strict";
var gOnInput = null;

var kMaxHistory = 20;
var kShowHistory = 20;

var lastTimestamp = null;

if (imports.terminal) {
	core.register("onMessage", function(sender, message) {
		if (message.message && message.when) {
			printMessage(message, true);
		}
	});
	core.register("onSessionBegin", function(user) {
		if (user.packageName === core.user.packageName &&
			user.index !== core.user.index) {
			listUsers(user.name + " has joined the BBS.  ");
		}
	});
	core.register("onSessionEnd", function(user) {
		if (user.packageName === core.user.packageName &&
			user.index !== core.user.index) {
			listUsers(user.name + " has left the BBS.  ");
		}
	});
} else {
	// Chat service process.
	core.register("onMessage", function(sender, message) {
		if (message.message && message.when) {
			message.sender = sender;
			return database.get("board").catch(function() {
				return null;
			}).then(function(data) {
				try {
					data = JSON.parse(data);
				} catch(error) {
					data = [];
				}
				data.push(message);
				while (data.length > kMaxHistory) {
					data.shift();
				}
				return saveBoard(data);
			}).then(function() {
				return core.broadcast(message);
			});
		}
	});
}

function listUsers() {
	return core.getUsers(core.user.packageOwner, core.user.packageName).then(function(users) {
		terminal.select("users");
		terminal.clear();
		terminal.print("Users:");
		var counts = {};
		for (var i = 0; i < users.length; i++) {
			counts[users[i].name] = (counts[users[i].name] || 0) + 1;
		}
		var names = Object.keys(counts).sort();
		for (var i = 0; i < names.length; i++) {
			var name = names[i];
			var message = [];
			if (message.length > 1) {
				message.push(", ");
			}
			message.push({class: "orange", value: name});
			if (counts[name] > 1) {
				message.push({class: "base01", value: "(x" + counts[name] + ")"});
			}
			terminal.print(message);
		}
		terminal.select("terminal");
	});
}

function saveBoard(data) {
	return database.set("board", JSON.stringify(data)).catch(function(error) {
		if (error.message.indexOf("MDB_MAP_FULL") != -1) {
			data.shift();
			return saveBoard(data);
		} else {
			throw error;
		}
	});
}

core.register("onInput", function(input) {
	if (gOnInput && typeof input == "string") {
		gOnInput(input);
	}
});

function logo() {
	terminal.clear();
	terminal.print("");
	terminal.print("");
	terminal.print('Welcome to');
	terminal.print('   ______                _          ____  ____ _____');
	terminal.print('  / ____/___  _______  _( )_____   / __ )/ __ ) ___/');
	terminal.print(' / /   / __ \\/ ___/ / / /// ___/  / __  / __  \\__ \\ ');
	terminal.print('/ /___/ /_/ / /  / /_/ / (__  )  / /_/ / /_/ /__/ / ');
	terminal.print('\\____/\\____/_/   \\__, / /____/  /_____/_____/____/  ');
	terminal.print('                /____/                              ');
	terminal.print('                    yesterday\'s technology...today!');
	terminal.print("");
}

function welcome() {
	logo();
	chat();
}

function main() {
	terminal.clear();
	logo();
	terminal.print("");
	terminal.print("Main menu commands:");
	terminal.print("  ", {command: "chat"}, "       chat message board");
	terminal.print("  ", {command: "guess"}, "      guess the number game");
	terminal.print("  ", {command: "exit"}, "       back to that sweet logo");
	gOnInput = function(input) {
		input = input.toLowerCase();
		if (input == "chat") {
			chat();
		} else if (input == "guess") {
			guess();
		} else if (input == "exit") {
			terminal.print("Goodbye.");
			exit(0);
		} else {
			terminal.print("I didn't understand that: " + input);
			main();
		}
	};
}

function formatMessage(message) {
	var result;
	if (typeof message == "string") {
		result = [];
		var regex = /(\w+:\/*\S+?)(?=(?:[\.!?])?(?:$|\s))/gi;
		var match;
		var lastIndex = 0;
		while ((match = regex.exec(message)) !== null) {
			result.push({class: "base1", value: message.substring(lastIndex, match.index)});
			result.push({href: match[0]});
			lastIndex = regex.lastIndex;
		}
		result.push({class: "base1", value: message.substring(lastIndex)});
	} else {
		result = message;
	}
	return result;
}

function niceTime(lastTime, thisTime) {
	if (!lastTime) {
		return thisTime;
	}
	let result = [];
	let lastParts = lastTime.split(" ");
	let thisParts = thisTime.split(" ");
	for (let i = 0; i < thisParts.length; i++) {
		if (thisParts[i] !== lastParts[i]) {
			result.push(thisParts[i]);
		}
	}
	return result.join(" ");
}

function printMessage(message, notify) {
	terminal.print(
		{class: "base0", value: niceTime(lastTimestamp, message.when)},
		" ",
		{class: "base00", value: "<"},
		{class: "base3", value: (message.sender ? message.sender.name : "unknown")},
		{class: "base00", value: ">"},
		" ",
		formatMessage(message.message));
	lastTimestamp = message.when;
	if (notify) {
		return core.getUser().then(function(user) {
			if (message.message.indexOf("!") != -1) {
				return terminal.notify("SOMEONE IS SHOUTING!", {body: "<" + (message.sender ? message.sender.name : "unknown") + "> " + message.message});
			} else if (message.message.indexOf(user.name + ":") != -1) {
				return terminal.notify("Someone is talking at you.", {body: "<" + (message.sender ? message.sender.name : "unknown") + "> " + message.message});
			}
		});
	}
}

function chat() {
	terminal.setEcho(false);
	terminal.print("");
	terminal.print("You are now in a chat.  Anything you type will be broadcast to everyone else connected.  To leave, say ", {command: "exit"}, ".");
	listUsers();
	database.get("board").catch(function() {
		return null;
	}).then(function(board) {
		try {
			board = JSON.parse(board);
		} catch (error) {
			board = [];
		}

		for (let i = Math.max(0, board.length - kShowHistory); i < board.length; i++) {
			printMessage(board[i], false);
		}
	});
	gOnInput = function(input) {
		if (input == "exit") {
			terminal.setEcho(true);
			main();
		} else {
			core.getService("chat").then(function(chatService) {
				return chatService.postMessage({when: new Date().toString(), message: input});
			}).catch(function(error) {
				terminal.print("ERROR: " + JSON.stringify(error));
			});
		}
	};
}

function guess() {
	terminal.clear();
	var number = Math.round(Math.random() * 100);
	var guesses = 0;
	terminal.print("OK, I have a number in mind.  What do you think it is?  Use ", {command: "exit"}, " to stop.");
	gOnInput = function(input) {
		if (input == "exit") {
			main();
		} else {
			var guess = parseInt(input);
			guesses++;
			if (input != guess.toString()) {
				terminal.print("I'm not sure that's an integer.  Please guess only integers.");
			} else {
				if (guess < number) {
					terminal.print("Too low.");
				} else if (guess > number) {
					terminal.print("Too high.");
				} else if (guess == number) {
					terminal.print("Wow, you got it in " + guesses + " guesses!  It was " + number + ".");
					guessEnd(guesses);
				}
			}
		}
	};
}

function guessEnd(guesses) {
	terminal.print("What's your name, for the high score table?");
	gOnInput = function(name) {
		var entry = {'guesses': guesses, 'name': name, 'when': new Date().toString()};
		database.get("guessHighScores").then(function(data) {
			data = JSON.parse(data);
			var index = data.length;
			for (var i in data) {
				if (guesses < data[i].guesses) {
					index = i;
					break;
				}
			}
			data.splice(index, 0, entry);
			printHighScores(data);
			database.set("guessHighScores", JSON.stringify(data));
			gOnInput = function() {
				main();
			};
		}).catch(function() {
			var data = [entry];
			printHighScores(data);
			database.set("guessHighScores", JSON.stringify(data));
			main();
		});
	};
}

function printTable(data) {
	var widths = [];
	for (var i in data) {
		var row = data[i];
		for (var c in row) {
			widths[c] = Math.max(widths[c] || 0, row[c].length);
		}
	}

	for (var i in data) {
		var row = data[i];
		var line = "";
		for (var c in row) {
			line += row[c];
			line += " ".repeat(widths[c] - row[c].length + 2);
		}
		terminal.print(line);
	}
}

function printHighScores(data) {
	printTable([["Name", "Guesses", "Date"]].concat(data.map(function(entry) {
		return [entry.name, entry.guesses.toString(), entry.when];
	})));
}

if (imports.terminal) {
	terminal.split([
		{type: "horizontal", children: [
			{name: "terminal", grow: 1},
			{name: "users", grow: 0},
		]},
	]);
	welcome();
}
