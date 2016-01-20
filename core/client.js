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
			} else if (line && line.action == "session") {
				gSessionId = line.session.sessionId;
				gCredentials = line.session.credentials;
				updateLogin();
			} else if (line && line[0] && line[0].action == "notify") {
				new Notification(line[0].title, line[0].options);
			} else {
				print(line);
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
	} else if (data && data[0] !== undefined) {
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
		node.appendChild(document.createTextNode(data.value || data.href || data.command || ""));
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
			data: JSON.stringify(value),
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

function limitImageSize(sourceData, maxWidth, maxHeight, callback) {
	var result = sourceData;
	var image = new Image();
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
	limitImageSize(image, 320, 240, function(result) {
		send({image: result});
	});
}

function fileDropRead(event) {
	sendImage(event.target.result);
}

function fileDrop(event) {
	dragHover(event);

	var files = event.target.files || event.dataTransfer.files;
	for (var i = 0; i < files.length; i++) {
		var file = files[i];
		if (file.type.substring(0, "image/".length) == "image/") {
			var reader = new FileReader();
			reader.onloadend = fileDropRead;
			reader.readAsDataURL(file);
		}
	}

	var items = event.dataTransfer.items;
	for (var i = 0; i < items.length; i++) {
		if (items[i].type == "text/plain") {
			items[i].getAsString(function(result) {
				if (result.substring(0, "data:image/".length) == "data:image/") {
					sendImage(result);
				} else {
					send(result);
				}
			});
		}
	}
}

function enableDragDrop() {
	var body = document.body;
	body.addEventListener("dragover", dragHover);
	body.addEventListener("dragleave", dragHover);

	body.addEventListener("drop", fileDrop);
}

$(document).ready(function() {
	if (Notification) {
		Notification.requestPermission();
	}
	$("#input").keydown(enter);
	$("#input").focus();
	enableDragDrop();
});

$(window).load(function() {
	setTimeout(function() {
		receive();
	}, 0);
});
