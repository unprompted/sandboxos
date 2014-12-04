var cm;
var currentFileName;

function saveFile() {
	cm.save()
	if (currentFileName) {
		$.ajax({
			type: "POST",
			url: "put",
			data: {fileName: currentFileName, contents: JSON.stringify($("#edit").val())},
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
	if (cm) {
		cm.setValue(text);
	} else {
		$("#edit").val(text);
	}
	if (currentFileName) {
		if (endsWith(currentFileName, ".js")) {
			cm.setOption("mode", "javascript");
		} else if (endsWith(currentFileName, ".json")) {
			cm.setOption("mode", {name: "javascript", json: true});
		} else if (endsWith(currentFileName, ".html")) {
			cm.setOption("mode", "htmlmixed");
		} else if (endsWith(currentFileName, ".css")) {
			cm.setOption("mode", "css");
		}
	}
}

function clonePackage() {
	var newPackage = prompt("Name of new package:");
	if (newPackage) {
		$.ajax({
			url: "clone",
			data: {newName: newPackage},
		}).then(function(data) {
			alert("Package '" + newPackage + "' created successfully.");
			window.location.href = "/editor/" + newPackage + "/";
		}).fail(function(xhr, status, error) {
			alert("Unable to clone package to " + newPackage + ".\n\n" + JSON.parse(xhr.responseText));
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
	$("#save").val("Save " + currentFileName);
	$("#rename").val("Rename " + currentFileName);
	$("#delete").val("Delete " + currentFileName);
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
	var editor = document.getElementById("edit");
	cm = CodeMirror.fromTextArea(editor, {
		indentWithTabs: true,
		theme: 'lesser-dark',
		indentUnit: 4,
		smartIndent: false,
		lineNumbers: true,
		electricChars: false,
		showTrailingSpace: true,
	});
});
