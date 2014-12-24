var currentFileName;
var gEditor;
var gOriginalBuffers = {};
var gBuffers = {};

function saveFileInternal(fileName, contents) {
	return $.ajax({
			type: "POST",
			url: "put",
			data: {fileName: fileName, contents: JSON.stringify(contents)},
			dataType: "text",
	}).done(function() {
		gBuffers[fileName] = contents;
		gOriginalBuffers[fileName] = contents;
		updateModified(fileName);
	}).fail(function(xhr, status, error) {
		alert("Unable to save " + fileName + ".\n\n" + JSON.parse(xhr.responseText));
	});
}

function saveFile() {
	if (currentFileName) {
		saveFileInternal(currentFileName, gEditor.getValue());
	}
}

function endsWith(string, suffix) {
	return string.substring(string.length - suffix.length) == suffix;
}

function setText(fileName, text) {
	if (gEditor) {
		gEditor.setValue(text);
		gEditor.selection.clearSelection();
	}
	if (fileName) {
		if (!gOriginalBuffers[fileName]) {
			gOriginalBuffers[fileName] = text;
		}
		gBuffers[fileName] = text;
		if (endsWith(fileName, ".js")) {
			gEditor.session.setMode("ace/mode/javascript");
		} else if (endsWith(fileName, ".json")) {
			gEditor.session.setMode("ace/mode/json");
		} else if (endsWith(fileName, ".html")) {
			gEditor.session.setMode("ace/mode/html");
		} else if (endsWith(fileName, ".css")) {
			gEditor.session.setMode("ace/mode/css");
		}
	}
}

function copyToWorkspace(options) {
	if (options && options.suppressPrompt
		|| confirm("Are you sure you want to copy this package to your workspace?  It will overwrite any existing copy you have.")) {
		$.ajax({
			url: "copyToWorkspace",
		}).then(function(data) {
			alert("Package copied successfully.");
			refreshPackage();
		}).fail(function(xhr, status, error) {
			alert("Unable to copy the package to your workspace.\n\n" + JSON.parse(xhr.responseText));
		});
	}
}

function saveAll() {
	$("#status").text("Saving files...");
	var promises = [];
	for (var i in gBuffers) {
		if (gBuffers[i]) {
			var buffer = (i == currentFileName) ? gEditor.getValue() : gBuffers[i];
			if (gOriginalBuffers[i] != buffer) {
				promises.push(saveFileInternal(currentFileName, buffer));
			}
		}
	}
	return $.when.apply(this, promises).then(function() {
		$("#status").text("All files saved.");
	});
}

function restartTask() {
	$("#status").text("Restarting.");
	$.ajax({
		url: "restartTask",
	}).then(function(data) {
		$("#status").text("Done.");
	}).fail(function(xhr, status, error) {
		$("#status").text("Task did not start.");
		var exception = JSON.parse(xhr.responseText);
		if (exception.exception) {
			$("#errors").text(exception.message + "\n" + exception.fileName + ":" + exception.lineNumber + "\n" + exception.sourceLine);
			$("#errors").show();
		}
	});
}

function install() {
	if (confirm("Are you sure you want to install this package?  It will overwrite any existing package of the same name.")) {
		$("#errors").hide();
		saveAll().then(function() {
			$("#status").text("Installing...");
			$.ajax({
				url: "install",
			}).then(function(data) {
				$("#status").text("Installed.");
				restartTask();
			}).fail(function(xhr, status, error) {
				$("#status").text("Install failed.");
				alert("Unable to install the package.\n\n" + JSON.parse(xhr.responseText));
			});
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
	setText(null, "");
	gBuffers = {};
	$.ajax({
		url: "list",
		dataType: "JSON",
	}).then(function(data) {
		$("#files").empty();
		if (!data.length) {
			copyToWorkspace({supressPrompt: true});
		}
		for (var i in data) {
			var li = document.createElement("li");
			$(li).text(data[i]);
			$(li).click(changeFile);
			$("#files").append(li);
		}
	});
}

function changeFile() {
	if (currentFileName) {
		gBuffers[currentFileName] = gEditor.getValue();
	}
	currentFileName = $(this).text();
	$("#fileSpecific").show();
	$("#files").children().each(function (i) {
		if ($(this).text() == currentFileName) {
			$(this).addClass("current");
		} else {
			$(this).removeClass("current");
		}
	});
	
	if (gBuffers[currentFileName]) {
		setText(currentFileName, gBuffers[currentFileName]);
	} else {
		$.ajax({
			url: "get",
			data: {fileName: currentFileName},
			dataType: "text",
		}).done(function(data) { setText(currentFileName, data); });
	}
}

function updateModified(fileName) {
	if (fileName && gOriginalBuffers[fileName]) {
		var buffer = (fileName == currentFileName) ? gEditor.getValue() : gBuffers[fileName];
		var changed = gOriginalBuffers[fileName] != buffer;
		$("#files").children().each(function() {
			if ($(this).text() == fileName) {
				if (changed) {
					$(this).addClass("modified");
				} else {
					$(this).removeClass("modified");
				}
			}
		});
	}
}

function textChanged() {
	updateModified(currentFileName);
}

$(document).ready(function() {
	refreshPackage();
	gEditor = ace.edit("editor");
	gEditor.$blockScrolling = Infinity;
	gEditor.setAnimatedScroll(false);
	gEditor.setBehavioursEnabled(false);
	gEditor.setShowInvisibles(true);
	gEditor.setTheme("ace/theme/terminal");
	gEditor.session.setUseSoftTabs(false);
	gEditor.session.on("change", textChanged);
});
