"use strict";
var gConnections = {};

function Backlog() {
	this.data = [];
	return this;
}

Backlog.prototype.print = function(data) {
	this.data.push(data);
};

Backlog.prototype.playBack = function(terminal) {
	for (var i = 0; i < this.data.length; i++) {
		terminal.print(this.data[i]);
	}
	this.data.length = 0;
}

function Connection(key) {
	this.key = key;
	this.connection = null;
	this.terminal = new Backlog();
	this.buffer = "";
	this.nickname = null;
	this.settings = {};
	return this;
}

Connection.loadAll = function() {
	return imports.filesystem.getPackageData().then(function(fs) {
		return fs.listDirectory(".").then(function(contents) {
			var promises = [];
			for (var i = 0; i < contents.length; i++) {
				promises.push(fs.readFile(contents[i]).then(function(data) {
					return Connection.loadAllFromString(JSON.parse(data));
				}));
			}
			return Promise.all(promises);
		});
	});
};

Connection.loadAllFromString = function(data) {
	if (!data.user) {
		throw new Error("No user is saved settings.");
	}
	var promises = [];
	if (data.connections) {
		for (var name in data.connections) {
			var settings = data.connections[name];
			promises.push(Connection.get(data.user, name, null).then(function(connection) {
				connection.settings = settings;
				return connection.connect();
			}));
		}
	}
	return Promise.all(promises);
}

Connection.saveAll = function(user) {
	var data = {};
	data.user = user;
	data.connections = {};
	for (var key in gConnections) {
		var parts = JSON.parse(key);
		if (parts[0] === user) {
			data.connections[parts[1]] = gConnections[key].settings;
		}
	}
	return imports.filesystem.getPackageData().then(function(fs) {
		return fs.ensureDirectoryTreeExists(".").then(function() {
			return fs.writeFile(user + ".json", JSON.stringify(data));
		});
	});
};

Connection.get = function(user, name, terminal) {
	var key = JSON.stringify([user, name]);
	return imports.network.getConnection(key).then(function(connection) {
		if (!gConnections[key]) {
			gConnections[key] = new Connection(key);
			gConnections[key].user = user;
			gConnections[key].name = name;
		}
		gConnections[key].connection = connection;
		if (gConnections[key].terminal.playBack) {
			gConnections[key].terminal.playBack(terminal);
		}
		if (terminal) {
			gConnections[key].terminal = terminal;
		}
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

Connection.prototype.setupInternal = function() {
	this.connection.onError(this.onError.bind(this));
	return this.connection.read(this.onRead.bind(this));
};

Connection.prototype.connect = function(host, port) {
	var connection = this;
	this.settings.host = host || this.settings.host;
	this.settings.port = parseInt(port || this.settings.port || 6667);
	return this.connection.isConnected().then(function(status) {
		if (status) {
			connection.terminal.print("Already connected.  Refreshing connection.");
			connection.refresh();
		} else {
			connection.terminal.print("Connecting...");
			return connection.connection.connect(connection.settings.host, connection.settings.port).then(function() {
				return connection.setupInternal();
			}).then(function() {
				return connection.write("NICK " + connection.settings.nicknames.split(",")[0]);
			}).then(function() {
				return connection.write("USER " + connection.user + " " + connection.user + " localhost :" + connection.user);
			});
		}
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
	return this.connection.isConnected().then(function(status) {
		if (status) {
			connection.write("PONG :" + new Date().getTime());
			connection.write("VERSION");
		}
		return connection.setupInternal();
	});
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
	print(line);
	
	var from;
	var remaining = line;
	if (remaining[0] == ':') {
		var space = remaining.indexOf(' ');
		from = Connection.parseFrom(remaining.substring(1, space));
		remaining = remaining.substring(space + 1);
	}
	
	var colon = remaining.indexOf(' :');
	var argv;
	if (colon != -1) {
		argv = remaining.substring(0, colon).split(" ");
		argv.push(remaining.substring(colon + 2));
	} else {
		argv = remaining.split(" ");
	}

	var command = argv[0];
	var numeric = parseInt(command);
	if (numeric) {
		this.nickname = argv[1];
		
		if (numeric == 1) {
			if (this.settings.autoJoinChannels) {
				var channels = this.settings.autoJoinChannels.split(",");
				this.write("JOIN " + channels.join(","));
			}
		}
		this.terminal.print(line);
	} else {
		if (command == "PING") {
			this.write("PONG :" + argv[1]);
		} else if (command == "PRIVMSG") {
			this.printMessage(from.nick, argv[1], argv[2]);
			if (argv[2].charCodeAt(0) === 1 && argv[2].charCodeAt(argv[2].length - 1) === 1) {
				this.onCtcp(from, argv[1], argv[2].substring(1, argv[2].length - 1));
			}
		} else if (command == "NICK") {
			if (from.nick == this.nick) {
				this.nickname = argv[1];
			}
			this.terminal.print(line);
		} else {
			this.terminal.print(line);
		}
	}
};

function irc(terminal, argv, credentials) {
	return imports.auth.verifyCredentials(credentials).then(function(verified) {
		if (argv[1] == "save") {
			return Connection.saveAll(credentials.user).then(function() {
				terminal.print("Connections saved.");
			});
		} else {
			var connectionName = argv[1];
			argv = argv.slice(2);
	
			if (argv[0] == "connect") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.connect(argv[1], argv[2]);
				}).then(function() {
					terminal.print("Connected to " + connection.settings.host + ":" + connection.settings.port);
				});
			} else if (argv[0] == "msg") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.write("PRIVMSG " + argv[1] + " :" + argv.slice(2).join(" ")).then(function() {
						connection.printMessage(connection.nickname, argv[1], argv.slice(2).join(" "));
					});
				});
			} else if (argv[0] == "send") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.write(argv[1]);
				}).then(function() {
					terminal.print("=> " + argv[1]);
				});
			} else if (argv[0] == "close") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.close();
				}).then(function() {
					terminal.print("Closed connection.");
				});
			} else if (argv[0] == "status") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.status();
				});
			} else if (argv[0] == "refresh") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.refresh().then(function() {
						terminal.print("Connection refreshed.");
					});
				});
			} else if (argv[0] == "set") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					if (argv.length > 3) {
						connection.settings[argv[1]] = argv.slice(2).join(",");
					} else {
						connection.settings[argv[1]] = argv[2];
					}
					terminal.print(JSON.stringify(connection.settings, null, "  "));
				});
			} else {
				terminal.print("Unsupported IRC command: " + argv[0]);
			}
		}
	});
}

imports.shell.register("irc", irc);
Connection.loadAll().catch(function(e) {
	print("Error loading connections: " + e);
});