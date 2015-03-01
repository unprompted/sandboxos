"use strict";
var md5 = require("md5");
var base64 = require("base64");
var parser = require("parser");

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
};

function Connection(key) {
	this.key = key;
	this.connection = null;
	this.terminal = new Backlog();
	this.buffer = "";
	this.settings = {};
	this.parser = new parser.XmlStanzaParser(1);
	this.authenticationStarted = false;
	this.authenticationFinished = false;
	return this;
}

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
			return fs.writeFile(user + ".json", JSON.stringify(data, null, "\t"));
		});
	});
};

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
		throw new Error("No user in saved settings.");
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
};

Connection.prototype.connect = function() {
	var self = this;
	return self.connection.isConnected().then(function(status) {
		if (status) {
			self.terminal.print("Guess we're already connected.");
			self.authenticationStarted = true;
			self.authenticationFinished = true;
			self.parser = new parser.XmlStanzaParser(0);
			self.refresh();
		} else {
			return self.connection.connect(self.settings.host, self.settings.port).then(self.onConnect.bind(self));
		}
	});
};

Connection.prototype.refresh = function() {
	var self = this;
	
	self.connection.onError(function(error) {
		self.terminal.print("Socket error: " + error);
	});
	self.connection.read(self.onRead.bind(this));
};

Connection.prototype.onRead = function(data) {
	var self = this;
	print(data);
	if (!data) {
		return;
	}
	self.parser.parse(data).forEach(function(stanza) {
		if (stanza) {
			print(stanza);
		}
		if (stanza.name == "stream:features") {
			if (!self.authenticationStarted) {
				socket.write("<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>");
			} else if (!self.authenticationFinished) {
				socket.write("<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='DIGEST-MD5'/>");
			} else {
				socket.write("<iq type='set' id='bind0'><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'><resource>js2</resource></bind></iq>");
			}
		} else if (stanza.name == "proceed") {
			if (!self.authenticationStarted) {
				self.authenticationStarted = true;
				if (self.settings.trust) {
					socket.addTrustedCertificate(self.settings.trust).then(function() { print("trusted"); }).catch(function(e) { print("no trust: " + e); });
				}
				socket.startTls().then(function() {
					self.parser.reset();
					socket.write("<stream:stream to='" + self.settings.host + "' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' version='1.0'>");
				}).catch(function(e) {
					print(e);
					socket.getPeerCertificate().then(function(c) { print(c); });
				});
			}
		} else if (stanza.name == "success") {
			self.authenticationFinished = true;
			socket.write("<?xml version='1.0'?>");
			socket.write("<stream:stream to='" + self.settings.host + "' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' version='1.0'>");
			parse.reset();
		} else if (stanza.name == "iq") {
			if (stanza.attributes.id == "bind0") {
				socket.write("<iq type='set' id='session0'><session xmlns='urn:ietf:params:xml:ns:xmpp-session'/></iq>");
			} else if (stanza.attributes.id == "session0") {
			}
		} else if (stanza.name == "message") {
			var body;
			var delayed = false;
			var type = stanza.attributes.type;
			for (var i in stanza.children) {
				if (stanza.children[i].name == "body") {
					body = stanza.children[i].text;
				}
				if (stanza.children[i].name == "delay") {
					delayed = true;
				}
			}

			if (!delayed && body) {
				var from = stanza.attributes.from;
				var to = stanza.attributes.to;
				self.terminal.print(type + " => " + to + " <" + from + "> " + body);
			}
		} else if (stanza.name == "challenge") {
			var challenge = base64.decode(stanza.text);
			var parts = challenge.split(',');
			challenge = {};
			for (var i = 0; i < parts.length; i++) {
				var equals = parts[i].indexOf("=");
				if (equals != -1) {
					var key = parts[i].substring(0, equals);
					var value = parts[i].substring(equals + 1);
					if (value.length > 2 && value.charAt(0) == '"' && value.charAt(value.length - 1) == '"') {
						value = value.substring(1, value.length - 1);
					}
					challenge[key] = value;
				} else {
					print(parts[i]);
				}
			}
			if (challenge.rspauth) {
				socket.write("<response xmlns='urn:ietf:params:xml:ns:xmpp-sasl'/>");
			} else {
				var realm = self.settings.host;
				var cnonce = base64.encode(new Date().toString());
				var x = self.settings.username + ":" + self.settings.host + ":" + self.settings.password;
				var y = md5.raw_md5(x);
				var a1 = y + ":" + challenge.nonce + ":" + cnonce;
				var digestUri = "xmpp/" + self.settings.host;
				var a2 = "AUTHENTICATE:" + digestUri;
				var ha1 = md5.md5(a1);
				var ha2 = md5.md5(a2);
				var nc = "00000001";
				var kd = ha1 + ":" + challenge.nonce + ":" + nc + ":" + cnonce + ":" + challenge.qop + ":" + ha2;
				var response = md5.md5(kd);
				var encoded = base64.encode('username="' + self.settings.username + '",realm="' + self.settings.host + '",nonce="' + challenge.nonce + '",cnonce="' + cnonce + '",nc=' + nc + ',qop=' + challenge.qop + ',digest-uri="' + digestUri + '",response=' + response + ',charset=utf-8');
				socket.write("<response xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>" + encoded + "</response>");
			}
		}
	});
}

Connection.prototype.onConnect = function() {
	var self = this;
	var socket = this.connection;
	socket.write("<?xml version='1.0'?>");
	socket.write("<stream:stream to='" + self.settings.host + "' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' version='1.0'>");
	self.refresh();
}

Connection.prototype.join = function(group) {
	this.connection.write("<presence to='" + group + "'><priority>1</priority><x xmlns='http://jabber.org/protocol/muc'/></presence>");
}

function xmpp(terminal, argv, credentials) {
	return imports.auth.verifyCredentials(credentials).then(function(verified) {
		if (argv[1] == "save") {
			return Connection.saveAll(credentials.user).then(function() {
				terminal.print("Connections saved.");
			});
		} else {
			var connectionName = argv[1];
			if (argv[2] == "status") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.connection.isConnected();
				}).then(function(connected) {
					return terminal.print(connected ? "We seem to have a connection?" : "We don't seem to be connected.");
				});
			} else if (argv[2] == "connect") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.connect();
				});
			} else if (argv[2] == "close") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.connection.close();
				});
			} else if (argv[2] == "set") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					if (argv.length > 4) {
						connection.settings[argv[3]] = argv.slice(4).join(",");
					} else {
						connection.settings[argv[3]] = argv[4];
					}
					return Connection.saveAll(credentials.user).then(function() {
						terminal.print("Settings updated.");
					});
				});
			} else if (argv[2] == "msg" || argv[2] == "groupmsg") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					var to = argv[3];
					var message = argv.slice(4).join(" ");
					var type = argv[2] == "msg" ? "chat" : "groupchat";
					if (to.indexOf("@") == -1) {
						to = to + "@" + connection.settings.host;
					}
					return connection.connection.write("<message type='" + type + "' to='" + to + "'><body>" + message + "</body></message>");
				});
			} else if (argv[2] == "join") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.join(argv[3]);
				});
			}
		}
	});
}

imports.shell.register("xmpp", xmpp);
Connection.loadAll();