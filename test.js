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

var a = startScript("test2.js");
print("startScript => " + a);
sleep(0.5);
print("killing script");
a.kill();
print("waiting");
sleep(0.5);
print("~test.js");

'test.js';
