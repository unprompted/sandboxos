"use strict";

var kMessages = [
	[
		"    _    _                 _   ",
		"   / \\  | |__   ___  _   _| |_ ",
		"  / _ \\ | '_ \\ / _ \\| | | | __|",
		" / ___ \\| |_) | (_) | |_| | |_ ",
		"/_/   \\_\\_.__/ \\___/ \\__,_|\\__|",
		"",
		"Tilde Friends: De-centralized webapps that anyone can download, modify, run, and share.",
		"",
		"You are looking at a web site running on a JavaScript and C++ web server that uses Google V8 to let visitors author webapps.",
		"",
		["Full source is here <", {href: "https://www.unprompted.com/projects/browser/sandboxos/trunk/"}, ">, but it is probably more fun and useful to poke around the ", {href: "/~cory/index", value: "existing webapps"}, "."],
	],
];
var gIndex = 0;

function printNextMessage() {
	if (gIndex < kMessages.length) {
		var block = kMessages[gIndex];
		for (var i = 0; i < block.length; i++) {
			terminal.print(block[i]);
		}
		terminal.print("");
	}
	if (gIndex < kMessages.length) {
		gIndex++;
		if (gIndex < kMessages.length) {
			terminal.print("(press enter to continue, \"exit\" to exit)");
		}
	}
}

core.register("onInput", function(input) {
	if (input == "exit") {
		exit();
	} else {
		printNextMessage();
	}
});

printNextMessage();
