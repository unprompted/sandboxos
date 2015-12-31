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

imports.terminal.print("Welcome to a test BBS!");

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
