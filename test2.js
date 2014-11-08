print("test2.js");
try {
	var p = syscall({command: "sum", args: [1, 2, 3]})
	.then(function(result) {
		print("response to message => " + result);
		return result;
	})
	.catch(function(result) {
		print("catch?");
	});
	print("promise => " + JSON.stringify(p));
} catch (e) {
	print("what?");
}
print("~test2.js");
'test2.js';
