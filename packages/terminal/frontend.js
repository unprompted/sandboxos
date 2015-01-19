var haveIndex = -1;

function enter(event) {
	if (event.keyCode == 13) {
		send();
		event.preventDefault();
	}
}

function receive() {
	$.ajax({
		url: "/terminal/receive",
			method: "POST",
			data: haveIndex.toString(),
			dataType: "json",
	}).then(function(data) {
		for (var i in data.lines) {
			if (typeof data.lines[i] == "string") {
				print(data.lines[i]);
			} else if (data.lines[i] && data.lines[i].action == "clear") {
				$("#terminal").val("");
			} else {
				print(JSON.stringify(data.lines[i]));
			}
		}
		haveIndex = data.index;
		receive();
	}).fail(function(xhr, message, error) {
		print("RECEIVE FAILED.  Reload to resume.");
	});
}

function print(line) {
	if (document.getElementById("terminal").value) {
		document.getElementById("terminal").value += "\n";
	}
	document.getElementById("terminal").value += line;
	var textarea = $(document.getElementById("terminal"));
	textarea.scrollTop(textarea[0].scrollHeight - textarea.height());
}

function send(command) {
	var value = command;
	if (!command) {
		value = $("#input").val();
		$("#input").val("");
	}
	$.ajax({
		url: "/terminal/send",
			method: "POST",
			data: value,
			dataType: "text",
	}).fail(function(xhr, status, error) {
		print("SEND FAILED: " + status + ": " + error)
	});
}

$(document).ready(function() {
	$("#input").keydown(enter);
	$("#input").focus();
	send("hello");
	receive();
});
