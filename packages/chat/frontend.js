var haveIndex = -1;
function enter() {
	if (event.keyCode == 13) {
		send();
		event.preventDefault();
	}
}
function receive() {
	$.ajax({
		url: "/chat/receive",
			method: "POST",
			data: haveIndex.toString(),
			dataType: "json",
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
	document.getElementById("chat").value += message;
	var textarea = $(document.getElementById("chat"));
	textarea.scrollTop(textarea[0].scrollHeight - textarea.height());
}
function send() {
	var value = document.getElementById("input").value;
	document.getElementById("input").value = "";
	$.ajax({
		url: "/chat/send",
			method: "POST",
			data: value,
			dataType: "text",
	}).fail(function(xhr, status, error) {
		print("SEND FAILED: " + status + ": " + error)
	});
}
$(document).ready(receive);