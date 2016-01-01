"use strict";
var gOnInput = null;

imports.terminal.register("onMessage", function(message) {
	imports.terminal.print("Incoming message: " + message);
});

imports.terminal.register("onInput", function(input) {
	if (gOnInput) {
		gOnInput(input);
	}
});

function welcome() {
	imports.terminal.print('Welcome to');
	imports.terminal.print('   ______                _          ____  ____ _____');
	imports.terminal.print('  / ____/___  _______  _( )_____   / __ )/ __ ) ___/');
	imports.terminal.print(' / /   / __ \\/ ___/ / / /// ___/  / __  / __  \\__ \\ ');
	imports.terminal.print('/ /___/ /_/ / /  / /_/ / (__  )  / /_/ / /_/ /__/ / ');
	imports.terminal.print('\\____/\\____/_/   \\__, / /____/  /_____/_____/____/  ');
	imports.terminal.print('                /____/                              ');
	imports.terminal.print('                    yesterday\'s technology...today!');
}

function main() {
	imports.terminal.print("");
	imports.terminal.print("Commands:");
	imports.terminal.print("  chat       enter the group chat");
	imports.terminal.print("  exit       end the current session (and start a new one)");
	gOnInput = function(input) {
		if (input == "chat") {
			chat();
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
	imports.terminal.print("You are now in a chat.  Anything you type will be broadcast to everyone else connected.  To leave, say \"exit\".");
	gOnInput = function(input) {
		if (input == "exit") {
			main();
		} else {
			imports.core.broadcast(input);
		}
	};
}

welcome();
main();
