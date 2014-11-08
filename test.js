print("test.js");

function onMessage(message) {
	print("test.js onMessage => " + JSON.stringify(message));
	if (message.command == 'sum') {
		var total = 0;
		for (var i = 0; i < message.args.length; i++) {
			total += message.args[i];
		}
		return total;
	}
	return "???";
}

var a = startScript("test2.js");
print("startScript => " + a);

print("SLEEP starting");
sleep(0.5).then(function() { print("SLEEP finished!"); });
print("SLEEP started");

a.invoke(['test', 1, 2, 3.0]).then(function(x) { print("response => " + x); });

print("~test.js");
print("parent = " + parent);
sleep(2.0).then(function() { a.kill() });
'test.js';

writeFile("writtenFile.txt", "Hello, world!");
print(readFile("writtenFile.txt"));

sleep(3.0).then(exit);
