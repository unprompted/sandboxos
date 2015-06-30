#include "Socket.h"

#include "Task.h"
#include "TaskTryCatch.h"
#include "Tls.h"
#include "TlsContextWrapper.h"

#include <assert.h>
#include <cstring>
#include <uv.h>

int Socket::_count = 0;
int Socket::_openCount = 0;
TlsContext* Socket::_defaultTlsContext = 0;

struct SocketResolveData {
	uv_getaddrinfo_t resolver;
	Socket* socket;
	promiseid_t promise;
};

Socket::Socket(Task* task) {
	v8::HandleScope scope(task->getIsolate());
	++_count;

	v8::Handle<v8::External> data = v8::External::New(task->getIsolate(), this);

	v8::Local<v8::ObjectTemplate> socketTemplate = v8::ObjectTemplate::New(task->getIsolate());
	socketTemplate->SetInternalFieldCount(1);
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "bind"), v8::FunctionTemplate::New(task->getIsolate(), bind, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "connect"), v8::FunctionTemplate::New(task->getIsolate(), connect, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "listen"), v8::FunctionTemplate::New(task->getIsolate(), listen, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "accept"), v8::FunctionTemplate::New(task->getIsolate(), accept, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "startTls"), v8::FunctionTemplate::New(task->getIsolate(), startTls, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "stopTls"), v8::FunctionTemplate::New(task->getIsolate(), stopTls, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "shutdown"), v8::FunctionTemplate::New(task->getIsolate(), shutdown, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "close"), v8::FunctionTemplate::New(task->getIsolate(), close, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "read"), v8::FunctionTemplate::New(task->getIsolate(), read, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "onError"), v8::FunctionTemplate::New(task->getIsolate(), onError, data));
	socketTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "write"), v8::FunctionTemplate::New(task->getIsolate(), write, data));
	socketTemplate->SetAccessor(v8::String::NewFromUtf8(task->getIsolate(), "peerName"), getPeerName, 0, data);
	socketTemplate->SetAccessor(v8::String::NewFromUtf8(task->getIsolate(), "peerCertificate"), getPeerCertificate, 0, data);
	socketTemplate->SetAccessor(v8::String::NewFromUtf8(task->getIsolate(), "isConnected"), isConnected, 0, data);

	v8::Local<v8::Object> socketObject = socketTemplate->NewInstance();
	socketObject->SetInternalField(0, v8::External::New(task->getIsolate(), this));
	_object = v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> >(task->getIsolate(), socketObject);

	uv_tcp_init(task->getLoop(), &_socket);
	++_openCount;
	_socket.data = this;
	_task = task;
}

Socket::~Socket() {
	if (_tls) {
		delete _tls;
		_tls = 0;
	}
	--_count;
}

void Socket::close() {
	if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&_socket))) {
		if (!_onRead.IsEmpty()) {
			_onRead.Reset();
		}
		uv_close(reinterpret_cast<uv_handle_t*>(&_socket), onClose);
	}
}

void Socket::reportError(const char* error) {
	v8::Handle<v8::Value> exception = v8::Exception::Error(v8::String::NewFromUtf8(_task->getIsolate(), error));
	if (!_onError.IsEmpty()) {
		v8::Handle<v8::Function> callback = v8::Local<v8::Function>::New(_task->getIsolate(), _onError);
		callback->Call(callback, 1, &exception);
	} else {
		_task->getIsolate()->ThrowException(exception);
	}
}

void Socket::reportTlsErrors() {
	char buffer[4096];
	while (_tls && _tls->getError(buffer, sizeof(buffer))) {
		reportError(buffer);
	}
}

void Socket::startTls(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		if (!socket->_tls) {
			TlsContext* context = 0;

			if (args.Length() > 0 && !args[0].IsEmpty() && !args[0]->IsUndefined()) {
				if (TlsContextWrapper* wrapper = TlsContextWrapper::get(args[0])) {
					context = wrapper->getContext();
				}
			} else {
				if (!_defaultTlsContext) {
					_defaultTlsContext = TlsContext::create();
				}
				context = _defaultTlsContext;
			}

			if (context) {
				socket->_tls = context->createSession();
			}

			if (socket->_tls) {
				socket->_tls->setHostname(socket->_peerName.c_str());
				if (socket->_direction == kAccept) {
					socket->_tls->startAccept();
				} else if (socket->_direction == kConnect) {
					socket->_tls->startConnect();
				}
				socket->_startTlsPromise = socket->_task->allocatePromise();
				socket->processOutgoingTls();
				args.GetReturnValue().Set(socket->_task->getPromise(socket->_startTlsPromise));
			} else {
				args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), "Failed to get TLS context")));
			}
		} else {
			args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), "startTls with TLS already started")));
		}
	}
}

void Socket::stopTls(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		if (socket->_tls) {
			socket->processOutgoingTls();
			delete socket->_tls;
			socket->_tls = 0;
		} else {
			args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), "stopTls with TLS already stopped")));
		}
	}
}

void Socket::processOutgoingTls() {
	while (true) {
		char buffer[8192];
		int result = _tls->readEncrypted(buffer, sizeof(buffer));
		if (result > 0) {
			char* rawBuffer = new char[sizeof(uv_write_t) + result];
			uv_write_t* request = reinterpret_cast<uv_write_t*>(rawBuffer);
			std::memset(request, 0, sizeof(*request));
			request->data = reinterpret_cast<void*>(-1);
			rawBuffer += sizeof(uv_write_t);
			std::memcpy(rawBuffer, buffer, result);

			uv_buf_t writeBuffer;
			writeBuffer.base = rawBuffer;
			writeBuffer.len = result;

			int writeResult = uv_write(request, reinterpret_cast<uv_stream_t*>(&_socket), &writeBuffer, 1, onWrite);
			if (writeResult != 0) {
				std::string error = "uv_write: " + std::string(uv_strerror(writeResult));
				reportError(error.c_str());
			}
		} else {
			reportTlsErrors();
			break;
		}
	}
}

void Socket::bind(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		v8::String::Utf8Value node(args[0]->ToString());
		v8::String::Utf8Value port(args[1]->ToString());

		SocketResolveData* data = new SocketResolveData();
		std::memset(data, 0, sizeof(*data));
		struct addrinfo hints;
		hints.ai_family = PF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = 0;
		data->resolver.data = data;
		data->socket = socket;
		data->promise = socket->_task->allocatePromise();

		int result = uv_getaddrinfo(socket->_task->getLoop(), &data->resolver, onResolvedForBind, *node, *port, &hints);
		if (result != 0) {
			std::string error = "uv_getaddrinfo: " + std::string(uv_strerror(result));
			socket->_task->rejectPromise(data->promise, v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), error.c_str())));
			delete data;
		}

		args.GetReturnValue().Set(socket->_task->getPromise(data->promise));
	}
}

void Socket::onResolvedForBind(uv_getaddrinfo_t* resolver, int status, struct addrinfo* result) {
	SocketResolveData* data = reinterpret_cast<SocketResolveData*>(resolver->data);
	if (status != 0) {
		std::string error = "uv_getaddrinfo: " + std::string(uv_strerror(status));
		data->socket->_task->rejectPromise(data->promise, v8::Exception::Error(v8::String::NewFromUtf8(data->socket->_task->getIsolate(), error.c_str())));
	} else {
		int bindResult = uv_tcp_bind(&data->socket->_socket, result->ai_addr, 0);
		if (bindResult != 0) {
			std::string error = "uv_tcp_bind: " + std::string(uv_strerror(bindResult));
			data->socket->_task->rejectPromise(data->promise, v8::Exception::Error(v8::String::NewFromUtf8(data->socket->_task->getIsolate(), error.c_str())));
		} else {
			data->socket->_task->resolvePromise(data->promise, v8::Undefined(data->socket->_task->getIsolate()));
		}
	}
	delete data;
}

void Socket::connect(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		socket->_direction = kConnect;
		v8::String::Utf8Value node(args[0]->ToString());
		v8::String::Utf8Value port(args[1]->ToString());

		socket->_peerName = *node;

		promiseid_t promise = socket->_task->allocatePromise();

		SocketResolveData* data = new SocketResolveData();
		std::memset(data, 0, sizeof(*data));
		struct addrinfo hints;
		hints.ai_family = PF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = 0;
		data->resolver.data = data;
		data->socket = socket;
		data->promise = promise;

		int result = uv_getaddrinfo(socket->_task->getLoop(), &data->resolver, onResolvedForConnect, *node, *port, &hints);
		if (result != 0) {
			std::string error = "uv_getaddrinfo: " + std::string(uv_strerror(result));
			socket->_task->rejectPromise(promise, v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), error.c_str())));
			delete data;
		}

		args.GetReturnValue().Set(socket->_task->getPromise(promise));
	}
}

void Socket::onResolvedForConnect(uv_getaddrinfo_t* resolver, int status, struct addrinfo* result) {
	SocketResolveData* data = reinterpret_cast<SocketResolveData*>(resolver->data);
	if (status != 0) {
		std::string error = "uv_getaddrinfo: " + std::string(uv_strerror(status));
		data->socket->_task->rejectPromise(data->promise, v8::Exception::Error(v8::String::NewFromUtf8(data->socket->_task->getIsolate(), error.c_str())));
	} else {
		uv_connect_t* request = new uv_connect_t();
		std::memset(request, 0, sizeof(*request));
		request->data = reinterpret_cast<void*>(data->promise);
		int connectResult = uv_tcp_connect(request, &data->socket->_socket, result->ai_addr, onConnect);
		if (connectResult != 0) {
			std::string error("uv_tcp_connect: " + std::string(uv_strerror(connectResult)));
			data->socket->_task->rejectPromise(data->promise, v8::Exception::Error(v8::String::NewFromUtf8(data->socket->_task->getIsolate(), error.c_str())));
		}
	}
	delete data;
}


void Socket::onConnect(uv_connect_t* request, int status) {
	promiseid_t promise = reinterpret_cast<intptr_t>(request->data);
	if (promise != -1) {
		Socket* socket = reinterpret_cast<Socket*>(request->handle->data);
		if (status == 0) {
			socket->_connected = true;
			socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), status));
		} else {
			std::string error("uv_tcp_connect: " + std::string(uv_strerror(status)));
			socket->_task->rejectPromise(promise, v8::String::NewFromUtf8(socket->_task->getIsolate(), error.c_str()));
		}
	}
}

void Socket::listen(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		int backlog = args[0]->ToInteger()->Value();
		if (socket->_onConnect.IsEmpty()) {
			v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[1].As<v8::Function>());
			socket->_onConnect = callback;
			int result = uv_listen(reinterpret_cast<uv_stream_t*>(&socket->_socket), backlog, onNewConnection);
			if (result != 0) {
				std::string error = "uv_listen: " + std::string(uv_strerror(result));
				args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), error.c_str(), v8::String::kNormalString, error.size())));
			}
			args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), result));
		} else {
			args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), "listen: Already listening.")));
		}
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
	if (Socket* socket = Socket::get(args.Data())) {
		v8::HandleScope handleScope(args.GetIsolate());
		Socket* client = new Socket(socket->_task);
		client->_direction = kAccept;
		promiseid_t promise = socket->_task->allocatePromise();
		v8::Handle<v8::Value> promiseObject = socket->_task->getPromise(promise);
		v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), client->_object);
		int status = uv_accept(reinterpret_cast<uv_stream_t*>(&socket->_socket), reinterpret_cast<uv_stream_t*>(&client->_socket));
		if (status == 0) {
			client->_connected = true;
			socket->_task->resolvePromise(promise, result);
		} else {
			std::string error = "uv_accept: " + std::string(uv_strerror(status));
			socket->_task->rejectPromise(promise, v8::String::NewFromUtf8(args.GetIsolate(), error.c_str(), v8::String::kNormalString, error.size()));
		}
		args.GetReturnValue().Set(promiseObject);
		client->release();
	}
}

void Socket::close(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		if (socket->_closePromise == -1) {
			socket->_closePromise = socket->_task->allocatePromise();
			args.GetReturnValue().Set(socket->_task->getPromise(socket->_closePromise));
			socket->close();
		}
	}
}

void Socket::shutdown(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		if (socket->_tls) {
			socket->_tls->shutdown();
			socket->processOutgoingTls();
			promiseid_t promise = socket->_task->allocatePromise();
			socket->_task->resolvePromise(promise, v8::Undefined(socket->_task->getIsolate()));
			args.GetReturnValue().Set(socket->_task->getPromise(promise));
		} else {
			uv_shutdown_t* request = new uv_shutdown_t();
			std::memset(request, 0, sizeof(*request));
			promiseid_t promise = socket->_task->allocatePromise();
			request->data = reinterpret_cast<void*>(promise);
			int result = uv_shutdown(request, reinterpret_cast<uv_stream_t*>(&socket->_socket), onShutdown);
			if (result != 0) {
				std::string error = "uv_shutdown: " + std::string(uv_strerror(result));
				socket->_task->rejectPromise(promise, v8::Exception::Error(v8::String::NewFromUtf8(socket->_task->getIsolate(), error.c_str())));
				delete request;
			}
			args.GetReturnValue().Set(socket->_task->getPromise(promise));
		}
	}
}

void Socket::onError(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[0].As<v8::Function>());
		socket->_onError = callback;
	}
}

void Socket::read(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > callback(args.GetIsolate(), args[0].As<v8::Function>());
		socket->_onRead = callback;
		int result = uv_read_start(reinterpret_cast<uv_stream_t*>(&socket->_socket), allocateBuffer, onRead);
		promiseid_t promise = socket->_task->allocatePromise();
		if (result != 0) {
			std::string error = "uv_read_start: " + std::string(uv_strerror(result));
			socket->_task->rejectPromise(promise, v8::String::NewFromUtf8(socket->_task->getIsolate(), error.c_str(), v8::String::kNormalString, error.size()));
		} else {
			socket->_task->resolvePromise(promise, v8::Undefined(socket->_task->getIsolate()));
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
		v8::Handle<v8::Value> data;

		if (readSize <= 0) {
			socket->_connected = false;
			v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onRead);
			if (!callback.IsEmpty()) {
				data = v8::Undefined(socket->_task->getIsolate());
				callback->Call(callback, 1, &data);
			}
			socket->close();
		} else {
			if (socket->_tls) {
				socket->reportTlsErrors();
				socket->_tls->writeEncrypted(buffer->base, readSize);
				if (socket->_startTlsPromise != -1) {
					TlsSession::HandshakeResult result = socket->_tls->handshake();
					if (result == TlsSession::kDone) {
						promiseid_t promise = socket->_startTlsPromise;
						socket->_startTlsPromise = -1;
						socket->_task->resolvePromise(promise, v8::Undefined(socket->_task->getIsolate()));
					} else if (result == TlsSession::kFailed) {
						promiseid_t promise = socket->_startTlsPromise;
						socket->_startTlsPromise = -1;
						char buffer[8192];
						if (socket->_tls->getError(buffer, sizeof(buffer))) {
							socket->_task->rejectPromise(promise, v8::String::NewFromUtf8(socket->_task->getIsolate(), buffer));
						} else {
							socket->_task->rejectPromise(promise, v8::Undefined(socket->_task->getIsolate()));
						}
					}
				} else {
					while (true) {
						char plain[8192];
						int result = socket->_tls->readPlain(plain, sizeof(plain));
						if (result > 0) {
							v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onRead);
							if (!callback.IsEmpty()) {
								data = v8::String::NewFromOneByte(socket->_task->getIsolate(), reinterpret_cast<const uint8_t*>(plain), v8::String::kNormalString, result);
								callback->Call(callback, 1, &data);
							}
						} else if (result == TlsSession::kReadFailed) {
							socket->reportTlsErrors();
							socket->close();
							break;
						} else if (result == TlsSession::kReadZero) {
							v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onRead);
							if (!callback.IsEmpty()) {
								data = v8::Undefined(socket->_task->getIsolate());
								callback->Call(callback, 1, &data);
							}
							break;
						} else {
							break;
						}
					}
				}
				if (socket->_tls) {
					socket->processOutgoingTls();
				}
			} else {
				v8::Local<v8::Function> callback = v8::Local<v8::Function>::New(socket->_task->getIsolate(), socket->_onRead);
				if (!callback.IsEmpty()) {
					data = v8::String::NewFromOneByte(socket->_task->getIsolate(), reinterpret_cast<const uint8_t*>(buffer->base), v8::String::kNormalString, readSize);
					callback->Call(callback, 1, &data);
				}
			}
		}
	}
	delete[] buffer->base;
}

void Socket::write(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Socket* socket = Socket::get(args.Data())) {
		promiseid_t promise = socket->_task->allocatePromise();
		args.GetReturnValue().Set(socket->_task->getPromise(promise));
		v8::Handle<v8::String> value = args[0].As<v8::String>();
		if (!value.IsEmpty() && value->IsString()) {
			if (socket->_tls) {
				socket->reportTlsErrors();
				int result;
				int length;
				if (value->ContainsOnlyOneByte()) {
					length = value->Length();
					std::vector<uint8_t> bytes(length);
					value->WriteOneByte(bytes.data(), 0, bytes.size(), v8::String::NO_NULL_TERMINATION);
					result = socket->_tls->writePlain(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				} else {
					v8::String::Utf8Value utf8(value);
					length = utf8.length();
					result = socket->_tls->writePlain(*utf8, utf8.length());
				}
				char buffer[8192];
				if (result <= 0 && socket->_tls->getError(buffer, sizeof(buffer))) {
					socket->_task->rejectPromise(promise, v8::String::NewFromUtf8(args.GetIsolate(), buffer));
				} else if (result < length) {
					socket->_task->rejectPromise(promise, v8::Integer::New(socket->_task->getIsolate(), result));
				} else {
					socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), result));
				}
				socket->processOutgoingTls();
			} else {
				int valueLength = value->Utf8Length();
				char* rawBuffer = new char[sizeof(uv_write_t) + valueLength];
				uv_write_t* request = reinterpret_cast<uv_write_t*>(rawBuffer);
				rawBuffer += sizeof(uv_write_t);
				value->WriteUtf8(rawBuffer, valueLength, 0, 0);

				uv_buf_t buffer;
				buffer.base = rawBuffer;
				buffer.len = valueLength;

				request->data = reinterpret_cast<void*>(promise);
				int result = uv_write(request, reinterpret_cast<uv_stream_t*>(&socket->_socket), &buffer, 1, onWrite);
				if (result != 0) {
					std::string error = "uv_write: " + std::string(uv_strerror(result));
					socket->_task->rejectPromise(promise, v8::String::NewFromUtf8(args.GetIsolate(), error.c_str(), v8::String::kNormalString, error.size()));
				}
			}
		} else {
			socket->_task->rejectPromise(promise, v8::Integer::New(args.GetIsolate(), -2));
		}
	}
}

void Socket::onWrite(uv_write_t* request, int status) {
	if (Socket* socket = reinterpret_cast<Socket*>(request->handle->data)) {
		v8::HandleScope handleScope(socket->_task->getIsolate());
		promiseid_t promise = reinterpret_cast<intptr_t>(request->data);
		if (promise != -1) {
			if (status == 0) {
				socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), status));
			} else {
				std::string error = "uv_write: " + std::string(uv_strerror(status));
				socket->_task->rejectPromise(promise, v8::String::NewFromUtf8(socket->_task->getIsolate(), error.c_str(), v8::String::kNormalString, error.size()));
			}
		}
	}
	delete[] reinterpret_cast<char*>(request);
}

void Socket::onClose(uv_handle_t* handle) {
	--_openCount;
	if (Socket* socket = reinterpret_cast<Socket*>(handle->data)) {
		if (socket->_closePromise != -1) {
			v8::HandleScope scope(socket->_task->getIsolate());
			promiseid_t promise = socket->_closePromise;
			socket->_closePromise = -1;
			socket->_connected = false;
			socket->_task->resolvePromise(promise, v8::Integer::New(socket->_task->getIsolate(), 0));
		}
		if (socket->_object.IsEmpty()) {
			delete socket;
		}
	}
}

void Socket::onShutdown(uv_shutdown_t* request, int status) {
	if (Socket* socket = reinterpret_cast<Socket*>(request->handle->data)) {
		promiseid_t promise = reinterpret_cast<intptr_t>(request->data);
		if (status == 0) {
			socket->_task->resolvePromise(promise, v8::Undefined(socket->_task->getIsolate()));
		} else {
			std::string error = "uv_shutdown: " + std::string(uv_strerror(status));
			socket->_task->rejectPromise(promise, v8::Exception::Error(v8::String::NewFromUtf8(socket->_task->getIsolate(), error.c_str())));
		}
	}
	delete request;
}

void Socket::getPeerName(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (Socket* socket = Socket::get(info.Data())) {
		struct sockaddr_in6 addr;
		int nameLength = sizeof(addr);
		if (uv_tcp_getpeername(&socket->_socket, reinterpret_cast<sockaddr*>(&addr), &nameLength) == 0) {
			char name[1024];
			if (static_cast<size_t>(nameLength) > sizeof(struct sockaddr_in)) {
				if (uv_ip6_name(&addr, name, sizeof(name)) == 0) {
					info.GetReturnValue().Set(v8::String::NewFromUtf8(info.GetIsolate(), name));
				}
			} else {
				if (uv_ip4_name(reinterpret_cast<struct sockaddr_in*>(&addr), name, sizeof(name)) == 0) {
					info.GetReturnValue().Set(v8::String::NewFromUtf8(info.GetIsolate(), name));
				}
			}
		}
	}
}

void Socket::getPeerCertificate(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (Socket* socket = Socket::get(info.Data())) {
		if (socket->_tls) {
			std::vector<char> buffer(128 * 1024);
			int result = socket->_tls->getPeerCertificate(buffer.data(), buffer.size());
			if (result > 0) {
				info.GetReturnValue().Set(v8::String::NewFromUtf8(info.GetIsolate(), buffer.data(), v8::String::kNormalString, result));
			}
		}
	}
}

void Socket::isConnected(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
	if (Socket* socket = Socket::get(info.Data())) {
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

Socket* Socket::get(v8::Handle<v8::Value> socketObject) {
	return reinterpret_cast<Socket*>(v8::Handle<v8::External>::Cast(socketObject)->Value());
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
