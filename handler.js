var votes = {a: 0, b: 0, c: 0};
var last = undefined;

function generate() {
	return " \
<html> \
	<body> \
		<h1>Votey thingy</h1> \
		<form method='POST' action='/handler/post'> \
			<input type='submit' name='A' value='A (" + votes['a'] + ")'></input> \
			<input type='submit' name='B' value='B (" + votes['b'] + ")'></input> \
			<input type='submit' name='C' value='C (" + votes['c'] + ")'></input> \
			<div>" + last + "</div> \
		</form> \
	</body> \
</html>";
}

function onMessage(from, message) {
	var contents;
	if (message.request.uri == "/handler/post") {
		var vote = message.request.body[0].toLowerCase();
		votes[vote] += 1;
		last = vote;
	}
	contents = generate();
	parent.invoke({
		to: "httpd",
		response: "HTTP 1.0/OK\nContent-Type: text/html\nConnection: close\n\n" + contents,
		messageId: message.messageId
	});
	return true;
}