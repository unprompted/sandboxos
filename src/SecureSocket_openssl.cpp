#include "SecureSocket_openssl.h"

#include "Task.h"
#include "TaskTryCatch.h"

#include <assert.h>
#include <cstring>
#include <uv.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

int SecureSocket_openssl::_count = 0;
int SecureSocket_openssl::_openCount = 0;

SecureSocket_openssl::SecureSocket_openssl(Task* task) {
	v8::HandleScope scope(task->getIsolate());
	++_count;

	SSL_library_init();
	SSL_load_error_strings();

	v8::Local<v8::ObjectTemplate> socketTemplate = v8::ObjectTemplate::New(task->getIsolate());
	socketTemplate->SetInternalFieldCount(1);
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "bind"), v8::FunctionTemplate::New(task->getIsolate(), bind));
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

	_bioIn = BIO_new(BIO_s_mem());
	_bioOut = BIO_new(BIO_s_mem());
}

SecureSocket_openssl::~SecureSocket_openssl() {
	SSL_CTX_free(_context);
	--_count;
}

void SecureSocket_openssl::close() {
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


void SecureSocket_openssl::bind(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(args.This())) {
		v8::String::Utf8Value ip(args[0]->ToString());
		int port = args[1]->ToInteger()->Value();
		struct sockaddr_in6 address;
		uv_ip6_addr(*ip, port, &address);
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(),
			uv_tcp_bind(&socket->_socket, reinterpret_cast<struct sockaddr*>(&address), 0)));
	}
}

void SecureSocket_openssl::listen(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(args.This())) {
		int backlog = args[0]->ToInteger()->Value();
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[1].As<v8::Function>());
		socket->_onConnect = callback;
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), uv_listen(reinterpret_cast<uv_stream_t*>(&socket->_socket), backlog, onNewConnection)));
	}
}

void SecureSocket_openssl::onNewConnection(uv_stream_t* server, int status) {
	if (SecureSocket_openssl* socket = reinterpret_cast<SecureSocket_openssl*>(server->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		TaskTryCatch tryCatch(socket->_task);
		if (!socket->_onConnect.IsEmpty()) {
			v8::Handle<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onConnect);
			callback->Call(callback, 0, 0);
		}
	}
}

void SecureSocket_openssl::accept(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(args.This())) {
		v8::HandleScope handleScope(args.GetIsolate());
		SecureSocket_openssl* client = new SecureSocket_openssl(socket->_task);
		v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), client->_object);
		args.GetReturnValue().Set(result);
		client->_context = socket->_context;
		client->_ssl = SSL_new(client->_context);
		SSL_set_bio(client->_ssl, client->_bioIn, client->_bioOut);
		SSL_accept(client->_ssl);
		client->release();
		if (uv_accept(reinterpret_cast<uv_stream_t*>(&socket->_socket), reinterpret_cast<uv_stream_t*>(&client->_socket)) == 0) {
			client->_connected = true;
		} else {
			std::cerr << "uv_accept failed\n";
		}
	}
}

void SecureSocket_openssl::close(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(args.This())) {
		args.GetReturnValue().Set(socket->makePromise());
		socket->close();
	}
}

void SecureSocket_openssl::shutdown(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(args.This())) {
		int result = SSL_shutdown(socket->_ssl);
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), result));
	}
}

void SecureSocket_openssl::read(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(args.This())) {
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[0].As<v8::Function>());
		socket->_onRead = callback;
		if (uv_read_start(reinterpret_cast<uv_stream_t*>(&socket->_socket), allocateBuffer, onRead) != 0) {
			std::cerr << "uv_read_start failed\n";
		}
	}
}

void SecureSocket_openssl::update() {
	if (!SSL_is_init_finished(_ssl)) {
		int result = SSL_do_handshake(_ssl);
		if (result < 0) {
			int error = SSL_get_error(_ssl, result);
			if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
				_task->getIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(_task->getIsolate(), "SSL_connect failed.")));
				close();
			}
		}
	}

	if (SSL_is_init_finished(_ssl) && !_onRead.IsEmpty()) {
		while (true) {
			char buffer[8192];
			int result = SSL_read(_ssl, buffer, sizeof(buffer));
			if (result > 0) {
				v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(_task->getIsolate(), _onRead);
				v8::Handle<v8::Value> arg = v8::String::NewFromUtf8(_task->getIsolate(), buffer, v8::String::kNormalString, result);
				callback->Call(callback, 1, &arg);
			} else {
				break;
				close();
			}
		}
	}

	char buffer[8192];
	while (true) {
		int result = BIO_read(_bioOut, buffer, sizeof(buffer));
		if (result > 0) {
			char* rawBuffer = new char[sizeof(uv_write_t) + result];
			uv_write_t* request = reinterpret_cast<uv_write_t*>(rawBuffer);
			rawBuffer += sizeof(uv_write_t);
			std::memcpy(rawBuffer, buffer, result);

			uv_buf_t writeBuffer;
			writeBuffer.base = rawBuffer;
			writeBuffer.len = result;

			request->data = reinterpret_cast<void*>(-1);
			if (uv_write(request, reinterpret_cast<uv_stream_t*>(&_socket), &writeBuffer, 1, onWrite) != 0) {
				std::cerr << "uv_write failed\n";
			}
		} else {
			break;
		}
	}
}

void SecureSocket_openssl::allocateBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf) {
	*buf = uv_buf_init(new char[suggestedSize], suggestedSize);
}

void SecureSocket_openssl::onRead(uv_stream_t* stream, ssize_t readSize, const uv_buf_t* buffer) {
	if (SecureSocket_openssl* socket = reinterpret_cast<SecureSocket_openssl*>(stream->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		TaskTryCatch tryCatch(socket->_task);
		v8::Handle<v8::Value> data;
		if (readSize <= 0) {
			socket->close();
			socket->_connected = false;
		}

		if (readSize >= 0) {
			BIO_write(socket->_bioIn, buffer->base, readSize);
			socket->update();
		} else if (!socket->_onRead.IsEmpty()) {
			v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onRead);
			v8::Handle<v8::Value> arg = v8::Undefined(socket->_task->getIsolate());
			callback->Call(callback, 1, &arg);
		}
	}
	delete[] buffer->base;
}

void SecureSocket_openssl::write(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(args.This())) {
		promiseid_t promise = socket->_task->allocatePromise();
		args.GetReturnValue().Set(socket->_task->getPromise(promise));
		v8::Handle<v8::String> value = args[0].As<v8::String>();
		if (!value.IsEmpty() && value->IsString()) {
			v8::String::Utf8Value utf8Value(value);
			int result = SSL_write(socket->_ssl, *utf8Value, utf8Value.length());
			socket->update();
			socket->_task->resolvePromise(promise, v8::Integer::New(args.GetIsolate(), result));
		} else {
			socket->_task->rejectPromise(promise, v8::Integer::New(args.GetIsolate(), -2));
		}
	}
}

void SecureSocket_openssl::onWrite(uv_write_t* request, int status) {
	if (SecureSocket_openssl* socket = reinterpret_cast<SecureSocket_openssl*>(request->handle->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		promiseid_t promise = reinterpret_cast<intptr_t>(request->data);
		if (promise != -1) {
			socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), status));
		}
	}
	delete[] reinterpret_cast<char*>(request);
}

v8::Handle<v8::Promise::Resolver> SecureSocket_openssl::makePromise() {
	if (_promise != -1) {
		promiseid_t promise = _promise;
		_promise = -1;
		_task->rejectPromise(promise, v8::Integer::New(_task->getIsolate(), -1));
	}
	_promise = _task->allocatePromise();
	return _task->getPromise(_promise);
}

void SecureSocket_openssl::onClose(uv_handle_t* handle) {
	--_openCount;
	if (SecureSocket_openssl* socket = reinterpret_cast<SecureSocket_openssl*>(handle->data)) {
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

void SecureSocket_openssl::getPeerName(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(info.This())) {
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

void SecureSocket_openssl::isConnected(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (SecureSocket_openssl* socket = SecureSocket_openssl::get(info.This())) {
		info.GetReturnValue().Set(v8::Boolean::New(socket->_task->getIsolate(), socket->_connected));
	}
}

void SecureSocket_openssl::create(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope handleScope(args.GetIsolate());
	if (SecureSocket_openssl* socket = new SecureSocket_openssl(Task::get(args.GetIsolate()))) {
		socket->_context = SSL_CTX_new(SSLv23_method());

		v8::String::Utf8Value keyFile(args[0]);
		v8::String::Utf8Value certificateFile(args[1]);

		if (!SSL_CTX_use_certificate_chain_file(socket->_context, *certificateFile)) {
			args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), "Failed to read certificate.")));
		} else if (!SSL_CTX_use_PrivateKey_file(socket->_context, *keyFile, SSL_FILETYPE_PEM)) {
			args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), "Failed to read private key.")));
		} else {
			socket->_ssl = SSL_new(socket->_context);
			SSL_set_bio(socket->_ssl, socket->_bioIn, socket->_bioOut);

			v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), socket->_object);
			args.GetReturnValue().Set(result);
		}

		socket->release();
	}
}

SecureSocket_openssl* SecureSocket_openssl::get(v8::Handle<v8::Object> socketObject) {
	return reinterpret_cast<SecureSocket_openssl*>(v8::Handle<v8::External>::Cast(socketObject->GetInternalField(0))->Value());
}

void SecureSocket_openssl::ref() {
	if (++_refCount == 1) {
		_object.ClearWeak();
	}
}

void SecureSocket_openssl::release() {
	assert(_refCount >= 1);
	if (--_refCount == 0) {
		_object.SetWeak(this, onRelease);
	}
}

void SecureSocket_openssl::onRelease(const v8::WeakCallbackData<v8::Object, SecureSocket_openssl>& data) {
	data.GetParameter()->_object.Reset();
	data.GetParameter()->close();
}
