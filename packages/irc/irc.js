"use strict";
var gConnections = {};

function Connection(key) {
	this.key = key;
	this.connection = null;
	this.buffer = "";
	return this;
}

Connection.get = function(credentials, name, terminal) {
	var key = JSON.stringify([credentials.user, name]);
	return imports.network.getConnection(credentials, name).then(function(connection) {
		if (!gConnections[key]) {
			gConnections[key] = new Connection(key);
		}
		gConnections[key].connection = connection;
		gConnections[key].terminal = terminal;
		return gConnections[key];
	});
};

Connection.prototype.connect = function(host, port) {
	var connection = this;
	return this.connection.connect(host, port).then(function() {
		return connection.refresh();
	});
};

Connection.prototype.onRead = function(data) {
	this.buffer += data;
	while (true) {
		var lineEnd = this.buffer.indexOf('\r\n');
		var nextStart = lineEnd + 2;
		if (lineEnd == -1) {
			lineEnd = this.buffer.indexOf('\n');
			nextStart = lineEnd + 1;
		}
		if (lineEnd != -1) {
			var line = this.buffer.substring(0, lineEnd);
			this.onReadLine(line);
			this.buffer = this.buffer.substring(nextStart);
		} else {
			break;
		}
	}
};

Connection.prototype.onError = function(data) {
	this.terminal.print("Read error: " + data);
};

Connection.prototype.write = function(command) {
	return this.connection.write(command + "\r\n");
};

Connection.prototype.status = function() {
	var connection = this;
	return this.connection.isConnected().then(function(status) {
		return connection.terminal.print("Status: " + (status ? "connected" : "not connected"));
	});
};


Connection.prototype.refresh = function() {
	this.connection.onError(this.onError.bind(this));
	return this.connection.read(this.onRead.bind(this));
};

Connection.prototype.onReadLine = function(line) {
	var colon = line.indexOf(' :');
	var argv;
	if (colon != -1) {
		argv = line.substring(0, colon).split(" ");
		argv.push(line.substring(colon + 2));
	} else {
		argv = line.split(" ");
	}
	if (argv[0] == "PING") {
		this.write("PONG :" + argv[1]);
	} else {
		this.terminal.print(line);
	}
};

function irc(terminal, argv, credentials) {
	return imports.auth.transferCredentials(credentials, "network").then(function(networkCredentials) {
		var connectionName = argv[1];
		argv = argv.slice(2);

		if (argv[0] == "connect") {
			return Connection.get(networkCredentials, connectionName, terminal).then(function(connection) {
				return connection.connect(argv[1], parseInt(argv[2]) || 6667);
			}).then(function() {
				terminal.print("Connected to " + argv[1] + ":" + (parseInt(argv[2]) || 6667));
			});
		} else if (argv[0] == "send") {
			return Connection.get(networkCredentials, connectionName, terminal).then(function(connection) {
				return connection.write(argv[1]);
			}).then(function() {
				terminal.print("=> " + argv[1]);
			});
		} else if (argv[0] == "close") {
			return Connection.get(networkCredentials, connectionName, terminal).then(function(connection) {
				return connection.close();
			}).then(function() {
				terminal.print("Closed connection.");
			});
		} else if (argv[0] == "status") {
			return Connection.get(networkCredentials, connectionName, terminal).then(function(connection) {
				return connection.status();
			});
		} else if (argv[0] == "refresh") {
			return Connection.get(networkCredentials, connectionName, terminal).then(function(connection) {
				return connection.refresh().then(function() {
					terminal.print("Connection refreshed.");
				});
			});
		} else {
			terminal.print("Unsupported IRC command: " + argv[0]);
		}
	});
}

imports.shell.register("irc", irc);