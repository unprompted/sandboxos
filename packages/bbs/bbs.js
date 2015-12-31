"use strict";
var gWaiting = [];
var gMessages = [];

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

imports.terminal.print('Welcome to');
imports.terminal.print('   ______                _          ____  ____ _____');
imports.terminal.print('  / ____/___  _______  _( )_____   / __ )/ __ ) ___/');
imports.terminal.print(' / /   / __ \\/ ___/ / / /// ___/  / __  / __  \\__ \\ ');
imports.terminal.print('/ /___/ /_/ / /  / /_/ / (__  )  / /_/ / /_/ /__/ / ');
imports.terminal.print('\\____/\\____/_/   \\__, / /____/  /_____/_____/____/  ');
imports.terminal.print('                /____/                              ');
imports.terminal.print('             the future of text-based communication.');

function main() {
	return wait().then(function(input) {
		imports.terminal.print("You entered: " + input);
		if (input == "exit") {
			imports.terminal.print("Goodbye.");
			exit(0);
		}
	}).then(main);
}

main();
