"use strict";
var gOnInput = null;

var kMaxHistory = 20;
var kShowHistory = 20;

if (imports.terminal) {
	imports.core.register("onMessage", function(sender, message) {
		if (message.message && message.when) {
			printMessage(message, true);
		}
	});
	imports.core.register("onSessionBegin", function(user) {
		if (user.packageName === imports.core.user.packageName &&
		   user.index !== imports.core.user.index) {
			imports.terminal.print(user.name + " has joined the BBS.");
		}
	});
	imports.core.register("onSessionEnd", function(user) {
		if (user.packageName === imports.core.user.packageName &&
		   user.index !== imports.core.user.index) {
			imports.terminal.print(user.name + " has left the BBS.");
		}
	});
} else {
	// Chat service process.
	imports.core.register("onMessage", function(sender, message) {
		if (message.message && message.when) {
			message.sender = sender;
			return imports.database.get("board").catch(function() {
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
				return imports.core.broadcast(message);
			});
		}
	});
}

function saveBoard(data) {
	return imports.database.set("board", JSON.stringify(data)).catch(function(error) {
		if (error.message.indexOf("MDB_MAP_FULL") != -1) {
			data.shift();
			return saveBoard(data);
		} else {
			throw error;
		}
	});
}

imports.core.register("onInput", function(input) {
	if (gOnInput) {
		gOnInput(input);
	}
});

function welcome() {
	imports.terminal.clear();
	imports.terminal.print("");
	imports.terminal.print("");
	imports.terminal.print('Welcome to');
	imports.terminal.print('   ______                _          ____  ____ _____');
	imports.terminal.print('  / ____/___  _______  _( )_____   / __ )/ __ ) ___/');
	imports.terminal.print(' / /   / __ \\/ ___/ / / /// ___/  / __  / __  \\__ \\ ');
	imports.terminal.print('/ /___/ /_/ / /  / /_/ / (__  )  / /_/ / /_/ /__/ / ');
	imports.terminal.print('\\____/\\____/_/   \\__, / /____/  /_____/_____/____/  ');
	imports.terminal.print('                /____/                              ');
	imports.terminal.print('                    yesterday\'s technology...today!');
	imports.terminal.print("");
	imports.terminal.print("Press ", {command: "enter"}, " to continue.");
	gOnInput = function(input) {
		main();
	};
}

function main() {
	imports.terminal.clear();
	imports.terminal.print("");
	imports.terminal.print("Main menu commands:");
	imports.terminal.print("  ", {command: "chat"}, "       chat message board");
	imports.terminal.print("  ", {command: "guess"}, "      guess the number game");
	imports.terminal.print("  ", {command: "exit"}, "       back to that sweet logo");
	gOnInput = function(input) {
		input = input.toLowerCase();
		if (input == "chat") {
			chat();
		} else if (input == "guess") {
			guess();
		} else if (input == "exit") {
			imports.terminal.print("Goodbye.");
			exit(0);
		} else {
			imports.terminal.print("I didn't understand that: " + input);
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

function printMessage(message, notify) {
	imports.terminal.print(
		{class: "base0", value: message.when},
		" ",
		{class: "base00", value: "<"},
		{class: "base3", value: (message.sender ? message.sender.name : "unknown")},
		{class: "base00", value: ">"},
		" ",
		formatMessage(message.message));
	if (notify) {
		return imports.core.getUser().then(function(user) {
			if (message.message.indexOf("!") != -1) {
				return imports.terminal.notify("SOMEONE IS SHOUTING!", {body: "<" + (message.sender ? message.sender.name : "unknown") + "> " + message.message});
			} else if (message.message.indexOf(user.name + ":") != -1) {
				return imports.terminal.notify("Someone is talking at you.", {body: "<" + (message.sender ? message.sender.name : "unknown") + "> " + message.message});
			}
		});
	}
}

function chat() {
	imports.terminal.clear();
	imports.terminal.setEcho(false);
	imports.terminal.print("");
	imports.terminal.print("You are now in a chat.  Anything you type will be broadcast to everyone else connected.  To leave, say ", {command: "exit"}, ".");
	imports.database.get("board").catch(function() {
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
			imports.terminal.setEcho(true);
			main();
		} else {
			imports.core.getService("chat").then(function(chatService) {
				return chatService.postMessage({when: new Date().toString(), message: input});
			}).catch(function(error) {
				imports.terminal.print("ERROR: " + JSON.stringify(error));
			});
		}
	};
}

function guess() {
	imports.terminal.clear();
	var number = Math.round(Math.random() * 100);
	var guesses = 0;
	imports.terminal.print("OK, I have a number in mind.  What do you think it is?  Use ", {command: "exit"}, " to stop.");
	gOnInput = function(input) {
		if (input == "exit") {
			main();
		} else {
			var guess = parseInt(input);
			guesses++;
			if (input != guess.toString()) {
				imports.terminal.print("I'm not sure that's an integer.  Please guess only integers.");
			} else {
				if (guess < number) {
					imports.terminal.print("Too low.");
				} else if (guess > number) {
					imports.terminal.print("Too high.");
				} else if (guess == number) {
					imports.terminal.print("Wow, you got it in " + guesses + " guesses!  It was " + number + ".");
					guessEnd(guesses);
				}
			}
		}
	};
}

function guessEnd(guesses) {
	imports.terminal.print("What's your name, for the high score table?");
	gOnInput = function(name) {
		var entry = {'guesses': guesses, 'name': name, 'when': new Date().toString()};
		imports.database.get("guessHighScores").then(function(data) {
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
			imports.database.set("guessHighScores", JSON.stringify(data));
			gOnInput = function() {
				main();
			};
		}).catch(function() {
			var data = [entry];
			printHighScores(data);
			imports.database.set("guessHighScores", JSON.stringify(data));
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
		imports.terminal.print(line);
	}
}

function printHighScores(data) {
	printTable([["Name", "Guesses", "Date"]].concat(data.map(function(entry) {
		return [entry.name, entry.guesses.toString(), entry.when];
	})));
}

if (imports.terminal) {
	welcome();
}
	