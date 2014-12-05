$(document).ready(function() {
	watchForChanges();
	refresh();
});

function startTask() {
	var taskName = $(this).data("taskName");
	$.ajax({
		url: "/tasks/start",
		data: {taskName: taskName},
	});
}

function restartTask() {
	var taskName = $(this).data("taskName");
	$.ajax({
		url: "/tasks/restart",
		data: {taskName: taskName},
	});
}

function stopTask() {
	var taskName = $(this).data("taskName");
	$.ajax({
		url: "/tasks/stop",
		data: {taskName: taskName},
	});
}

function watchForChanges() {
	$.ajax({
		url: "/tasks/changes",
		dataType: "json",
	}).then(handleNewData).then(watchForChanges)
	.fail(function(xhr, error, status) {
		console.debug([error, status]);
		watchForChanges();
	});
}

function showError(error) {
	return function() {
		alert(error.stackTrace + "\n\n" + error.fileName + ":" + error.lineNumber + ":\n" + error.sourceLine);
	}
}

function handleNewData(data) {
	console.debug(data);
	$("#tasks").empty();

	var tr = document.createElement("tr");
	var columns = ["Package", "Status", "Actions", "Statistics"];
	for (var i in columns) {
		var th = document.createElement("th");
		$(th).text(columns[i]);
		$(tr).append(th);
	}
	$("#tasks").append(tr);

	for (var i in data.packages) {
		var package = data.packages[i];
		var tr = document.createElement("tr");

		var td = document.createElement("td");
		if (data.tasks[package]
			&& data.tasks[package].manifest
			&& data.tasks[package].manifest.href) {
			var a = document.createElement("a");
			$(a).attr("href", data.tasks[package].manifest.href);
			$(a).text(package);
			$(td).append(a);
		} else {
			$(td).text(package);
		}
		$(tr).append(td);

		if (package == "system") {
			var td = document.createElement("td");
			$(td).text("always running");
			$(tr).append(td);
		} else if (data.tasks[package]) {
			var td = document.createElement("td");
			$(td).text(data.tasks[package].status);
			if (data.tasks[package].error) {
				$(td).click(showError(data.tasks[package].error));
				$(td).css({color: "red", cursor: "pointer"});
			}
			$(tr).append(td);
		} else {
			var td = document.createElement("td");
			$(td).text("not running");
			$(tr).append(td);
		}

		// Actions
		var td = document.createElement("td");
		var a = document.createElement("a");
		$(a).text("edit");
		$(a).attr("href", "/editor/" + package + "/");
		$(td).append(a);
		$(td).append(" ");
		if (data.tasks[package]) {
			var stop = document.createElement("input");
			$(stop).attr("type", "button");
			$(stop).attr("value", "Stop");
			$(stop).data("taskName", package);
			$(stop).click(stopTask);
			$(td).append(stop);

			var restart = document.createElement("input");
			$(restart).attr("type", "button");
			$(restart).attr("value", "Restart");
			$(restart).data("taskName", package);
			$(restart).click(restartTask);
			$(td).append(restart);
		} else {
			var start = document.createElement("input");
			$(start).attr("type", "button");
			$(start).attr("value", "Start");
			$(start).data("taskName", package);
			$(start).click(startTask);
			$(td).append(start);
		}
		$(tr).append(td);

		var td = document.createElement("td");
		if (data.tasks[package]) {
			$(td).append(JSON.stringify(data.tasks[package].statistics));
		}
		$(tr).append(td);

		$("#tasks").append(tr);
	}
}

function refresh() {
	$("#tasks").empty();
	$.ajax({
		url: "/tasks/get",
		dataType: "json"
	}).then(handleNewData).fail(function(xhr, error, message) {
		console.debug([error, message]);
	});
}
