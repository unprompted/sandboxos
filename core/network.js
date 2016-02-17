"use strict";

function Connection() {
	this.socket = null;
	this.buffer = null;
	this.onReadCallback = null;
	this.onErrorCallback = null;
	this.tlsContext = null;
	this._exported = null;
	return this;
}

Connection.prototype.connect = function(host, port) {
	let connection = this;
	connection.close();
	connection.socket = new Socket();
	return connection.socket.connect(host, port).then(function() {
		connection.buffer = "";
		return Promise.all([
			connection.socket.onError(function(error) {
				if (connection.onErrorCallback) {
					connection.onErrorCallback(error);
				}
				connection.close();
			}),
			connection.socket.read(function(data) {
				if (connection.onReadCallback) {
					connection.onReadCallback(data);
				} else {
					connection.buffer += data;
				}
			}),
		]);
	});
};

Connection.prototype.isConnected = function() {
	return this.socket && this.socket.isConnected;
};

Connection.prototype.read = function(callback) {
	this.onReadCallback = callback;
	if (this.buffer) {
		callback(this.buffer);
	}
	this.buffer = "";
};

Connection.prototype.onError = function(callback) {
	this.onErrorCallback = callback;
};

Connection.prototype.write = function(data) {
	return this.socket.write(data);
};

Connection.prototype.close = function() {
	let socket = this.socket;
	this.socket = null;
	if (socket) {
		return socket.close();
	}
};

Connection.prototype.startTls = function() {
	return this.socket.startTls(this.tlsContext);
};

Connection.prototype.getPeerCertificate = function() {
	return this.socket.peerCertificate;
};

Connection.prototype.addTrustedCertificate = function(certificate) {
	if (!this.tlsContext) {
		this.tlsContext = new TlsContext();
	}
	return this.tlsContext.addTrustedCertificate(certificate);
};

Connection.prototype.exported = function() {
	if (!this._exported) {
		this._exported = {
			isConnected: this.isConnected.bind(this),
			connect: this.connect.bind(this),
			startTls: this.startTls.bind(this),
			write: this.write.bind(this),
			read: this.read.bind(this),
			onError: this.onError.bind(this),
			close: this.close.bind(this),
			getPeerCertificate: this.getPeerCertificate.bind(this),
			addTrustedCertificate: this.addTrustedCertificate.bind(this),
		};
	}
	return this._exported;
};

function newConnection() {
	let process = this;
	let connection = new Connection();
	process.connections.push(connection);
	return connection.exported();
}
