print("test2.js");
print("parent = " + parent);
try {
	var p = parent.invoke({command: "sum", args: [1, 2, 3]})
	.then(function(result) {
		print("response to message => " + result);
		return result;
	})
	.catch(function(result) {
		print("catch?");
	});
	print("promise => " + JSON.stringify(p));
} catch (e) {
	print("what?: " + e);
}

function onMessage(message) {
	print("test2.js got message => " + JSON.stringify(message));
	return "OK";
}

while (true) {
	var r = readLine();
	JSON.stringify(undefined);
	print(JSON.stringify(undefined));
	print("read " + JSON.stringify(r));
	if (r == 'exit' || r == undefined) {
		exit();
	}
}

print("~test2.js");
'test2.js';
