"use strict";
imports.terminal.print("API Documentation");
imports.terminal.print("=================");

let kDocumentation = {
	"imports.core.broadcast": `Broadcast a message to errybody.`,
};

dumpDocumentation("imports", imports);

function dumpDocumentation(prefix, object) {
	if (typeof object == "function") {
		imports.terminal.print(prefix + " => " + (kDocumentation[prefix] ? kDocumentation[prefix] : object));
	} else if (object) {
		for (let i in object) {
			dumpDocumentation(prefix + "." + i, object[i]);
		}
	}
}