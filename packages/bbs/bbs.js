"use strict";
var gOnInput = null;

imports.terminal.register("onMessage", function(message) {
	if (message.chat) {
		imports.terminal.print("Incoming message: " + message.chat);
	} else if (message.message && message.when) {
		imports.database.get("board").catch(function() {
			return null;
		}).then(function(data) {
			try {
				data = JSON.parse(data);
			} catch(e) {
				data = [];
			}
			data.push(message);
			return imports.database.set("board", JSON.stringify(data));
		});
	}
});

imports.terminal.register("onInput", function(input) {
	if (gOnInput) {
		gOnInput(input);
	}
});

function welcome() {
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
	imports.terminal.print("");
	main();
}

function main() {
	imports.terminal.print("");
	imports.terminal.print("Main menu commands:");
	imports.terminal.print(["  ", {command: "chat"}, "       enter the group chat"]);
	imports.terminal.print(["  ", {command: "board"}, "      message board test thing"]);
	imports.terminal.print(["  ", {command: "guess"}, "      guess the number game"]);
	imports.terminal.print(["  ", {command: "exit"}, "       end the current session (and start a new one)"]);
	gOnInput = function(input) {
		input = input.toLowerCase();
		if (input == "chat") {
			chat();
		} else if (input == "board") {
			board();
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

function chat() {
	imports.terminal.print("");
	imports.terminal.print(["You are now in a chat.  Anything you type will be broadcast to everyone else connected.  To leave, say ", {command: "exit"}, "."]);
	gOnInput = function(input) {
		if (input == "exit") {
			main();
		} else {
			imports.core.broadcast({chat: input});
		}
	};
}

function board() {
	imports.terminal.print("");
	imports.terminal.print(["Message board (", {command: "exit"}, " to exit):"]);
	
	imports.database.get("board").catch(function() {
		return JSON.stringify([]);
	}).then(function(board) {
		board = JSON.parse(board);
		printTable([["When", "Message"]].concat(board.slice(Math.max(0, board.length - 10), board.length).map(function(entry) {
			return [entry.when, entry.message];
		})));
	});	
	gOnInput = function(input) {
		if (input == "exit") {
			main();
		} else {
			imports.core.sendToLeader({when: new Date().toString(), message: input}).then(board);
		}
	};
}

function databaseTest() {
	imports.terminal.print(["Database test commands: ", {command: "get"}, {command: "set"}, {command: "remove"}, {command: "getAll"}, {command: "exit"}]);
	gOnInput = function(input) {
		var parts = input.split(' ');
		if (parts[0] == "get") {
			imports.database.get(parts[1]).then(function(value) {
				imports.terminal.print(parts[0] + " => " + value);
			}).catch(function(error) {
				imports.terminal.print(error);
			});
		} else if (parts[0] == "set") {
			imports.database.set(parts[1], parts[2]).then(function() {
				imports.terminal.print("set");
			}).catch(function(error) {
				imports.terminal.print(error);
			});
		} else if (parts[0] == "remove") {
			imports.database.remove(parts[1]).then(function(value) {
				imports.terminal.print(parts[0] + " removed");
			}).catch(function(error) {
				imports.terminal.print(error);
			});
		} else if (parts[0] == "getAll") {
			imports.database.getAll().then(function(data) {
				imports.terminal.print(data.join(", "));
			}).catch(function(error) {
				imports.terminal.print(error);
			});
		} else if (parts[0] == "exit") {
			main();
		} else {
			imports.terminal.print("I didn't get that.");
		}
	}
}

function guess() {
	var number = Math.round(Math.random() * 100);
	var guesses = 0;
	imports.terminal.print(["OK, I have a number in mind.  What do you think it is?  Use ", {command: "exit"}, " to stop."]);
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
			main();
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

welcome();
