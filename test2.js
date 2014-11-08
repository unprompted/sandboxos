print("test2.js");
send({text: "this is a message", array: [1, 2, 3]}, function bleh(result) {
	print("response to message => " + JSON.stringify(result));
});
'test2.js';
