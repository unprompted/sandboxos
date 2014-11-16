var haveIndex = -1;
function enter() {
	if (event.keyCode == 13) {
		send();
		event.preventDefault();
	}
}
function receive() {
	$.ajax({
		url: "/chat2/receive",
			method: "POST",
			data: haveIndex.toString(),
			dataType: "text",
	}).then(function(data) {
		print(data.message);
		haveIndex = data.index;
		receive();
	}).fail(function(xhr, message, error) {
		print("RECEIVE FAILED.  Reload to resume.");
	});
}
function print(message) {
	if (document.getElementById("chat").value) {
		document.getElementById("chat").value += "\n";
	}
	document.getElementById("chat").value += message.name + ': ' + message.text;
}
function send() {
	var name = document.getElementById("name").value;
	var text = document.getElementById("input").value;
	document.getElementById("input").value = "";
	$.ajax({
		url: "/chat2/send",
			method: "POST",
			data: {name: name, text: text},
			dataType: "text",
	}).fail(function(xhr, status, error) {
		print("SEND FAILED: " + status + ": " + error)
	});
}
$(document).ready(receive);