var cm;
var currentFileName;
var currentPackage;

function saveFile() {
	cm.save()
	if (currentFileName && currentPackage) {
		$.ajax({
			type: "POST",
			url: "/editor/put",
			data: {fileName: currentFileName, contents: JSON.stringify($("#edit").val()), taskName: currentPackage},
			dataType: "text",
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

function newPackage() {
	var package = prompt("Name of new package:");
	if (package) {
		$.ajax({
			url: "/editor/newPackage",
			data: {taskName: package},
		}).then(function(data) {
			alert("Package '" + package + "' created successfully.");
			refreshPackageList();
		}).fail(function(xhr, status, error) {
			alert("Error: " + error);
		});
	}
}

function newFile() {
	var fileName = prompt("Name of new file:");
	if (fileName) {
		$.ajax({
			type: "POST",
			url: "/editor/put",
			data: {fileName: fileName, contents: JSON.stringify(""), taskName: currentPackage},
			dataType: "text",
		});
	}
}

function changePackage() {
	currentPackage = $(this).text();
	currentFileName = null;
	$("#title").text("> " + currentPackage);
	$("#packageSpecific").show();
	$("#fileSpecific").hide();
	$("#packages").children().each(function (i) {
		if ($(this).text() == currentPackage) {
			$(this).addClass("current");
		} else {
			$(this).removeClass("current");
		}
	});
	$.ajax({
		url: "/editor/list",
		data: {taskName: currentPackage},
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
	$("#title").text("> " + currentPackage + " > " + currentFileName);
	$("#fileSpecific").show();
	$("#save").val("Save " + currentFileName);
	$("#files").children().each(function (i) {
		if ($(this).text() == currentFileName) {
			$(this).addClass("current");
		} else {
			$(this).removeClass("current");
		}
	});
	$.ajax({
		url: "/editor/get",
		data: {taskName: currentPackage, fileName: currentFileName},
		dataType: "text",
	}).done(setText);
}

function refreshPackageList() {
	$.ajax({
		url: "/editor/getPackageList",
		dataType: "JSON",
	}).then(function(data) {
		$("#packages").empty();
		for (var i in data) {
			var li = document.createElement("li");
			$(li).text(data[i]);
			$(li).click(changePackage);
			$("#packages").append(li);
		}
	});
}

$(document).ready(function() {
	refreshPackageList();
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