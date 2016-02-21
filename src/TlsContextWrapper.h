#ifndef INCLUDED_TlsContextWrapper
#define INCLUDED_TlsContextWrapper

#include <v8.h>

class Task;
class TlsContext;

class TlsContextWrapper {
public:
	static void create(const v8::FunctionCallbackInfo<v8::Value>& args);
	void close();

	static TlsContextWrapper* get(v8::Handle<v8::Value> value);

	static void setCertificate(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void setPrivateKey(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void addTrustedCertificate(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void onRelease(const v8::WeakCallbackData<v8::Object, TlsContextWrapper>& data);

	TlsContext* getContext() { return _context; }

	static int getCount();

private:
	TlsContextWrapper(Task* task);
	~TlsContextWrapper();

	static TlsContextWrapper* get(const v8::FunctionCallbackInfo<v8::Value>& args);

	TlsContext* _context = 0;
	Task* _task = 0;
	v8::Persistent<v8::Object> _object;
	int _refCount = 1;
	static int _count;

	void ref();
	void release();
};

#endif
