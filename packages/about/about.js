"use strict";

var kMessages = [
	[
		"    _    _                 _   ",
		"   / \\  | |__   ___  _   _| |_ ",
		"  / _ \\ | '_ \\ / _ \\| | | | __|",
		" / ___ \\| |_) | (_) | |_| | |_ ",
		"/_/   \\_\\_.__/ \\___/ \\__,_|\\__|",
		"",
		"The goal: enable de-centralized webapps that anyone can download, modify, run, and share.",
		"",
		["Source: ", {href: "https://www.unprompted.com/projects/browser/sandboxos/trunk/"}],
		["Cory's BBS: ", {href: "https://www.sandboxos.net/bbs"}],
	],
	[
		"-------------------",
		"Why de-centralized?",
		"-------------------",
		"",
		"The big players in cloud services don't really deserve much trust.",
		"[x] Data breaches.",
		"[x] Unexplained down-time.",
		"[x] Cut useful services because they're not profitable enough.",
		"",
		"As a consumer there's little recourse when these things happen.",
		"",
		"If you, your business, your group of friends, or your community can run its own services, then things are different.",
	],
	[
		"------------",
		"Why webapps?",
		"------------",
		"",
		"Because they are the single most widely accessible thing we have today.",
	],
	[
		"------------------------------------------",
		"Didn't you say webapps?  Isn't this a BBS?",
		"------------------------------------------",
		"",
		"This is a BBS, but it is also a webapp.",
		"  It's an experiment.",
		"    We can add web features to a BBS to serve the needs of more and more interesting webapps.",
		"",
		"But for now, a BBS sets an attainable level of expectations with a good dose of nostalgia.",
	],
	[
		"------------------------------------------------",
		"What does it mean that anyone can download them?",
		"------------------------------------------------",
		"",
		"You see that \"view source\" link at the top left?  That means you can look at it, download it, and drop it onto your own server.  If you are running these apps on your server, then you are also sharing them.",
	],
	[
		"----------------------",
		"Blah blah blah modify?",
		"----------------------",
		"",
		["1. ", {href: "https://www.wikipedia.org/"}],
		["2. ", {href: "http://www.gnu.org/philosophy/free-sw.html"}],
		"",
		"See also the edit link at the top left.",
		"",
		"Right now, this thing is wide open like Wikipedia.  Because it can't do much yet, it's basically like my server is a web browser and I'm viewing your web page, if you try to write malicious javascript.  " +
			"Any more dangerous features that are added will have to come with some restrictions on who can edit apps that use them.",
	],
	[
		"---------------------------",
		"How hard will it be to run?",
		"---------------------------",
		"",
		"I want it to be an Android app.  Run it on an old phone or from your pocket.",
		"",
		"Install apps from another server onto your server in a few taps.",
		"",
		"And when you can't connect, because the server phone doesn't have signal, it will just be like the days of BBSes.",
	],
	[
		"--------",
		" Status ",
		"--------",
		"",
		"It's not done.",
		"",
		"Backend things to do:",
		"[ ] Filesystem access",
		"[ ] Network access",
		"[ ] Deeper process model - connect and resume a chat session",
		"[ ] Limits on all resource usage",
		"",
		"Terminal frontend things to do:",
		"[ ] rich formatting",
		"[ ] images",
		"[ ] sound",
		"[ ] video",
		"[ ] gamepad?",
		"[ ] big touchable text menus for phones",
		"[ ] favicon",
		"[ ] redirect to other apps",
		"[ ] open URLs",
		"[ ] merge the terminal and editor",
		"[ ] blink",
		"[ ] animation",
		"",
		"BBS things to do:",
		"[ ] colored ANSI art",
		"[ ] votes",
		"[ ] better chat - nicknames?",
		"[ ] implement twitter",
	],
];
var gIndex = 0;

function printNextMessage() {
	if (gIndex < kMessages.length) {
		var block = kMessages[gIndex];
		for (var i = 0; i < block.length; i++) {
			imports.terminal.print(block[i]);
		}
		imports.terminal.print("");
	}
	imports.terminal.print("(press enter to continue, \"exit\" to exit)");
	if (gIndex < kMessages.length) {
		gIndex++;
	}
}

imports.terminal.register("onInput", function(input) {
	if (input == "exit") {
		exit();
	} else {
		printNextMessage();
	}
});

printNextMessage();