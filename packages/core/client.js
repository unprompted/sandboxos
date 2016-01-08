var gHaveIndex = -1;
var gSessionId;

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
		url: window.location.href + "/receive?sessionId=" + gSessionId,
			method: "POST",
			data: gHaveIndex.toString(),
			dataType: "json",
	}).then(function(data) {
		for (var i in data.lines) {
			if (typeof data.lines[i] == "string") {
				print(data.lines[i]);
			} else if (data.lines[i] instanceof Array) {
				printStructured(data.lines[i]);
			} else if (data.lines[i] && data.lines[i].action == "clear") {
				document.getElementById("terminal").innerText = "";
			} else {
				print(JSON.stringify(data.lines[i]));
			}
		}
		gHaveIndex = data.index;
		receive();
	}).fail(function(xhr, message, error) {
		print("RECEIVE FAILED.  Reload to resume.");
	});
}

function autoNewLine() {
	document.getElementById("terminal").appendChild(document.createElement("br"));
}

function print(line) {
	if (!line) {
		document.getElementById("terminal").appendChild(document.createElement("br"));
	} else {
		autoNewLine();
		document.getElementById("terminal").appendChild(document.createTextNode(line));
	}
	autoScroll();
}

function commandClick() {
	send(this.dataset.command);
}

function printStructured(list) {
	autoNewLine();
	var terminal = document.getElementById("terminal");
	for (var i = 0; i < list.length; i++) {
		var item = list[i];
		if (typeof item == "string" || item instanceof String) {
			terminal.appendChild(document.createTextNode(item));
		} else {
			var node;
			if (item.href) {
				node = document.createElement("a");
				node.setAttribute("href", item.href);
			} else {
				node = document.createElement("span");
			}
			if (item.style) {
				node.setAttribute("style", item.style);
			}
			node.innerText = item.value || item.href || item.command;
			if (item.command) {
				node.dataset.command = item.command;
				node.onclick = commandClick;
				node.setAttribute("class", "command");
			}
			terminal.appendChild(node);
		}
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
		url: window.location.href + "/send?sessionId=" + gSessionId,
			method: "POST",
			data: value,
			dataType: "text",
	}).fail(function(xhr, status, error) {
		print("SEND FAILED: " + status + ": " + error)
	});
}

function getNewSession() {
	$.ajax({
		url: window.location.href + "/newSession",
			method: "GET",
			dataType: "json",
	}).then(function(data) {
		gSessionId = data.sessionId;
		receive();
	}).fail(function(xhr, message, error) {
		print("Error starting session.");
	});
}

$(document).ready(function() {
	$("#input").keydown(enter);
	$("#input").focus();
});

$(window).load(function() {
	setTimeout(function() {
		getNewSession();
	}, 0);
});
