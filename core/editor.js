var gBackup;
var gEditor;

$(document).ready(function() {
	gEditor = CodeMirror.fromTextArea(document.getElementById("editor"), {
		'theme': 'base16-dark',
		'lineNumbers': true,
		'tabSize': 4,
		'indentUnit': 4,
		'indentWithTabs': true,
		'showTrailingSpace': true,
	});
	gBackup = gEditor.getValue();
});

function packageOwner() {
	var match = /^\/~([^\/]+)\/(^[\/]+)(.*)/.exec(window.location.pathname);
	return match[1];
}

function packageName() {
	var match = /^\/~([^\/]+)\/(^[\/]+)(.*)/.exec(window.location.pathname);
	return match[2];
	/*var name = window.location.pathname;
	var start = 0;
	var end = -1;
	while (name.charAt(start) == '/') {
		start++;
	}
	for (var i = start + 1; i < name.length; i++) {
		if (name.charAt(i) == '/') {
			end = i;
		}
	}
	return name.substring(start, end);*/
}

function back(uri) {
	if (uri) {
		window.location.pathname = uri;
	} else {
		window.location.pathname = "/~" + packageOwner() + "/" + packageName();
	}
}

function save(newName) {
	document.getElementById("save").disabled = true;
	document.getElementById("saveAs").disabled = true;

	var contents = gEditor.getValue();
	var run = document.getElementById("run").checked;

	return $.ajax({
		type: "POST",
		url: newName ? "/" + newName + "/save" : "save",
		data: contents,
		dataType: "text",
	}).done(function(uri) {
		gBackup = contents;
		if (run) {
			back(uri);
		}
	}).fail(function(xhr, status, error) {
		alert("Unable to save: " + xhr.responseText);
	}).always(function() {
		document.getElementById("save").disabled = false;
		document.getElementById("saveAs").disabled = false;
	});
}

function saveAs() {
	var newName = prompt("Save as:", packageName());
	if (newName) {
		save(newName);
	}
}

function revert() {
	gEditor.setValue(gBackup);
}
