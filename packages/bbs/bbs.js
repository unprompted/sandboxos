imports.terminal.print("Welcome to a test BBS!"); (
imports.terminal.register("onInput", function(input) {
	imports.terminal.print("You entered: " + input);
	if (input == "exit") {
		imports.terminal.print("Goodbye.");
		exit(0);
	}
});
