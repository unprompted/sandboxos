var gHaveIndex = -1;
var gSessionId;
var gCredentials;

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
			var line = data.lines[i];
			if (line && line.action == "ping") {
				// PONG
			} else if (line && line[0] && line[0].action == "notify") {
				new Notification(line[0].title, line[0].options);
			} else {
				print(line);
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
		document.getElementById("terminal").innerHTML = "";
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
		if (data.class) {
			node.setAttribute("class", data.class);
		}
		node.appendChild(document.createTextNode(data.value || data.href || data.command));
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
	$("#input").focus();
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

function updateLogin() {
	var login = document.getElementById("login");
	while (login.firstChild) {
		login.removeChild(login.firstChild);
	}

	var a = document.createElement("a");
	if (gCredentials && gCredentials.session) {
		a.appendChild(document.createTextNode("logout " + gCredentials.session.name));
		a.setAttribute("href", "/login/logout?return=" + encodeURIComponent(window.location.href));
	} else {
		a.appendChild(document.createTextNode("login"));
		a.setAttribute("href", "/login?return=" + encodeURIComponent(window.location.href));
	}
	login.appendChild(a);
}

function getNewSession() {
	$.ajax({
		url: window.location.href + "/newSession",
			method: "GET",
			dataType: "json",
	}).then(function(data) {
		gSessionId = data.sessionId;
		gCredentials = data.credentials;
		updateLogin();
		receive();
	}).fail(function(xhr, message, error) {
		print("Error starting session.");
	});
}

$(document).ready(function() {
	if (Notification) {
		Notification.requestPermission();
	}
	$("#input").keydown(enter);
	$("#input").focus();
});

$(window).load(function() {
	setTimeout(function() {
		getNewSession();
	}, 0);
});
