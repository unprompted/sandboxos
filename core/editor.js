var gBackup;
var gEditor;

$(document).ready(function() {
	gEditor = CodeMirror.fromTextArea(document.getElementById("editor"), {
		'theme': 'base16-dark',
		'lineNumbers': true,
		'tabSize': 4,
		'indentUnit': 4,
		'indentWithTabs': true,
	});
	gBackup = gEditor.getValue();
});

function packageName() {
	var name = window.location.pathname;
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
	return name.substring(start, end);
}

function back(name) {
	window.location.pathname = "/" + (name || packageName());
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
	}).done(function() {
		gBackup = contents;
		if (run) {
			back(newName);
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
