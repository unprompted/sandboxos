#include "Socket.h"

#include "Task.h"

#include <uv.h>

Socket::Socket(Task* task, socketid_t id) {
	_id = id;
	uv_tcp_init(task->getLoop(), &_socket);
	_open = true;
	_socket.data = this;
	_task = task;
	_promise = -1;
}

Socket::~Socket() {
	close();
}

void Socket::close() {
	if (_open) {
		_open = false;
		uv_close(reinterpret_cast<uv_handle_t*>(&_socket), onClose);
	} else if (_promise != -1) {
		_task->rejectPromise(_promise, v8::Undefined(_task->getIsolate()));
	}
}

void Socket::bind(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.This())) {
		v8::String::Utf8Value ip(args[0]->ToString());
		int port = args[1]->ToInteger()->Value();
		struct sockaddr_in6 address;
		uv_ip6_addr(*ip, port, &address);
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), uv_tcp_bind(&socket->_socket, reinterpret_cast<struct sockaddr*>(&address), 0)));
	}
}

void Socket::listen(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.This())) {
		int backlog = args[0]->ToInteger()->Value();
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[1].As<v8::Function>());
		socket->_onConnect = callback;
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), uv_listen(reinterpret_cast<uv_stream_t*>(&socket->_socket), backlog, onNewConnection)));
	}
}

void Socket::onNewConnection(uv_stream_t* server, int status) {
	if (Socket* socket = reinterpret_cast<Socket*>(server->data)) {
		TaskTryCatch tryCatch(socket->_task);
		v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onConnect);
		callback->Call(callback, 0, 0);
	}
}

void Socket::accept(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.This())) {
		v8::Handle<v8::Object> client = Socket::create(socket->_task);
		args.GetReturnValue().Set(client);
		uv_accept(reinterpret_cast<uv_stream_t*>(&socket->_socket), reinterpret_cast<uv_stream_t*>(&Socket::get(client)->_socket));
	}
}

void Socket::close(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.This())) {
		args.GetReturnValue().Set(socket->makePromise());
		socket->close();
	}
}

void Socket::read(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.This())) {
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[0].As<v8::Function>());
		socket->_onRead = callback;
		uv_read_start(reinterpret_cast<uv_stream_t*>(&socket->_socket), allocateBuffer, onRead);
	}
}

void Socket::allocateBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
	*buf = uv_buf_init(new char[suggestedSize], suggestedSize);
}

void Socket::onRead(uv_stream_t* stream, ssize_t readSize, const uv_buf_t* buffer) {
	if (Socket* socket = reinterpret_cast<Socket*>(stream->data)) {
		TaskTryCatch tryCatch(socket->_task);
		v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onRead);
		v8::Handle<v8::Value> data;
		if (readSize >= 0) {
			data = v8::String::NewFromOneByte(socket->_task->getIsolate(), reinterpret_cast<const uint8_t*>(buffer->base), v8::String::kNormalString, readSize);
		} else {
			data = v8::Undefined(socket->_task->getIsolate());
		}
		callback->Call(callback, 1, &data);
	}
	delete[] buffer->base;
}

void Socket::write(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.This())) {
		promiseid_t promise = socket->_task->allocatePromise();
		args.GetReturnValue().Set(socket->_task->getPromise(promise));
		v8::Handle<v8::String> value = args[0].As<v8::String>();
		if (!value->IsUndefined()) {
			int valueLength = value->Utf8Length();
			char* rawBuffer = new char[sizeof(uv_write_t) + valueLength];
			uv_write_t* request = reinterpret_cast<uv_write_t*>(rawBuffer);
			rawBuffer += sizeof(uv_write_t);
			value->WriteUtf8(rawBuffer, valueLength, 0, 0);

			uv_buf_t buffer;
			buffer.base = rawBuffer;
			buffer.len = valueLength;

			request->data = reinterpret_cast<void*>(promise);
			uv_write(request, reinterpret_cast<uv_stream_t*>(&socket->_socket), &buffer, 1, onWrite);
		} else {
			socket->_task->rejectPromise(socket->_promise, v8::Integer::New(args.GetIsolate(), -2));
		}
	}
}

void Socket::onWrite(uv_write_t* request, int status) {
	if (Socket* socket = reinterpret_cast<Socket*>(request->handle->data)) {
		promiseid_t promise = reinterpret_cast<intptr_t>(request->data);
		socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), status));
	}
	delete[] reinterpret_cast<char*>(request);
}

v8::Handle<v8::Promise::Resolver> Socket::makePromise() {
	if (_promise != -1) {
		std::cerr << *_task << " making a second promise?\n";
		promiseid_t promise = _promise;
		_promise = -1;
		_task->rejectPromise(promise, v8::Integer::New(_task->getIsolate(), -1));
	}
	_promise = _task->allocatePromise();
	return _task->getPromise(_promise);
}

void Socket::onClose(uv_handle_t* handle) {
	if (Socket* socket = reinterpret_cast<Socket*>(handle->data)) {
		if (socket->_promise != -1) {
			promiseid_t promise = socket->_promise;
			socket->_promise = -1;
			socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), 0));
		}
		handle->data = 0;
		socket->_task->releaseSocket(socket->_id);
		delete socket;
	}
}

void Socket::getPeerName(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (Socket* socket = Socket::get(info.This())) {
		struct sockaddr_in6 addr;
		int nameLength = sizeof(addr);
		if (uv_tcp_getpeername(&socket->_socket, reinterpret_cast<sockaddr*>(&addr), &nameLength) == 0) {
			char name[1024];
			if (uv_ip6_name(&addr, name, sizeof(name)) == 0) {
				info.GetReturnValue().Set(v8::String::NewFromUtf8(info.GetIsolate(), name));
			}
		}
	}
}

v8::Handle<v8::Object> Socket::create(Task* task) {
	v8::Handle<v8::Object> socketObject;

	v8::Handle<v8::ObjectTemplate> socketTemplate = v8::ObjectTemplate::New(task->getIsolate());
	socketTemplate->SetInternalFieldCount(1);
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "bind"), v8::FunctionTemplate::New(task->getIsolate(), bind));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "listen"), v8::FunctionTemplate::New(task->getIsolate(), listen));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "accept"), v8::FunctionTemplate::New(task->getIsolate(), accept));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "close"), v8::FunctionTemplate::New(task->getIsolate(), close));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "read"), v8::FunctionTemplate::New(task->getIsolate(), read));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "write"), v8::FunctionTemplate::New(task->getIsolate(), write));
	socketTemplate->SetAccessor(v8::String::NewFromUtf8(task->getIsolate(), "peerName"), getPeerName);

	socketObject = socketTemplate->NewInstance();
	socketObject->SetInternalField(0, v8::Int32::New(task->getIsolate(), task->allocateSocket()));

	return socketObject;
}

Socket* Socket::get(v8::Handle<v8::Object> socketObject) {
	Socket* result = 0;
	if (Task* task = reinterpret_cast<Task*>(socketObject->GetIsolate()->GetData(0))) {
		result = task->getSocket(socketObject->GetInternalField(0)->Int32Value());
	}
	return result;
}
