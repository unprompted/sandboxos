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

function explodePath() {
	return /^\/~([^\/]+)\/([^\/]+)(.*)/.exec(window.location.pathname);
}

function packageOwner() {
	return explodePath()[1];
}

function packageName() {
	return explodePath()[2];
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
