#include "PacketStream.h"

#include <cstring>
#include <iostream>

PacketStream::PacketStream()
:	_onReceive(0),
	_onReceiveUserData(0) {
}

PacketStream::~PacketStream() {
	_onReceive = 0;
	_onReceiveUserData = 0;
	if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&_stream))) {
		uv_close(reinterpret_cast<uv_handle_t*>(&_stream), 0);
	}
}

void PacketStream::accept(uv_stream_t* stream) {
	uv_tcp_init(stream->loop, &_stream);
	if (uv_accept(stream, reinterpret_cast<uv_stream_t*>(&_stream)) != 0) {
		std::cerr << "uv_accept failed\n";
	}
	_stream.data = this;
	uv_read_start(reinterpret_cast<uv_stream_t*>(&_stream), onAllocate, onRead);
}

void PacketStream::createFrom(uv_loop_t* loop, uv_os_sock_t sock) {
	uv_tcp_init(loop, &_stream);
	uv_tcp_open(&_stream, sock);
	_stream.data = this;
	uv_read_start(reinterpret_cast<uv_stream_t*>(&_stream), onAllocate, onRead);
}

void PacketStream::send(int packetType, char* begin, size_t length) {
	size_t bufferLength = sizeof(uv_write_t) + sizeof(packetType) + sizeof(length) + length;
	char* buffer = new char[bufferLength];
	uv_write_t* request = reinterpret_cast<uv_write_t*>(buffer);
	buffer += sizeof(uv_write_t);
	memcpy(buffer, &packetType, sizeof(packetType));
	memcpy(buffer + sizeof(packetType), &length, sizeof(length));
	memcpy(buffer + sizeof(packetType) + sizeof(length), begin, length);
	uv_buf_t writeBuffer;
	writeBuffer.base = buffer;
	writeBuffer.len = sizeof(packetType) + sizeof(length) + length;
	uv_write(request, reinterpret_cast<uv_stream_t*>(&_stream), &writeBuffer, 1, onWrite);
}

void PacketStream::setOnReceive(OnReceive* onReceiveCallback, void* userData) {
	_onReceive = onReceiveCallback;
	_onReceiveUserData = userData;
}

void PacketStream::onWrite(uv_write_t* request, int status) {
	delete reinterpret_cast<char*>(request);
}

void PacketStream::onAllocate(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer) {
	buffer->base = new char[suggestedSize];
	buffer->len = suggestedSize;
}

void PacketStream::onRead(uv_stream_t* handle, ssize_t count, const uv_buf_t* buffer) {
	PacketStream* owner = reinterpret_cast<PacketStream*>(handle->data);
	if (count >= 0) {
		if (count > 0) {
			owner->_buffer.insert(owner->_buffer.end(), buffer->base, buffer->base + count);
			owner->processMessages();
		}
		delete[] reinterpret_cast<char*>(buffer->base);
	}
}

void PacketStream::processMessages() {
	int packetType = 0;
	size_t length = 0;
	while (_buffer.size() >= sizeof(packetType) + sizeof(length)) {
		memcpy(&packetType, &*_buffer.begin(), sizeof(packetType));
		memcpy(&length, &*_buffer.begin() + sizeof(packetType), sizeof(length));

		if (_buffer.size() >= sizeof(packetType) + sizeof(length) + length) {
			if (_onReceive) {
				_onReceive(packetType, &*_buffer.begin() + sizeof(length) + sizeof(packetType), length, _onReceiveUserData);
			}
			_buffer.erase(_buffer.begin(), _buffer.begin() + sizeof(length) + sizeof(packetType) + length);
		} else {
			break;
		}
	}
}
