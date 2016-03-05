"use strict";

var kMessages = [
	[
		"    _    _                 _   ",
		"   / \\  | |__   ___  _   _| |_ ",
		"  / _ \\ | '_ \\ / _ \\| | | | __|",
		" / ___ \\| |_) | (_) | |_| | |_ ",
		"/_/   \\_\\_.__/ \\___/ \\__,_|\\__|",
		"",
		"Tilde Friends: Webapps that anyone can download, modify, run, and share.",
		"",
		"You are looking at a web site running on a JavaScript and C++ web server that uses Google V8 to let visitors author webapps.",
		"",
		["Full source is here <",
		 	{href: "https://www.unprompted.com/projects/browser/sandboxos/trunk/"},
		 	">, but it is probably more fun and useful to poke around the ",
		 	{href: "/~cory/index", value: "existing webapps"},
		 	".  A ",
		 	{href: "https://www.unprompted.com/projects/wiki/Projects/SandboxOS", value: "prebuilt Windows .zip"},
		 	" is available as well.  ",
		],
		"",
		[
			"Use the links at the top of the page to explore existing apps.  When you are ready, click edit and start making your own.  See the ",
			{href: "/~cory/documentation", value: "documentation"},
			" for more information.",
		],
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
