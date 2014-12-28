#include "Socket.h"

#include "Task.h"
#include "TaskTryCatch.h"

#include <assert.h>
#include <uv.h>

int Socket::_count = 0;
int Socket::_openCount = 0;

Socket::Socket(Task* task) {
	v8::HandleScope scope(task->getIsolate());
	++_count;

	v8::Local<v8::ObjectTemplate> socketTemplate = v8::ObjectTemplate::New(task->getIsolate());
	socketTemplate->SetInternalFieldCount(1);
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "bind"), v8::FunctionTemplate::New(task->getIsolate(), bind));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "listen"), v8::FunctionTemplate::New(task->getIsolate(), listen));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "accept"), v8::FunctionTemplate::New(task->getIsolate(), accept));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "close"), v8::FunctionTemplate::New(task->getIsolate(), close));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "read"), v8::FunctionTemplate::New(task->getIsolate(), read));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "write"), v8::FunctionTemplate::New(task->getIsolate(), write));
	socketTemplate->SetAccessor(v8::String::NewFromUtf8(task->getIsolate(), "peerName"), getPeerName);
	socketTemplate->SetAccessor(v8::String::NewFromUtf8(task->getIsolate(), "isConnected"), isConnected);

	v8::Local<v8::Object> socketObject = socketTemplate->NewInstance();
	socketObject->SetInternalField(0, v8::External::New(task->getIsolate(), this));
	_object = v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> >(task->getIsolate(), socketObject);

	uv_tcp_init(task->getLoop(), &_socket);
	++_openCount;
	_socket.data = this;
	_task = task;
	_promise = -1;
}

Socket::~Socket() {
	--_count;
}

void Socket::close() {
	if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&_socket))) {
		if (!_onRead.IsEmpty()) {
			_onRead.Reset();
		}

		if (_promise != -1) {
			int promise = _promise;
			_task->rejectPromise(promise, v8::Integer::New(_task->getIsolate(), -1));
			_promise = -1;
		}

		uv_close(reinterpret_cast<uv_handle_t*>(&_socket), onClose);
	}
}

void Socket::bind(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.This())) {
		v8::String::Utf8Value ip(args[0]->ToString());
		int port = args[1]->ToInteger()->Value();
		std::cout << "Trying to bind to " << *ip << " " << port << "\n";
		struct sockaddr_in6 address;
		uv_ip6_addr(*ip, port, &address);
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(),
			uv_tcp_bind(&socket->_socket, reinterpret_cast<struct sockaddr*>(&address), 0)));
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
		v8::HandleScope handleScope(socket->_task->getIsolate());
		TaskTryCatch tryCatch(socket->_task);
		if (!socket->_onConnect.IsEmpty()) {
			v8::Handle<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onConnect);
			callback->Call(callback, 0, 0);
		}
	}
}

void Socket::accept(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.This())) {
		v8::HandleScope handleScope(args.GetIsolate());
		Socket* client = new Socket(socket->_task);
		v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), client->_object);
		args.GetReturnValue().Set(result);
		client->release();
		if (uv_accept(reinterpret_cast<uv_stream_t*>(&socket->_socket), reinterpret_cast<uv_stream_t*>(&client->_socket)) == 0) {
			client->_connected = true;
		} else {
			std::cerr << "uv_accept failed\n";
		}
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
		if (uv_read_start(reinterpret_cast<uv_stream_t*>(&socket->_socket), allocateBuffer, onRead) != 0) {
			std::cerr << "uv_read_start failed\n";
		}
	}
}

void Socket::allocateBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
	*buf = uv_buf_init(new char[suggestedSize], suggestedSize);
}

void Socket::onRead(uv_stream_t* stream, ssize_t readSize, const uv_buf_t* buffer) {
	if (Socket* socket = reinterpret_cast<Socket*>(stream->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		TaskTryCatch tryCatch(socket->_task);
		v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onRead);
		v8::Handle<v8::Value> data;
		if (readSize <= 0) {
			socket->close();
			socket->_connected = false;
		}

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
		if (!value.IsEmpty() && value->IsString()) {
			int valueLength = value->Utf8Length();
			char* rawBuffer = new char[sizeof(uv_write_t) + valueLength];
			uv_write_t* request = reinterpret_cast<uv_write_t*>(rawBuffer);
			rawBuffer += sizeof(uv_write_t);
			value->WriteUtf8(rawBuffer, valueLength, 0, 0);

			uv_buf_t buffer;
			buffer.base = rawBuffer;
			buffer.len = valueLength;

			request->data = reinterpret_cast<void*>(promise);
			if (uv_write(request, reinterpret_cast<uv_stream_t*>(&socket->_socket), &buffer, 1, onWrite) != 0) {
				std::cerr << "uv_write failed\n";
			}
		} else {
			socket->_task->rejectPromise(socket->_promise, v8::Integer::New(args.GetIsolate(), -2));
		}
	}
}

void Socket::onWrite(uv_write_t* request, int status) {
	if (Socket* socket = reinterpret_cast<Socket*>(request->handle->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		promiseid_t promise = reinterpret_cast<intptr_t>(request->data);
		socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), status));
	}
	delete[] reinterpret_cast<char*>(request);
}

v8::Handle<v8::Promise::Resolver> Socket::makePromise() {
	if (_promise != -1) {
		promiseid_t promise = _promise;
		_promise = -1;
		_task->rejectPromise(promise, v8::Integer::New(_task->getIsolate(), -1));
	}
	_promise = _task->allocatePromise();
	return _task->getPromise(_promise);
}

void Socket::onClose(uv_handle_t* handle) {
	--_openCount;
	if (Socket* socket = reinterpret_cast<Socket*>(handle->data)) {
		if (socket->_promise != -1) {
			v8::HandleScope scope(socket->_task->getIsolate());
			promiseid_t promise = socket->_promise;
			socket->_promise = -1;
			socket->_connected = false;
			socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), 0));
		}
		if (socket->_object.IsEmpty()) {
			delete socket;
		}
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

void Socket::isConnected(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (Socket* socket = Socket::get(info.This())) {
		info.GetReturnValue().Set(v8::Boolean::New(socket->_task->getIsolate(), socket->_connected));
	}
}

void Socket::create(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope handleScope(args.GetIsolate());
	if (Socket* socket = new Socket(Task::get(args.GetIsolate()))) {
		v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), socket->_object);
		args.GetReturnValue().Set(result);
		socket->release();
	}
}

Socket* Socket::get(v8::Handle<v8::Object> socketObject) {
	return reinterpret_cast<Socket*>(v8::Handle<v8::External>::Cast(socketObject->GetInternalField(0))->Value());
}

void Socket::ref() {
	if (++_refCount == 1) {
		_object.ClearWeak();
	}
}

void Socket::release() {
	assert(_refCount >= 1);
	if (--_refCount == 0) {
		_object.SetWeak(this, onRelease);
	}
}

void Socket::onRelease(const v8::WeakCallbackData<v8::Object, Socket>& data) {
	data.GetParameter()->_object.Reset();
	data.GetParameter()->close();
}
