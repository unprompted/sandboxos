print("test.js");

print("test promise");
var p = new Promise(function(resolve, reject) {
	print("yo");
	print(resolve);
	print(reject);
	print(resolve("resolved"));
})
.then(function(result) { print("then: " + result); })
.catch(function(e) { print("catch: " + e); });
print(p);
print("end test promise");

function onMessage(message) {
	print("onMessage => " + JSON.stringify(message));
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
sleep(0.2);
a.invoke(['test', 1, 2, 3.0]).then(function(x) { print("response => " + x); });
print("~test.js");
print("parent = " + parent);
sleep(0.5);
a.kill();
'test.js';
