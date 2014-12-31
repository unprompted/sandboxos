#ifndef INCLUDED_SecureSocket_commoncrypto
#define INCLUDED_SecureSocket_commoncrypto

#include <uv.h>
#include <v8.h>

#include <Security/SecureTransport.h>
#include <vector>

typedef int promiseid_t;
class Task;

class SecureSocket_commoncrypto {
public:
	void close();

	static void create(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void startTls(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void bind(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void connect(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void listen(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void accept(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void close(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void shutdown(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void read(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void write(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void getPeerName(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info);
	static void isConnected(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info);

	static int getCount() { return _count; }
	static int getOpenCount() { return _openCount; }

private:
	SecureSocket_commoncrypto(Task* task);
	~SecureSocket_commoncrypto();

	Task* _task;
	uv_tcp_t _socket;
	promiseid_t _promise;
	promiseid_t _startTlsPromise = -1;
	int _refCount = 1;
	bool _connected = false;

	SSLContextRef _context = 0;

	static int _count;
	static int _openCount;

	std::vector<char> _inBuffer;

	v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> > _object;

	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _onConnect;
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _onRead;

	v8::Handle<v8::Promise::Resolver> makePromise();

	static SecureSocket_commoncrypto* get(v8::Handle<v8::Object> socketObject);
	static void onClose(uv_handle_t* handle);
	static void onConnect(uv_connect_t* request, int status);
	static void onNewConnection(uv_stream_t* server, int status);

	static void allocateBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer);
	static void onRead(uv_stream_t* stream, ssize_t readSize, const uv_buf_t* buffer);
	static void onWrite(uv_write_t* request, int status);
	static void onRelease(const v8::WeakCallbackData<v8::Object, SecureSocket_commoncrypto>& data);

	static OSStatus writeInternal(SSLConnectionRef connection, const void* data, size_t* dataLength);
	static OSStatus readInternal(SSLConnectionRef connection, void* data, size_t* dataLength);

	void ref();
	void release();
};

#endif
