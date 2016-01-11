"use strict";

let kDocumentation = {
	"imports.core.broadcast": ["message", "Broadcast a message to every other instance of the same app.  Messages will be received through the \"onMessage\" event."],
	"imports.core.getService": ["name", "Get a reference to a long-running service process identified by name.  A process will be started if it is not already running.  Useful for coordinating between client processes."],
	"imports.core.getPackages": ["", "Get a list of all available applications."],
	"imports.core.getUsers": ["", "Get a list of all online users."],
	"imports.core.register": ["eventName, handlerFunction", "Register a callback function for the given event."],
	"imports.database.get": ["key", "Retrieve the database value associated with the given key."],
	"imports.database.set": ["key, value", "Sets the database value for the given key, overwriting any existing value."],
	"imports.database.getAll": ["", "Retrieve a list of all key names."],
	"imports.database.remove": ["key", "Remove the database entry for the given key."],
	"imports.terminal.print": ["arguments...", `Print to the terminal.  Multiple arguments and lists are all expanded.  The following special values are supported:
	{href: "http://www..."} => Create a link to the href value.  Text will be the href value or 'value' if specified.
	{iframe: "<html>...</html>", width: 640, height: 480} => Create an iframe with the given srcdoc.
	{style: "color: #f00", value: "Hello, world!"} => Create styled text.
	{command: "exit", value: "get out of here"} => Create a link that when clicked will act as if the user typed the given command.`],
	"imports.terminal.clear": ["", "Remove all terminal output."],
};

imports.terminal.print("V8 Version ", version);
imports.terminal.print("");

imports.terminal.print("API Documentation");
imports.terminal.print("=================");
dumpDocumentation("imports", imports);
imports.terminal.print("");
imports.terminal.print(`Notes
=====
All functions under "imports" are invoked asynchronously.  They
immediately return a `,
{href: "https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise", value: "Promise"},
` object.  If you want to do
something with the result, you most likely want to call them
like this:

	imports.database.get(key).then(function(value) {
		doSomethingWithTheResult(value);
	});`);

function dumpDocumentation(prefix, object) {
	if (typeof object == "function") {
		let documentation = kDocumentation[prefix] || ["", ""];
		imports.terminal.print(prefix + "(" + documentation[0] + ")");
		imports.terminal.print("\t", documentation[1]);
		imports.terminal.print("");
	} else if (object) {
		for (let i in object) {
			dumpDocumentation(prefix + "." + i, object[i]);
		}
	}
}