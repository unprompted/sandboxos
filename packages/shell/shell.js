print("shell ready");

var kBuiltin = {};

kBuiltin.hello = function(terminal) {
	terminal.print("Welcome to...something.");
}

kBuiltin.clear = function(terminal) {
	terminal.clear();
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