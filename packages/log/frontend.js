function log(line) {
	var textarea = $("#log");
	textarea.append(line + "\n");
	textarea.scrollTop(textarea[0].scrollHeight - textarea.height());
}

function requestLog(start) {
	$.ajax({
		url: "/log/get",
		data: JSON.stringify(start),
		method: "POST",
		dataType: "json",
	}).then(function(data) {
		for (var i = 0; i < data.messages.length; i++) {
			log(data.messages[i]);
		}
		requestLog(data.next);
	}).fail(function(xhr, error, status) {
		log("ERROR: " + JSON.stringify([error, status]) + "\n");
	});
}

$(document).ready(function() {
	requestLog();
});