"use strict";
var gConnections = {};

function Connection(key) {
	this.key = key;
	this.socket = null;
	this.buffer = "";
	this.onReadCallback = null;
	this.onErrorCallback = null;
	return this;
}

Connection.prototype.connect = function(host, port) {
	var connection = this;
	if (this.socket) {
		this.socket.close();
	}
	var socket = new Socket();
	this.socket = socket;
	
	return socket.connect(host, port).then(function() {
		connection.buffer = "";
		return Promise.all([
			socket.onError(function(error) {
				print("Socket error: " + error);
				if (connection.onErrorCallback) {
					connection.onErrorCallback(error);
				}
				socket.close();
				connection.socket = null;
			}),
			socket.read(function(data) {
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
}

Connection.prototype.write = function(data) {
	return this.socket.write(data);
};

Connection.prototype.close = function() {
	var result;
	if (this.socket) {
		result = this.socket.close();
		this.socket = null;
	}
	delete gConnections[this.key];
	return result;
};

Connection.prototype.export = function() {
	if (!this._export) {
		this._export = {
			isConnected: this.isConnected.bind(this),
			connect: this.connect.bind(this),
			write: this.write.bind(this),
			read: this.read.bind(this),
			onError: this.onError.bind(this),
			close: this.close.bind(this),
		};
	}
	return this._export;
};

function getConnection(connectionName) {
	var key = JSON.stringify([this.taskName, connectionName]);
	if (!gConnections[key]) {
		gConnections[key] = new Connection(key);
	}
	return gConnections[key].export();
}

exports = {
	getConnection: getConnection,
};