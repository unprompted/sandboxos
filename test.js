print("PARENT");

var task = new Task();
task.execute("child.js");
task.start();

function onMessage(from, message) {
	print("parent received message " + JSON.stringify(from) + ": " + JSON.stringify(message));
	return 2;
}

task.invoke({hello: "world"}).then(function(result) { print("RESULT: " + result); });
"parent";
