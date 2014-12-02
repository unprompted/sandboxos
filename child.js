print("CHILD");

function onMessage(from, message) {
	print("child received message " + JSON.stringify(from) + ": " + JSON.stringify(message));
	return 1;
}

parent.invoke({goodbye: "world"}).then(function(result) { print("RESULT: " + result); });
"child";
