"use strict";
var gWaiting = [];
var gMessages = [];

imports.terminal.register("onMessage", function(message) {
	imports.terminal.print("Incoming message: " + message);
});

imports.terminal.register("onInput", function(input) {
	if (gWaiting) {
		for (var i = 0; i < gWaiting.length; i++) {
			gWaiting[i](input);
		}
		gWaiting.length = 0;
	} else {
		gMessages.push(input);
	}
});

function wait() {
	return new Promise(function(resolve, reject) {
		if (gMessages.length) {
			resolve(gMessages.shift());
		} else {
			gWaiting.push(resolve);
		}
	});
}

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
	return wait().then(function(input) {
		if (input == "chat") {
			return chat();
		} if (input == "exit") {
			imports.terminal.print("Goodbye.");
			exit(0);
		} else {
			imports.terminal.print("I didn't understand that.");
		}
	}).then(main);
}

function chat() {
	imports.terminal.print("");
	imports.terminal.print("You are now in a chat.");
	return wait().then(chatLoop);
}

function chatLoop(input) {
	var next;
	if (input == "exit") {
		next = main();
	} else {
		imports.core.broadcast(input);
		next = wait().then(chatLoop);
	}
	return next;
}

welcome();
main();
