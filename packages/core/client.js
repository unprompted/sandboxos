var haveIndex = -1;

function enter(event) {
	if (event.keyCode == 13) {
		send();
		event.preventDefault();
	} else if (event.keyCode == 186
		&& !event.metaKey
		&& !event.altKey
		&& !event.ctrlKey
		&& !event.shiftKey) {
		var value = $("#input").val();
		if (value && value[value.length - 1] == '\\') {
			$("#input").val(value.substring(0, value.length - 1) + ";");
			event.preventDefault();
		} else {
			storeTarget(value);
			$("#input").val("");
			event.preventDefault();
		}
	}
}

function storeTarget(target) {
	$("#target").text(target || "");
}

function receive() {
	$.ajax({
		url: window.location.href + "/receive",
			method: "POST",
			data: haveIndex.toString(),
			dataType: "json",
	}).then(function(data) {
		for (var i in data.lines) {
			if (typeof data.lines[i] == "string") {
				print(data.lines[i]);
			} else if (data.lines[i] && data.lines[i].styled) {
				printStyled(data.lines[i].styled);
			} else if (data.lines[i] && data.lines[i].action == "clear") {
				document.getElementById("terminal").innerText = "";
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

function escape(line) {
	return line.replace(/[&<>]/g, function(c) { return {"&": "&amp;", "<": "&lt;", ">": "&gt;"}[c]; });
}

function autoNewLine() {
	if (document.getElementById("terminal").innerHTML) {
		document.getElementById("terminal").innerHTML += "\n";
	}
}

function print(line) {
	autoNewLine();
	document.getElementById("terminal").innerHTML += escape(line);
	autoScroll();
}

function printStyled(styled) {
	autoNewLine();
	var terminal = document.getElementById("terminal");
	for (var i = 0; i < styled.length; i++) {
		var node = document.createElement("span");
		node.setAttribute("style", styled[i].style);
		node.innerText = styled[i].value;
		terminal.appendChild(node);
	}
	autoScroll();
}

function autoScroll() {
	var textarea = $(document.getElementById("terminal"));
	textarea.scrollTop(textarea[0].scrollHeight - textarea.height());
}

function send(command) {
	var value = command;
	if (!command) {
		var target = $("#target").text();
		var prefix = target ? target + " " : "";
		value = prefix + $("#input").val();
		$("#input").val("");
	}
	$.ajax({
		url: window.location.href + "/send",
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
	setTimeout(function() {
		receive();
	}, 1000);
});
