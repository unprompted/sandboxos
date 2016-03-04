"use strict";

//! {"permissions": ["network"]}

terminal.print("Hello, world!");

let kFrom = core.user.name + "@unprompted.com";
let kTo = "test@unprompted.com";
let kSubject = "Hello, world!";
let kBody = "This is the body of the email."

let inBuffer = "";
let sentFrom = false;
let sentTo = false;
let sentData = false;

function lineReceived(socket, line) {
	terminal.print("> ", line);
	let parts = line.split(" ", 1);
	terminal.print(JSON.stringify(parts));
	if (parts[0] == "220") {
		socket.write("HELO rowlf.unprompted.com\r\n");
	} else if (parts[0] == "250") {
		if (!sentFrom) {
			terminal.print("FROM");
			socket.write("MAIL FROM: " + kFrom + "\r\n");
			sentFrom = true;
		} else if (!sentTo) {
			terminal.print("TO");
			socket.write("RCPT TO: " + kTo + "\r\n");
			sentTo = true;
		} else if (!sentData) {
			terminal.print("DATA");
			socket.write("DATA\r\n");
			sentData = true;
		} else {
			terminal.print("QUIT");
			socket.write("QUIT\r\n");
		}
	} else if (parts[0] == "354") {
		terminal.print("MESSAGE");
		socket.write("Subject: " + kSubject + "\r\n\r\n" + kBody + "\r\n.\r\n");
	}
}

function dataReceived(socket, data) {
	if (data === null) {
		return;
	}
	terminal.print(data);
	inBuffer += data;
	let again = true;
	while (again) {
		again = false;
		let end = inBuffer.indexOf("\n");
		if (end != -1) {
			again = true;
			let line = inBuffer.substring(0, end);
			inBuffer = inBuffer.substring(end + 1);
			lineReceived(socket, line);
		}
	}
}

network.newConnection().then(function(socket) {
	socket.read(function(data) {
		try {
			dataReceived(socket, data);
		} catch (error) {
			terminal.print("ERROR: ", error.message);
		}
	});
	socket.connect("localhost", 25);
});