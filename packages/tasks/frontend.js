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

function createTaskActionDiv(name, taskName, action) {
	var div = document.createElement("div");
	$(div).addClass("action");
	var button = document.createElement("input");
	$(button).attr("type", "button");
	$(button).attr("value", name);
	$(button).data("taskName", taskName);
	$(button).click(action);
	$(div).append(button);
	return div;
}

function handleNewData(data) {
	$("#tasks").empty();

	for (var i in data.packages) {
		var package = data.packages[i];
		var div = document.createElement("div");
		$(div).addClass("package");

		var nameDiv = document.createElement("div");
		$(nameDiv).addClass("packageName");
		if (data.tasks[package]
			&& data.tasks[package].manifest
			&& data.tasks[package].manifest.href) {
			var a = document.createElement("a");
			$(a).attr("href", data.tasks[package].manifest.href);
			$(a).text(package);
			$(nameDiv).append(a);
		} else {
			$(nameDiv).text(package);
		}
		$(div).append(nameDiv);

		var descriptionDiv = document.createElement("div");
		$(descriptionDiv).addClass("description");
		if (data.tasks[package] && data.tasks[package].manifest) {
			$(descriptionDiv).text(data.tasks[package].manifest.description || "Package has no description.");
		} else {
			$(descriptionDiv).text("No description available.");
		}
		$(div).append(descriptionDiv);

		var statusDiv = document.createElement("div");
		if (package == "system") {
			$(statusDiv).text("always running");
		} else if (data.tasks[package]) {
			$(statusDiv).text(data.tasks[package].status);
		} else {
			$(statusDiv).text("stopped");
		}
		$(div).append(statusDiv);

		if (data.tasks[package] && data.tasks[package].error) {
			var errorDiv = document.createElement("div");
			$(errorDiv).text(data.tasks[package].error);
			$(errorDiv).addClass("error");
			$(div).append(errorDiv);
		}

		var actionsDiv = document.createElement("div");
		$(actionsDiv).addClass("actions");

		var editDiv = document.createElement("div");
		$(editDiv).addClass("action");
		var edit = document.createElement("a");
		$(edit).text("edit");
		$(edit).attr("href", "/editor/" + package + "/");
		$(editDiv).append(edit);
		$(actionsDiv).append(editDiv);

		if (package != "system") {
			if (data.tasks[package]) {
				$(actionsDiv).append(createTaskActionDiv("Stop", package, stopTask));
				$(actionsDiv).append(createTaskActionDiv("Restart", package, restartTask));
			} else {
				$(actionsDiv).append(createTaskActionDiv("Start", package, startTask));
			}
		}
		$(div).append(actionsDiv);

		if (data.tasks[package]) {
			var statisticsDiv = document.createElement("div");
			$(statisticsDiv).addClass("statistics");
			$(statisticsDiv).text("Statistics: " + JSON.stringify(data.tasks[package].statistics, null, "\t"));
			$(div).append(statisticsDiv);
		}

		$("#tasks").append(div);
	}

	div = document.createElement("div");
	$(div).addClass("package");
	var aDiv = document.createElement("a");
	$(aDiv).text("Create a new task");
	$(aDiv).attr("href", "#");
	$(aDiv).addClass("link");
	$(aDiv).click(function() {
		var name = prompt("Enter name of new task:");
		if (name) {
			window.location.href = "/editor/" + encodeURIComponent(name) + "/?clone=helloworld";
		}
	});
	$(div).append(aDiv);
	$("#tasks").append(div);
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