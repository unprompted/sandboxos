var kBuiltin = {};

kBuiltin.hello = function(terminal) {
	var commands = [];
	for (var i in kBuiltin) {
		commands.push(i);
	}
	commands.sort();
	terminal.print("Welcome. Available commands: " + commands.join(", "));
}

kBuiltin.clear = function(terminal) {
	terminal.clear();
}

kBuiltin.w = function(terminal) {
	terminal.who().then(function(terminals) {
		for (var i in terminals) {
			terminal.print(i + "\t" + JSON.stringify(terminals[i]));
		}
	});
}

kBuiltin.send = function(terminal, argv) {
	terminal.send(argv[1], argv.slice(2).join(" "));
}

kBuiltin.date = function(terminal) {
	terminal.print(new Date().toString());
}

function splitArgs(command) {
	// TODO: Real argument splitting.
	return command.split(/\s+/);
}

function evaluate(terminal, command) {
	var argv = splitArgs(command.trim());
	if (argv.length) {
		if (kBuiltin[argv[0]]) {
			kBuiltin[argv[0]](terminal, argv);
		} else {
			terminal.print("Bad command or filename.");
		}
	}
}

exports = {
	evaluate: evaluate,
};