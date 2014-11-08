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

function reload() {
	$("#iframe")[0].src = "/" + currentPackage;
}

function setText(text) {
	if (cm) {
		cm.setValue(text);
	} else {
		$("#edit").val(text);
	}
}

function changePackage() {
	currentPackage = $(this).text();
	currentFileName = null;
	$("#title").text("Editor - " + currentPackage);
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
	$("#title").text("Editor - " + currentPackage + " - " + currentFileName);
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

$(document).ready(function() {
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
	var editor = document.getElementById("edit");
	cm = CodeMirror.fromTextArea(editor, {indentWithTabs: true, theme: 'lesser-dark', indentUnit: 4, smartIndent: false});
});
