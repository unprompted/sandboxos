"use strict";
var gConnections = {};

function Connection(key) {
	this.key = key;
	this.connection = null;
	this.buffer = "";
	this.nickname = null;
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

Connection.parseFrom = function(from) {
	var result = {full: from};
	var match = new RegExp(/:?(?:(.*)\!(.*)@)?(.*)/).exec(from);
	if (match) {
		result.nick = match[1];
		result.user = match[2];
		result.host = match[3];
	}
	return result;
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
	var connection = this;
	this.connection.onError(this.onError.bind(this));
	this.connection.isConnected().then(function(status) {
		if (status) {
			connection.write("PONG :" + new Date().getTime());
		}
	});
	return this.connection.read(this.onRead.bind(this));
};

Connection.prototype.printMessage = function(nick, target, message) {
	if (message.charCodeAt(0) === 1 && message.charCodeAt(message.length - 1) === 1) {
		message = message.substring(1, message.length - 1);
		var space = message.indexOf(' ');
		var command = message;
		var payload = "";
		if (space != -1) {
			command = message.substring(0, space);
			payload = message.substring(space + 1);
		}
		if (command == "ACTION") {
			this.terminal.print({styled: [
				{style: "color: #888", value: new Date().toString()},
				{style: "color: #448", value: " ["},
				{style: "color: #fff", value: target},
				{style: "color: #448", value: "] "},
				{style: "color: #44f", value: "* "},
				{style: "color: #fff", value: nick},
				{style: "color: #44f", value: " "},
				{style: "color: #ccc", value: payload},
			]});
		} else {
			this.terminal.print({styled: [
				{style: "color: #888", value: new Date().toString()},
				{style: "color: #448", value: " ["},
				{style: "color: #fff", value: target},
				{style: "color: #448", value: "] "},
				{style: "color: #44f", value: " <"},
				{style: "color: #fff", value: nick},
				{style: "color: #44f", value: "> "},
				{style: "color: #4f4", value: "CTCP "},
				{style: "color: #ccc", value: message},
			]});
		}
	} else {
		this.terminal.print({styled: [
			{style: "color: #888", value: new Date().toString()},
			{style: "color: #448", value: " ["},
			{style: "color: #fff", value: target},
			{style: "color: #448", value: "] "},
			{style: "color: #44f", value: "<"},
			{style: "color: #fff", value: nick},
			{style: "color: #44f", value: "> "},
			{style: "color: #ccc", value: message},
		]});
	}
};

Connection.prototype.onCtcp = function(from, to, message) {
	if (message == "VERSION") {
		this.write("NOTICE " + from.nick + " :\u0001VERSION SandboxOS IRC 0.1\u0001");
	}
}

Connection.prototype.onReadLine = function(line) {
	var colon = line.indexOf(' :');
	var argv;
	if (colon != -1) {
		argv = line.substring(0, colon).split(" ");
		argv.push(line.substring(colon + 2));
	} else {
		argv = line.split(" ");
	}
	if (parseInt(argv[1]) || argv[1] == "NICK") {
		this.nickname = argv[2];
	}
	if (argv[0] == "PING") {
		this.write("PONG :" + argv[1]);
	} else if (argv[1] == "PRIVMSG") {
		var from = Connection.parseFrom(argv[0]);
		this.printMessage(from.nick, argv[2], argv[3]);
		if (argv[3].charCodeAt(0) === 1 && argv[3].charCodeAt(argv[3].length - 1) === 1) {
			this.onCtcp(from, argv[2], argv[3].substring(1, argv[3].length - 1));
		}
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
		} else if (argv[0] == "msg") {
			return Connection.get(networkCredentials, connectionName, terminal).then(function(connection) {
				return connection.write("PRIVMSG " + argv[1] + " :" + argv.slice(2).join(" ")).then(function() {
					connection.printMessage(connection.nickname, argv[1], argv.slice(2).join(" "));
				});
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