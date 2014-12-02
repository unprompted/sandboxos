print("Hello");

var task = new Task();
task.execute("child.js");
task.start();

function onMessage(from, message) {
	console.debug("parent received message " + JSON.stringify(from) + ": " + JSON.stringify(message));
}

task.invoke({hello: "world"});
"test"
