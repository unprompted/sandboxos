print("starting test");
var socket = new Socket();
print("socket: " + socket);
print("bind: " + socket.bind("0.0.0.0", 12345));
socket.listen(8, function() {
	var client = socket.accept();
	print(client);
	client.read(function(data) {
		print("received => " + JSON.stringify(data));
		client.write(data).then(function() { print("echo'd"); });
		if (!data) {
			print("closed");
			client.close();
		}
	});
});
