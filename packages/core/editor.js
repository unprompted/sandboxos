var gBackup;

$(document).ready(function() {
	gBackup = $("#editor").val();
});

function back() {
	var url = window.location.href;
	if (url.substring(url.length - "/edit".length) == "/edit") {
		url = url.substring(0, url.length - "/edit".length);
	}
	window.location.href = url;
}

function save() {
	document.getElementById("save").disabled = true;
	document.getElementById("saveAndRun").disabled = true;

	var contents = $("#editor").val();

	return $.ajax({
		type: "POST",
		url: "save",
		data: contents,
		dataType: "text",
	}).done(function() {
		gBackup = contents;
	}).fail(function(xhr, status, error) {
		alert("Unable to save: " + xhr.responseText);
	}).always(function() {
		document.getElementById("save").disabled = false;
		document.getElementById("saveAndRun").disabled = false;
	});
}

function saveAndRun() {
	save().done(back);
}

function revert() {
	$("#editor").val(gBackup);
}
