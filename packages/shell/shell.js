"use strict";
var kBuiltin = {};
var gRegistered = {};
var gStartTime = new Date();

kBuiltin.hello = function(terminal) {
	var commands = [];
	var i;
	for (i in kBuiltin) {
		commands.push(i);
	}
	for (i in gRegistered) {
		commands.push(i);
	}
	commands.sort();
	terminal.print("Welcome. Available commands: " + commands.join(", "));
};

kBuiltin.clear = function(terminal) {
	terminal.clear();
};

kBuiltin.w = function(terminal) {
	terminal.who().then(function(terminals) {
		for (var i in terminals) {
			terminal.print(i + "\t" + JSON.stringify(terminals[i]));
		}
	});
};

kBuiltin.send = function(terminal, argv) {
	terminal.send(argv[1], argv.slice(2).join(" "));
};

kBuiltin.date = function(terminal) {
	terminal.print(new Date().toString());
};

kBuiltin.echo = function(terminal, args) {
	terminal.print(args.slice(1).join(" "));
};

kBuiltin.uptime  = function(terminal, args) {
	var seconds = (new Date().getTime() - gStartTime.getTime()) / 1000.0;
	var units = ["minutes", "hours", "days", "weeks", "months", "years"];
	var count = [60, 60, 24, 7, 365.25 / 52, 12];
	var value = seconds;
	var unit = "seconds";
	var index = 0;
	while (index < count.length) {
		var newValue = value / count[index];
		if (newValue > 1.0) {
			value = newValue;
			unit = units[index];
			index++;
		} else {
			break;
		}
	}
	value = Math.round(value * 10) / 10;
	terminal.print(value + " " + unit);
};

function isSpace(c) {
	return " \t\r\n\v\f".indexOf(c) != -1;
}

function splitArgs(command) {
	var result = [];
	var word = "";
	var inSpace = true;
	var quote;
	var escaped = false;

	for (var i = 0; i <= command.length; i++) {
		if (i == command.length || (!inSpace && !quote && !escaped && isSpace(command.charAt(i)))) {
			result.push(word);
			inSpace = true;
		} else {
			var c = command.charAt(i);
			var readWord = false;
			if (inSpace && !isSpace(c)) {
				inSpace = false;
				readWord = true;
				word = "";
			} else if (!inSpace) {
				readWord = true;
			}
			if (readWord) {
				if (escaped) {
					word += c;
					escaped = false;
				} else if (c == '\\') {
					escaped = true;
				} else if (!quote && (c == '"' || c == "'")) {
					quote = c;
				} else if (quote && c == quote) {
					quote = null;
				} else {
					word += c;
				}
			}
		}
	}
	if (quote) {
		throw new Error("Unmatched " + quote);
	} else if (escaped) {
		throw new Error("Trailing \\");
	}
	return result;
}

function evaluate(terminal, command, credentials) {
	try {
		var argv = splitArgs(command || "");
		if (argv.length) {
			if (kBuiltin[argv[0]]) {
				kBuiltin[argv[0]](terminal, argv);
			} else if (gRegistered[argv[0]]) {
				var handler = gRegistered[argv[0]];
				imports.auth.transferCredentials(credentials, handler.taskName).then(function(childCredentials) {
					return handler.callback(terminal, argv, childCredentials);
				}).catch(function(error) {
					terminal.print(error.stackTrace);
						terminal.print("while executing: " + command);
				});
			} else {
				terminal.print("Bad command or filename.");
			}
		}
	} catch (error) {
		terminal.print(error.toString());
		terminal.print("while evaluating: " + command);
	}
}

function register(command, callback) {
	gRegistered[command] = {callback: callback, taskName: this.taskName};
}

exports = {
	evaluate: evaluate,
	register: register,
};