$(document).ready(function() {
	refresh();
});

function refresh() {
	$("#tasks").empty();
	$.ajax({
		url: "/tasks/get",
		dataType: "json"
	}).then(function(data) {
		console.debug(data);
		var tr = document.createElement("tr");

		var th = document.createElement("th");
		$(th).text("Package");
		$(tr).append(th);

		th = document.createElement("th");
		$(th).text("Status");
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

			var td = document.createElement("td");
			$(td).text(data.tasks[package] ? "running" : "not running");
			$(tr).append(td);

			$("#tasks").append(tr);
		}
	}).fail(function(xhr, error, message) {
		console.debug([error, message]);
	});
}