print("test.js");
startScript("test2.js");
print("~test.js");

function onMessage(message) {
	print('onMessage(' + JSON.stringify(message) + ')');
	sleep(1.0);
	return ['yo'];
}

'test.js';
