"use strict";

var gHaveIndex = -1;
var gSessionId;
var gCredentials;
var gErrorCount = 0;

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

function url() {
	var hash = window.location.href.indexOf('#');
	return hash != -1 ? window.location.href.substring(0, hash) : window.location.href;
}

function storeTarget(target) {
	$("#target").text(target || "");
}

function receive() {
	$.ajax({
		url: url() + "/receive?sessionId=" + gSessionId,
			method: "POST",
			data: gHaveIndex.toString(),
			dataType: "json",
	}).then(function(data) {
		for (var i in data.lines) {
			var line = data.lines[i];

			var target = "terminal_";
			if (line && line.terminal) {
				if (document.getElementById("terminal_" + line.terminal)) {
					target = "terminal_" + line.terminal;
				}
				line = line.value;
			}
			if (line && line.action == "ping") {
				// PONG
			} else if (line && line.action == "session") {
				gSessionId = line.session.sessionId;
				gCredentials = line.session.credentials;
				updateLogin();
			} else if (line && line[0] && line[0].action == "ready") {
				if (window.location.hash) {
					send({event: "hashChange", hash: window.location.hash});
				}
			} else if (line && line[0] && line[0].action == "notify") {
				new Notification(line[0].title, line[0].options);
			} else if (line && line[0] && line[0].action == "title") {
				window.document.title = line[0].value;
			} else if (line && line[0] && line[0].action == "prompt") {
				var prompt = document.getElementById("prompt");
				while (prompt.firstChild) {
					prompt.removeChild(prompt.firstChild);
				}
				prompt.appendChild(document.createTextNode(line[0].value));
			} else if (line && line[0] && line[0].action == "password") {
				var prompt = document.getElementById("input");
				prompt.setAttribute("type", line[0].value ? "password" : "text");
			} else if (line && line[0] && line[0].action == "hash") {
				window.location.hash = line[0].value;
			} else if (line && line[0] && line[0].action == "update") {
				document.getElementById("update").setAttribute("Style", "display: inline");
			} else if (line && line[0] && line[0].action == "configure") {
				var options = line[0].options;
				if (options) {
					var node = document.getElementById("terminal_" + line[0].name);
					if (!node) {
						node = document.createElement("div");
						document.getElementById("terminals").appendChild(node);
					}
					node.setAttribute("id", "terminal_" + line[0].name);
					node.setAttribute("style", options.style);
				} else {
					var node = document.getElementById("terminal_" + line[0].name);
					if (node) {
						node.remove();
					}
				}
			} else {
				print(document.getElementById(target), line);
			}
		}
		if ("index" in data) {
			gHaveIndex = data.index;
		}
		receive();
		if (gErrorCount) {
			document.getElementById("status").setAttribute("style", "display: none");
		}
		gErrorCount = 0;
	}).fail(function(xhr, message, error) {
		var node = document.getElementById("status");
		while (node.firstChild) {
			node.removeChild(node.firstChild);
		}
		node.appendChild(document.createTextNode("ERROR: " + JSON.stringify([message, error])));
		node.setAttribute("style", "display: inline; color: #dc322f");
		if (gErrorCount < 60) {
			setTimeout(receive, 1000);
		} else {
			setTimeout(receive, 60 * 1000);
		}
		gErrorCount++;
	});
}

function autoNewLine(terminal) {
	terminal.appendChild(document.createElement("br"));
}

function print(terminal, data) {
	autoNewLine(terminal);
	printStructured(terminal, data);
	autoScroll(terminal);
}

function printStructured(container, data) {
	if (typeof data == "string") {
		container.appendChild(document.createTextNode(data));
	} else if (data && data[0] !== undefined) {
		for (var i in data) {
			printStructured(container, data[i]);
		}
	} else if (data && data.action == "clear") {
		while (container.firstChild) {
			container.removeChild(container.firstChild);
		}
	} else if (data) {
		var node;
		if (data.href) {
			node = document.createElement("a");
			node.setAttribute("href", data.href);
			node.setAttribute("target", "_blank");
		} else if (data.iframe) {
			node = document.createElement("iframe");
			node.setAttribute("srcdoc", data.iframe);
			node.setAttribute("sandbox", "allow-forms allow-scripts");
			node.setAttribute("width", data.width || 320);
			node.setAttribute("height", data.height || 240);
		} else if (data.image) {
			node = document.createElement("img");
			node.setAttribute("src", data.image);
		} else {
			node = document.createElement("span");
		}
		if (data.style) {
			node.setAttribute("style", data.style);
		}
		if (data.class) {
			node.setAttribute("class", data.class);
		}
		printStructured(node, data.value || data.href || data.command || "");
		if (data.command) {
			node.dataset.command = data.command;
			node.onclick = commandClick;
			node.setAttribute("class", "command");
		}
		container.appendChild(node);
	} else {
		printStructured(container, JSON.stringify(data));
	}
}

function commandClick() {
	send(this.dataset.command);
	$("#input").focus();
}

function autoScroll(terminal) {
	terminal.scrollTop = terminal.scrollHeight - terminal.clientHeight;
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
		url: url() + "/send?sessionId=" + gSessionId,
			method: "POST",
			data: JSON.stringify(value),
			dataType: "text",
	}).fail(function(xhr, status, error) {
		var node = document.getElementById("status");
		while (node.firstChild) {
			node.removeChild(node.firstChild);
		}
		node.appendChild(document.createTextNode("Send failed: " + JSON.stringify([status, error])));
		node.setAttribute("style", "display: inline; color: #dc322f");
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
		a.setAttribute("href", "/login/logout?return=" + encodeURIComponent(url()));
	} else {
		window.location.href = "/login?return=" + encodeURIComponent(url());
	}
	login.appendChild(a);
}

var gOriginalInput;
function dragHover(event) {
	event.stopPropagation();
	event.preventDefault();
	if (event.type == "dragover") {
		if (!$("#input").hasClass("drop")) {
			$("#input").addClass("drop");
			gOriginalInput = $("#input").val();
			$("#input").val("drop file to upload");
		}
	} else {
		$("#input").removeClass("drop");
		$("#input").val(gOriginalInput);
	}
}

function fixImage(sourceData, maxWidth, maxHeight, callback) {
	var result = sourceData;
	var image = new Image();
	image.crossOrigin = "anonymous";
	image.referrerPolicy = "no-referrer";
	image.onload = function() {
		if (image.width > maxWidth || image.height > maxHeight) {
			var downScale = Math.min(maxWidth / image.width, maxHeight / image.height);
			var canvas = document.createElement("canvas");
			canvas.width = image.width * downScale;
			canvas.height = image.height * downScale;
			var context = canvas.getContext("2d");
			context.clearRect(0, 0, canvas.width, canvas.height);
			image.width = canvas.width;
			image.height = canvas.height;
			context.drawImage(image, 0, 0, image.width, image.height);
			result = canvas.toDataURL();
		}
		callback(result);
	};
	image.src = sourceData;
}

function sendImage(image) {
	fixImage(image, 320, 240, function(result) {
		send({image: result});
	});
}

function fileDropRead(event) {
	sendImage(event.target.result);
}

function fileDrop(event) {
	dragHover(event);

	var done = false;
	if (!done) {
		var files = event.target.files || event.dataTransfer.files;
		for (var i = 0; i < files.length; i++) {
			var file = files[i];
			if (file.type.substring(0, "image/".length) == "image/") {
				var reader = new FileReader();
				reader.onloadend = fileDropRead;
				reader.readAsDataURL(file);
				done = true;
			}
		}
	}

	if (!done) {
		var html = event.dataTransfer.getData("text/html");
		var match = /<img.*src="([^"]+)"/.exec(html);
		if (match) {
			sendImage(match[1]);
			done = true;
		}
	}

	if (!done) {
		var text = event.dataTransfer.getData("text/plain");
		if (text) {
			send(text);
			done = true;
		}
	}
}

function enableDragDrop() {
	var body = document.body;
	body.addEventListener("dragover", dragHover);
	body.addEventListener("dragleave", dragHover);

	body.addEventListener("drop", fileDrop);
}

function hashChange() {
	send({event: 'hashChange', hash: window.location.hash});
}

function focus() {
	send({event: "focus"});
}

function blur() {
	send({event: "blur"});
}

$(document).ready(function() {
	if (Notification) {
		Notification.requestPermission();
	}
	$("#input").keydown(enter);
	$("#input").focus();
	window.addEventListener("hashchange", hashChange);
	window.addEventListener("focus", focus);
	window.addEventListener("blur", blur);
	enableDragDrop();
});

$(window).load(function() {
	setTimeout(function() {
		receive();
	}, 0);
});
