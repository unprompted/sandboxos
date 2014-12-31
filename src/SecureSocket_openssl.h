#ifndef INCLUDED_SecureSocket_openssl
#define INCLUDED_SecureSocket_openssl

#include <uv.h>
#include <v8.h>

typedef int promiseid_t;
class Task;

struct ssl_ctx;
typedef struct ssl_ctx_st SSL_CTX;
struct bio_st;
typedef struct bio_st BIO;
struct ssl_st;
typedef struct ssl_st SSL;

class SecureSocket_openssl {
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
	SecureSocket_openssl(Task* task);
	~SecureSocket_openssl();

	void update();

	Task* _task;
	uv_tcp_t _socket;
	promiseid_t _promise;
	promiseid_t _startTlsPromise = -1;
	int _refCount = 1;
	bool _connected = false;

	SSL_CTX* _context = 0;
	BIO* _bioIn = 0;
	BIO* _bioOut = 0;
	SSL* _ssl = 0;

	static int _count;
	static int _openCount;

	v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> > _object;

	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _onConnect;
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _onRead;

	v8::Handle<v8::Promise::Resolver> makePromise();

	static SecureSocket_openssl* get(v8::Handle<v8::Object> socketObject);
	static void onClose(uv_handle_t* handle);
	static void onConnect(uv_connect_t* request, int status);
	static void onNewConnection(uv_stream_t* server, int status);

	static void allocateBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer);
	static void onRead(uv_stream_t* stream, ssize_t readSize, const uv_buf_t* buffer);
	static void onWrite(uv_write_t* request, int status);
	static void onRelease(const v8::WeakCallbackData<v8::Object, SecureSocket_openssl>& data);

	void ref();
	void release();
};

#endif