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
			return fs.writeFile(user + ".json", JSON.stringify(data));
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
		print("in connect");
		if (status) {
			self.terminal.print("Guess we're already connected.");
		} else {
			print("let's connect to " + JSON.stringify(self.settings));
			return self.connection.connect(self.settings.host, self.settings.port).then(self.onConnect.bind(self));
		}
	});
};

Connection.prototype.onConnect = function() {
	var self = this;
	var socket = this.connection;	
	var parse = new parser.XmlStanzaParser(1);
	
	socket.write("<?xml version='1.0'?>");
	socket.write("<stream:stream to='" + self.settings.host + "' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' version='1.0'>");
	
	var started = false;
	var authenticated = false;
	
	socket.onError(function(error) {
		self.terminal.print("Socket error: " + error);
	});

	return socket.read(function(data) {
		if (!data) {
			return;
		}
		parse.parse(data).forEach(function(stanza) {
			if (stanza) {
				print(stanza);
			}
			if (stanza.name == "stream:features") {
				if (!started) {
					socket.write("<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>");
				} else if (!authenticated) {
					socket.write("<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='DIGEST-MD5'/>");
				} else {
					socket.write("<iq type='set' id='bind0'><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'><resource>js2</resource></bind></iq>");
				}
			} else if (stanza.name == "proceed") {
				if (!started) {
					started = true;
					if (self.settings.trust) {
						socket.addTrustedCertificate(self.settings.trust).then(function() { print("trusted"); }).catch(function(e) { print("no trust: " + e); });
					}
					socket.startTls().then(function() {
						parse.reset();
						socket.write("<stream:stream to='" + self.settings.host + "' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' version='1.0'>");
					}).catch(function(e) {
						print(e);
						socket.getPeerCertificate().then(function(c) { print(c); });
					});
				}
			} else if (stanza.name == "success") {
				authenticated = true;
				socket.write("<?xml version='1.0'?>");
				socket.write("<stream:stream to='" + self.settings.host + "' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' version='1.0'>");
				parse.reset();
			} else if (stanza.name == "iq") {
				if (stanza.attributes.id == "bind0") {
					socket.write("<iq type='set' id='session0'><session xmlns='urn:ietf:params:xml:ns:xmpp-session'/></iq>");
				} else if (stanza.attributes.id == "session0") {
				}
			} else if (stanza.name == "message" && stanza.attributes.type == "groupchat") {
				print("got a group chat message");
				var body;
				var delayed = false;
				for (var i in stanza.children) {
					if (stanza.children[i].name == "body") {
						body = stanza.children[i].text;
					}
					if (stanza.children[i].name == "delay") {
						delayed = true;
					}
				}

				if (!delayed) {
					print(body);
					if (body == "Hi.") {
						var from = stanza.attributes.from.split('/');
						from = from[from.length - 1];
					}
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
	});
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
					return terminal.print("We seem to have a connection?");
				});
			} else if (argv[2] == "connect") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.connect();
				});
			} else if (argv[2] == "close") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					return connection.close();
				});
			} else if (argv[2] == "set") {
				return Connection.get(credentials.user, connectionName, terminal).then(function(connection) {
					if (argv.length > 4) {
						connection.settings[argv[3]] = argv.slice(4).join(",");
					} else {
						connection.settings[argv[3]] = argv[4];
					}
					terminal.print(JSON.stringify(connection.settings, null, "  "));
				});
			}
		}
	});
}

imports.shell.register("xmpp", xmpp);
Connection.loadAll();