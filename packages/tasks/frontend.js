$(document).ready(function() {
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

function refresh() {
	$("#tasks").empty();
	$.ajax({
		url: "/tasks/get",
		dataType: "json"
	}).then(function(data) {
		var tr = document.createElement("tr");

		var th = document.createElement("th");
		$(th).text("Package");
		$(tr).append(th);

		th = document.createElement("th");
		$(th).text("Status");
		$(tr).append(th);

		th = document.createElement("th");
		$(th).text("Actions");
		$(tr).append(th);

		th = document.createElement("th");
		$(th).text("Edit");
		$(tr).append(th);

		$("#tasks").append(tr);

		for (var i in data.packages) {
			var package = data.packages[i];
			var tr = document.createElement("tr");

			var td = document.createElement("td");
			if (data.tasks[package]
				&& data.tasks[package].manifest
				&& data.tasks[package].manifest.httpd
				&& data.tasks[package].manifest.httpd.root) {
				var a = document.createElement("a");
				$(a).attr("href", data.tasks[package].manifest.httpd.root);
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

				var td = document.createElement("td");
				$(tr).append(td);
			} else if (data.tasks[package]) {
				var td = document.createElement("td");
				$(td).text("running");
				$(tr).append(td);

				var td = document.createElement("td");
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
				$(tr).append(td);
			} else {
				var td = document.createElement("td");
				$(td).text("not running");
				$(tr).append(td);

				var td = document.createElement("td");
				var start = document.createElement("input");
				$(start).attr("type", "button");
				$(start).attr("value", "Start");
				$(start).data("taskName", package);
				$(start).click(startTask);
				$(td).append(start);
				$(tr).append(td);
			}

			var td = document.createElement("td");
			var a = document.createElement("a");
			$(a).text("edit");
			$(a).attr("href", "/editor/" + package + "/");
			$(td).append(a);
			$(tr).append(td);

			$("#tasks").append(tr);
		}
	}).fail(function(xhr, error, message) {
		console.debug([error, message]);
	});
}