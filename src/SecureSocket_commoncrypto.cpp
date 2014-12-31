#include "SecureSocket_commoncrypto.h"

#include "Task.h"
#include "TaskTryCatch.h"

#include <assert.h>
#include <cstring>
#include <uv.h>

#include <Security/SecureTransport.h>

int SecureSocket_commoncrypto::_count = 0;
int SecureSocket_commoncrypto::_openCount = 0;

SecureSocket_commoncrypto::SecureSocket_commoncrypto(Task* task) {
	v8::HandleScope scope(task->getIsolate());
	++_count;

	v8::Local<v8::ObjectTemplate> socketTemplate = v8::ObjectTemplate::New(task->getIsolate());
	socketTemplate->SetInternalFieldCount(1);
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "bind"), v8::FunctionTemplate::New(task->getIsolate(), bind));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "connect"), v8::FunctionTemplate::New(task->getIsolate(), connect));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "startTls"), v8::FunctionTemplate::New(task->getIsolate(), startTls));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "listen"), v8::FunctionTemplate::New(task->getIsolate(), listen));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "accept"), v8::FunctionTemplate::New(task->getIsolate(), accept));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "close"), v8::FunctionTemplate::New(task->getIsolate(), close));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "shutdown"), v8::FunctionTemplate::New(task->getIsolate(), shutdown));
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

SecureSocket_commoncrypto::~SecureSocket_commoncrypto() {
	CFRelease(_context);
	_context = 0;
	--_count;
}

void SecureSocket_commoncrypto::close() {
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


void SecureSocket_commoncrypto::bind(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		v8::String::Utf8Value ip(args[0]->ToString());
		int port = args[1]->ToInteger()->Value();
		struct sockaddr_in6 address;
		uv_ip6_addr(*ip, port, &address);
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(),
			uv_tcp_bind(&socket->_socket, reinterpret_cast<struct sockaddr*>(&address), 0)));
	}
}

void SecureSocket_commoncrypto::connect(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		v8::String::Utf8Value ip(args[0]->ToString());
		int port = args[1]->ToInteger()->Value();
		struct sockaddr_in address;
		uv_ip4_addr(*ip, port, &address);

		promiseid_t promise = socket->_task->allocatePromise();

		uv_connect_t* request = new uv_connect_t();
		std::memset(request, 0, sizeof(*request));
		request->data = reinterpret_cast<void*>(promise);
		int result = uv_tcp_connect(request, &socket->_socket, reinterpret_cast<const sockaddr*>(&address), onConnect);
		if (result == 0) {
			args.GetReturnValue().Set(socket->_task->getPromise(promise));
		} else {
			args.GetReturnValue().Set(socket->_task->getPromise(promise));
			std::string error("uv_tcp_connect failed immediately: " + std::string(uv_strerror(result)));
			socket->_task->rejectPromise(promise, args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), error.c_str()))));
		}
	}
}

void SecureSocket_commoncrypto::onConnect(uv_connect_t* request, int status) {
	promiseid_t promise = reinterpret_cast<intptr_t>(request->data);
	if (promise != -1) {
		SecureSocket_commoncrypto* socket = reinterpret_cast<SecureSocket_commoncrypto*>(request->handle->data);
		if (status == 0) {
			socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), status));
		} else {
			std::string error("uv_tcp_connect failed: " + std::string(uv_strerror(status)));
			socket->_task->rejectPromise(promise, v8::String::NewFromUtf8(socket->_task->getIsolate(), error.c_str()));
		}
	}
}

void SecureSocket_commoncrypto::listen(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		int backlog = args[0]->ToInteger()->Value();
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[1].As<v8::Function>());
		socket->_onConnect = callback;
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), uv_listen(reinterpret_cast<uv_stream_t*>(&socket->_socket), backlog, onNewConnection)));
	}
}

void SecureSocket_commoncrypto::onNewConnection(uv_stream_t* server, int status) {
	if (SecureSocket_commoncrypto* socket = reinterpret_cast<SecureSocket_commoncrypto*>(server->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		TaskTryCatch tryCatch(socket->_task);
		if (!socket->_onConnect.IsEmpty()) {
			v8::Handle<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onConnect);
			callback->Call(callback, 0, 0);
		}
	}
}

void SecureSocket_commoncrypto::accept(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		v8::HandleScope handleScope(args.GetIsolate());
		SecureSocket_commoncrypto* client = new SecureSocket_commoncrypto(socket->_task);
		v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), client->_object);
		args.GetReturnValue().Set(result);
		// XXX: Propagate SSL context.
		client->release();
		if (uv_accept(reinterpret_cast<uv_stream_t*>(&socket->_socket), reinterpret_cast<uv_stream_t*>(&client->_socket)) == 0) {
			client->_connected = true;
		} else {
			std::cerr << "uv_accept failed\n";
		}
	}
}

void SecureSocket_commoncrypto::close(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		args.GetReturnValue().Set(socket->makePromise());
		socket->close();
	}
}

void SecureSocket_commoncrypto::shutdown(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		OSStatus result = SSLClose(socket->_context);
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), result));
	}
}

void SecureSocket_commoncrypto::read(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[0].As<v8::Function>());
		socket->_onRead = callback;
		if (uv_read_start(reinterpret_cast<uv_stream_t*>(&socket->_socket), allocateBuffer, onRead) != 0) {
			std::cerr << "uv_read_start failed\n";
		}
	}
}

void SecureSocket_commoncrypto::allocateBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
	*buf = uv_buf_init(new char[suggestedSize], suggestedSize);
}

void SecureSocket_commoncrypto::onRead(uv_stream_t* stream, ssize_t readSize, const uv_buf_t* buffer) {
	if (SecureSocket_commoncrypto* socket = reinterpret_cast<SecureSocket_commoncrypto*>(stream->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		TaskTryCatch tryCatch(socket->_task);
		v8::Handle<v8::Value> data;
		if (readSize <= 0) {
			socket->close();
			socket->_connected = false;
		}

		if (readSize >= 0) {
			if (socket->_context) {
				socket->_inBuffer.insert(socket->_inBuffer.end(), buffer->base, buffer->base + readSize);
				if (socket->_startTlsPromise != -1) {
					OSStatus result = SSLHandshake(socket->_context);
					if (result == noErr) {
						socket->_task->resolvePromise(socket->_startTlsPromise, v8::Integer::New(socket->_task->getIsolate(), 0));
						socket->_startTlsPromise = -1;
					}
				} else {
					char decrypted[8192];
					size_t processed = 0;
					while (true) {
						OSStatus result = SSLRead(socket->_context, decrypted, sizeof(decrypted), &processed);
						if (result == noErr && processed > 0) {
							v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(),socket-> _onRead);
							v8::Handle<v8::Value> arg = v8::String::NewFromUtf8(socket->_task->getIsolate(), decrypted, v8::String::kNormalString, processed);
							callback->Call(callback, 1, &arg);
						} else {
							break;
						}
					}
				}
			} else {
				v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(),socket-> _onRead);
				v8::Handle<v8::Value> arg = v8::String::NewFromUtf8(socket->_task->getIsolate(), buffer->base, v8::String::kNormalString, readSize);
				callback->Call(callback, 1, &arg);
			}
		} else if (!socket->_onRead.IsEmpty()) {
			v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onRead);
			v8::Handle<v8::Value> arg = v8::Undefined(socket->_task->getIsolate());
			callback->Call(callback, 1, &arg);
		}
	}
	delete[] buffer->base;
}

void SecureSocket_commoncrypto::write(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		promiseid_t promise = socket->_task->allocatePromise();
		args.GetReturnValue().Set(socket->_task->getPromise(promise));
		v8::Handle<v8::String> value = args[0].As<v8::String>();
		if (!value.IsEmpty() && value->IsString()) {
			v8::String::Utf8Value utf8Value(value);
			int result = -1;
			if (socket->_context) {
				size_t processed = 0;
				result = SSLWrite(socket->_context, *utf8Value, utf8Value.length(), &processed);
			} else {
				char* rawBuffer = new char[sizeof(uv_write_t) + utf8Value.length()];
				uv_write_t* request = reinterpret_cast<uv_write_t*>(rawBuffer);
				rawBuffer += sizeof(uv_write_t);
				std::memcpy(rawBuffer, *utf8Value, utf8Value.length());

				uv_buf_t writeBuffer;
				writeBuffer.base = rawBuffer;
				writeBuffer.len = utf8Value.length();

				request->data = reinterpret_cast<void*>(-1);
				result = uv_write(request, reinterpret_cast<uv_stream_t*>(&socket->_socket), &writeBuffer, 1, onWrite);
			}
			if (result != 0) {
				socket->_task->rejectPromise(promise, v8::Integer::New(args.GetIsolate(), result));
			} else {
				socket->_task->resolvePromise(promise, v8::Integer::New(args.GetIsolate(), result));
			}
		} else {
			socket->_task->rejectPromise(promise, v8::Integer::New(args.GetIsolate(), -2));
		}
	}
}

void SecureSocket_commoncrypto::onWrite(uv_write_t* request, int status) {
	if (SecureSocket_commoncrypto* socket = reinterpret_cast<SecureSocket_commoncrypto*>(request->handle->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		promiseid_t promise = reinterpret_cast<intptr_t>(request->data);
		if (promise != -1) {
			socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), status));
		}
	}
	delete[] reinterpret_cast<char*>(request);
}

v8::Handle<v8::Promise::Resolver> SecureSocket_commoncrypto::makePromise() {
	if (_promise != -1) {
		promiseid_t promise = _promise;
		_promise = -1;
		_task->rejectPromise(promise, v8::Integer::New(_task->getIsolate(), -1));
	}
	_promise = _task->allocatePromise();
	return _task->getPromise(_promise);
}

void SecureSocket_commoncrypto::onClose(uv_handle_t* handle) {
	--_openCount;
	if (SecureSocket_commoncrypto* socket = reinterpret_cast<SecureSocket_commoncrypto*>(handle->data)) {
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

void SecureSocket_commoncrypto::getPeerName(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(info.This())) {
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

void SecureSocket_commoncrypto::isConnected(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(info.This())) {
		info.GetReturnValue().Set(v8::Boolean::New(socket->_task->getIsolate(), socket->_connected));
	}
}

void SecureSocket_commoncrypto::create(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope handleScope(args.GetIsolate());
	if (SecureSocket_commoncrypto* socket = new SecureSocket_commoncrypto(Task::get(args.GetIsolate()))) {

		// XXX: Handle certificate + key arguments!
		v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), socket->_object);
		args.GetReturnValue().Set(result);

		socket->release();
	}
}

void SecureSocket_commoncrypto::startTls(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_commoncrypto* socket = SecureSocket_commoncrypto::get(args.This())) {
		socket->_context = SSLCreateContext(0, kSSLClientSide, kSSLStreamType);
		SSLSetIOFuncs(socket->_context, readInternal, writeInternal);
		SSLSetConnection(socket->_context, socket);

		SSLHandshake(socket->_context);

		socket->_startTlsPromise = socket->_task->allocatePromise();
		args.GetReturnValue().Set(socket->_task->getPromise(socket->_startTlsPromise));
	}
}

SecureSocket_commoncrypto* SecureSocket_commoncrypto::get(v8::Handle<v8::Object> socketObject) {
	return reinterpret_cast<SecureSocket_commoncrypto*>(v8::Handle<v8::External>::Cast(socketObject->GetInternalField(0))->Value());
}

void SecureSocket_commoncrypto::ref() {
	if (++_refCount == 1) {
		_object.ClearWeak();
	}
}

void SecureSocket_commoncrypto::release() {
	assert(_refCount >= 1);
	if (--_refCount == 0) {
		_object.SetWeak(this, onRelease);
	}
}

void SecureSocket_commoncrypto::onRelease(const v8::WeakCallbackData<v8::Object, SecureSocket_commoncrypto>& data) {
	data.GetParameter()->_object.Reset();
	data.GetParameter()->close();
}

OSStatus SecureSocket_commoncrypto::writeInternal(SSLConnectionRef connection, const void* data, size_t* dataLength) {
	const SecureSocket_commoncrypto* socket = reinterpret_cast<const SecureSocket_commoncrypto*>(connection);
	char* rawBuffer = new char[sizeof(uv_write_t) + *dataLength];
	uv_write_t* request = reinterpret_cast<uv_write_t*>(rawBuffer);
	rawBuffer += sizeof(uv_write_t);
	std::memcpy(rawBuffer, data, *dataLength);

	uv_buf_t writeBuffer;
	writeBuffer.base = rawBuffer;
	writeBuffer.len = *dataLength;

	request->data = reinterpret_cast<void*>(-1);
	uv_stream_t* stream = reinterpret_cast<uv_stream_t*>(const_cast<uv_tcp_t*>(&socket->_socket));
	if (uv_write(request, stream, &writeBuffer, 1, onWrite) != 0) {
		std::cerr << "uv_write failed\n";
	}
	return noErr;
}

OSStatus SecureSocket_commoncrypto::readInternal(SSLConnectionRef connection, void* data, size_t* dataLength) {
	OSStatus result = noErr;
	SecureSocket_commoncrypto* socket = reinterpret_cast<SecureSocket_commoncrypto*>(const_cast<void*>(connection));
	size_t bytes = std::min(socket->_inBuffer.size(), *dataLength);
	if (bytes > 0) {
		std::memcpy(data, &*socket->_inBuffer.begin(), bytes);
		socket->_inBuffer.erase(socket->_inBuffer.begin(), socket->_inBuffer.begin() + bytes);
	}
	if (bytes < *dataLength) {
		result = errSSLWouldBlock;
	}
	*dataLength = bytes;
	return result;
}
