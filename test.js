print("test.js");
function onMessage(message) {
	if (message.command == 'sum') {
		var total = 0;
		for (var i = 0; i < message.args.length; i++) {
			total += message.args[i];
		}
		return total;
	}
}

startScript("test2.js");
print("~test.js");

'test.js';
