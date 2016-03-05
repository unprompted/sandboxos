"use strict";

// This script runs server-side, once for each client session.

if (imports.terminal) {
	terminal.setEcho(false);
	terminal.split([
		{name: "graphics", basis: "520px", shrink: "0", grow: "0"},
		{name: "text"},
	]);

	// Request a callback every time the user hits enter at the terminal prompt.
	core.register("onInput", function(input) {
		// Ask a persistent service session to broadcast our message.  We'll also get a copy back.
		return core.getService("turtle").then(function(service) {
			return service.postMessage(input);
		});
	});

	// Request a callback for every message that is broadcast.
	core.register("onMessage", function(sender, message) {
		if (message.history) {
			for (var i = 0; i < message.history.length; i++) {
				// Pass the message on to the iframe in the client.
				terminal.postMessageToIframe("turtle", message.history[i]);
			}
		} else {
			// Pass the message on to the iframe in the client.
			terminal.postMessageToIframe("turtle", message);
		}
	});

	core.register("onWindowMessage", function(data) {
		terminal.print(data.message);
	});

	terminal.select("graphics");
	terminal.print("MMO Turtle Graphics using ", {href: "http://codeheartjs.com/turtle/"}, ".");

	// Add an iframe to the terminal.  This is how we sandbox code running on the client.
	var contents = `
	<script src="http://codeheartjs.com/turtle/turtle.min.js">-*- javascript -*-</script>
	<script>
	setScale(2);
	setWidth(3);

	// Receive messages in the iframe and use them to draw.
	function onMessage(event) {
		var parts = event.data.split(" ");
		var command = parts.shift();
		if (command == "reset") {
			setPosition(0, 0);
			setHeading(0);
			clear(WHITE);
			_ch_startTimer(30);
		} else if (command == "home") {
			var wasDown = _turtle.penDown;
			pu();
			setPosition(0, 0);
			setHeading(0);
			if (wasDown) {
				pd();
			}
			_ch_startTimer(30);
		} else if (command == "clear") {
			clear(WHITE);
			_ch_startTimer(30);
		} else if (["fd", "bk", "rt", "lt", "pu", "pd"].indexOf(command) != -1) {
			window[command].apply(window, parts.map(parseInt));
			event.source.postMessage(event.data, event.origin);
			_ch_startTimer(30);
		} else {
			event.source.postMessage("Unrecognized command: " + command, event.origin);
		}
	}

	// Register for messages in the iframe
	window.addEventListener('message', onMessage, false);
	</script>
	`
	terminal.print({iframe: contents, width: 640, height: 480, name: "turtle"});

	terminal.select("text");
	terminal.print("Supported commands: ", ["fd <distance>", "bk <distance>", "rt <angle>", "lt <angle>", "pu", "pd", "home", "reset", "clear"].join(", "));

	// Get the party started by asking for the history of commands (the turtle party).
	setTimeout(function() {
		core.getService("turtle").then(function(service) {
			return service.postMessage("sync");
		});
	}, 1000);
} else {
	var gHistory = [];
	core.register("onMessage", function(sender, message) {
		if (message == "reset") {
			gHistory.length = 0;
			return core.broadcast(message);
		} else if (message == "sync") {
			sender.postMessage({history: gHistory});
		} else {
			gHistory.push(message);
			return core.broadcast(message);
		}
	});
}