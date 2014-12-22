var currentFileName;
var gEditor;

function saveFile() {
	if (currentFileName) {
		$.ajax({
			type: "POST",
			url: "put",
			data: {fileName: currentFileName, contents: JSON.stringify(gEditor.getValue())},
			dataType: "text",
		}).fail(function(xhr, status, error) {
			alert("Unable to save " + currentFileName + ".\n\n" + JSON.parse(xhr.responseText));
		});
	}
}

function endsWith(string, suffix) {
	return string.substring(string.length - suffix.length) == suffix;
}

function setText(text) {
	if (gEditor) {
		gEditor.setValue(text);
		gEditor.selection.clearSelection();
	}
	if (currentFileName) {
		if (endsWith(currentFileName, ".js")) {
			gEditor.session.setMode("ace/mode/javascript");
		} else if (endsWith(currentFileName, ".json")) {
			gEditor.session.setMode("ace/mode/json");
		} else if (endsWith(currentFileName, ".html")) {
			gEditor.session.setMode("ace/mode/html");
		} else if (endsWith(currentFileName, ".css")) {
			gEditor.session.setMode("ace/mode/css");
		}
	}
}

function copyToWorkspace() {
	if (confirm("Are you sure you want to copy this package to your workspace?  It will overwrite any existing copy you have.")) {
		$.ajax({
			url: "copyToWorkspace",
		}).then(function(data) {
			alert("Package copied successfully.");
			window.location.reload();
		}).fail(function(xhr, status, error) {
			alert("Unable to copy the package to your workspace.\n\n" + JSON.parse(xhr.responseText));
		});
	}
}

function install() {
	if (confirm("Are you sure you want to install this package?  It will overwrite any existing package of the same name.")) {
		$.ajax({
			url: "install",
		}).then(function(data) {
			alert("Package installed successfully.\n\n" + JSON.stringify(data));
		}).fail(function(xhr, status, error) {
			alert("Unable to install the package.\n\n" + JSON.parse(xhr.responseText));
		});
	}
}

function newFile() {
	var fileName = prompt("Name of new file:");
	if (fileName) {
		$.ajax({
			type: "POST",
			url: "put",
			data: {fileName: fileName, contents: JSON.stringify("")},
			dataType: "text",
		}).then(refreshPackage);
	}
}

function renameFile() {
	var fileName = prompt("Rename " + currentFileName + " to:");
	if (fileName) {
		$.ajax({
			url: "rename",
			data: {oldName: currentFileName, newName: fileName},
			dataType: "text",
		}).then(refreshPackage).fail(function(xhr, status, error) {
			alert("Unable to rename " + currentFileName + " to " + fileName + ".\n\n" + JSON.parse(xhr.responseText));
		});
	}
}

function deleteFile() {
	if (confirm("Are you sure you want to delete " + currentFileName + "?")) {
		$.ajax({
			url: "unlink",
			data: {fileName: currentFileName},
			dataType: "text",
		}).then(refreshPackage);
	}
}

function refreshPackage() {
	currentFileName = null;
	$("#fileSpecific").hide();
	setText("");
	$.ajax({
		url: "list",
		dataType: "JSON",
	}).then(function(data) {
		$("#files").empty();
		for (var i in data) {
			var li = document.createElement("li");
			$(li).text(data[i]);
			$(li).click(changeFile);
			$("#files").append(li);
		}
	});
}

function changeFile() {
	currentFileName = $(this).text();
	$("#fileSpecific").show();
	$("#files").children().each(function (i) {
		if ($(this).text() == currentFileName) {
			$(this).addClass("current");
		} else {
			$(this).removeClass("current");
		}
	});
	$.ajax({
		url: "get",
		data: {fileName: currentFileName},
		dataType: "text",
	}).done(setText);
}

$(document).ready(function() {
	refreshPackage();
	gEditor = ace.edit("editor");
	gEditor.$blockScrolling = Infinity;
	gEditor.setAnimatedScroll(false);
	gEditor.setShowInvisibles(true);
	gEditor.setTheme("ace/theme/terminal");
	gEditor.session.setUseSoftTabs(false);
});
