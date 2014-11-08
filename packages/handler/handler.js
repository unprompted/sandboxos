var waiting = [];
var index = 0;
var messages = [];

function onMessage(from, message) {
  	if (message.request.uri == "/handler") {
	  parent.invoke({
		  to: "system",
		  action: "get",
		  fileName: "index.html",
	  }).then(function(data) {
		  parent.invoke({
			  to: "httpd",
			  response: "HTTP/1.0 200 OK\nContent-Type: text/html\nConnection: close\n\n" + data,
			  messageId: message.messageId,
		  });
	  });
	} else if (message.request.uri == "/handler/send") {
	  messages[index++] = message.request.body;
	  	for (var i in waiting) {
		  parent.invoke({
			  to: "httpd",
			  response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + JSON.stringify({index: index, message: message.request.body}),
			  messageId: waiting[i],
		  });
		}
	  	waiting.slice(0);
		  parent.invoke({
			  to: "httpd",
			  response: "HTTP/1.0 200 OK\nContent-Type: text/html\nConnection: close\n\nOK",
			  messageId: message.messageId,
		  });
	} else if (message.request.uri == "/handler/receive") {
	  	var haveIndex = parseInt(message.request.body);
	  	if (haveIndex + 1 < index) {
		  parent.invoke({
			  to: "httpd",
			  response: "HTTP/1.0 200 OK\nContent-Type: text/plain\nConnection: close\n\n" + JSON.stringify({index: haveIndex + 1, message: messages[haveIndex + 1]}),
			  messageId: message.messageId,
		  });
		} else {
			waiting.push(message.messageId);
		}
	}
	return true;
}
