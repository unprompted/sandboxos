print("test2.js");
send({command: "sum", args: [1, 2, 3]}, function(result) {
	print("response to message => " + result);
});
print("~test2.js");
'test2.js';
