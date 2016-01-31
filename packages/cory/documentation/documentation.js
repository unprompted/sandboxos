"use strict";

let kDocumentation = {
	"core.broadcast": ["message", "Broadcast a message to every other instance of the same app.  Messages will be received through the \"onMessage\" event."],
	"core.getService": ["name", "Get a reference to a long-running service process identified by name.  A process will be started if it is not already running.  Useful for coordinating between client processes."],
	"core.getPackages": ["", "Get a list of all available applications."],
	"core.getUser": ["", "Gets information about the current user."],
	"core.getUsers": ["", "Get a list of all online users."],
	"core.register": ["eventName, handlerFunction", "Register a callback function for the given event."],
	"database.get": ["key", "Retrieve the database value associated with the given key."],
	"database.set": ["key, value", "Sets the database value for the given key, overwriting any existing value."],
	"database.getAll": ["", "Retrieve a list of all key names."],
	"database.remove": ["key", "Remove the database entry for the given key."],
	"terminal.print": ["arguments...", `Print to the terminal.  Multiple arguments and lists are all expanded.  The following special values are supported:
	{href: "http://www..."} => Create a link to the href value.  Text will be the href value or 'value' if specified.
	{iframe: "<html>...</html>", width: 640, height: 480} => Create an iframe with the given srcdoc.
	{style: "color: #f00", value: "Hello, world!"} => Create styled text.
	{command: "exit", value: "get out of here"} => Create a link that when clicked will act as if the user typed the given command.`],
	"terminal.clear": ["", "Remove all terminal output."],
	"terminal.readLine": ["", "Produces the next line of text from user input."],
	"terminal.setEcho": ["echo", "Controls whether the terminal will automatically echo user input (default=true)."],
	"terminal.setTitle": ["title", "Sets the browser window/tab title."],
	"terminal.setPrompt": ["prompt", "Sets the terminal prompt (default \">\")."],
	"terminal.notify": ["body, {title, icon}", ["Produces an ", {href: "https://developer.mozilla.org/en-US/docs/Web/API/notification", value: "HTML5 Notification"}, ".  Arguments are the same as the Notification constructor."]],
};

terminal.print("V8 Version ", version);
terminal.print("");

heading("API Documentation");
dumpDocumentation("imports", imports);

heading("Notes");
terminal.print(`All API functions are invoked asynchronously.  They
immediately return a `,
{href: "https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise", value: "Promise"},
` object.  If you want to do
something with the result, you most likely want to call them
like this:

	database.get(key).then(function(value) {
		doSomethingWithTheResult(value);
	});`);
terminal.print("");
heading("Colors (CSS class names)");
dumpColors();

function heading(text) {
	terminal.print({class: "green", value: "+" + "-".repeat(text.length + 2) + "+"});
	terminal.print({class: "green", value: "| " + text + " |"});
	terminal.print({class: "green", value: "+" + "-".repeat(text.length + 2) + "+"});
}

function dumpDocumentation(prefix, object, depth) {
	if (typeof object == "function") {
		let documentation = kDocumentation[prefix.substring("imports.".length)] || ["", ""];
		terminal.print(
			{class: "yellow", value: prefix.substring("imports.".length)},
			"(",
			{class: "base0", value: documentation[0]},
			")");
		terminal.print({style: "display: block; margin-left: 2em", value: documentation[1]});
		terminal.print("");
	} else if (object && typeof object != "string") {
		for (let i in object) {
			dumpDocumentation(prefix + "." + i, object[i], (depth || 0) + 1);
		}
	}
}

function dumpColors() {
	var kColors = [
		"base03",
		"base02",
		"base01",
		"base00",
		"base0",
		"base1",
		"base2",
		"base3",
		"yellow",
		"red",
		"magenta",
		"violet",
		"blue",
		"cyan",
		"green",
	];
	terminal.print({style: "background-color: #000", value: kColors.map(function(color) { return [" ", {class: color, value: color}, " "]; })});
}