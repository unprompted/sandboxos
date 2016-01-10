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
			print(data.lines[i]);
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

function print(data) {
	autoNewLine();
	printStructured(data);
	autoScroll();
}

function printStructured(data) {
	if (typeof data == "string") {
		document.getElementById("terminal").appendChild(document.createTextNode(data));
	} else if (data && data[0] != undefined) {
		for (var i in data) {
			printStructured(data[i]);
		}
	} else if (data && data.action == "clear") {
		document.getElementById("terminal").innerText = "";
	} else if (data) {
		var node;
		if (data.href) {
			node = document.createElement("a");
			node.setAttribute("href", data.href);
		} else if (data.iframe) {
			node = document.createElement("iframe");
			node.setAttribute("srcdoc", data.iframe);
			node.setAttribute("sandbox", "allow-forms allow-scripts");
			node.setAttribute("width", data.width || 320);
			node.setAttribute("height", data.height || 240);
		} else {
			node = document.createElement("span");
		}
		if (data.style) {
			node.setAttribute("style", data.style);
		}
		node.innerText = data.value || data.href || data.command;
		if (data.command) {
			node.dataset.command = data.command;
			node.onclick = commandClick;
			node.setAttribute("class", "command");
		}
		terminal.appendChild(node);
	} else {
		printStructured(JSON.stringify(data));
	}
}

function commandClick() {
	send(this.dataset.command);
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
