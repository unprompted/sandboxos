print("CHILD");

function onMessage(from, message) {
	console.debug("onMessage");
	//console.debug("child received message " + JSON.stringify(from) + ": " + JSON.stringify(message));
	return 1;
}

//parent.invoke({hello: "world"});
