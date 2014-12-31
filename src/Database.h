#ifndef INCLUDED_Database
#define INCLUDED_Database

#include <lmdb.h>
#include <v8.h>

class Task;

class Database {
public:
	static void create(const v8::FunctionCallbackInfo<v8::Value>& args);
	static int getCount() { return _count; }

private:
	Database(Task* task);
	~Database();

	Task* _task;
	int _refCount = 1;
	v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> > _object;

	MDB_env* _environment;
	MDB_dbi _database;
	MDB_txn* _transaction;

	static int _count;

	static Database* get(v8::Handle<v8::Object> databaseObject);
	static void onRelease(const v8::WeakCallbackData<v8::Object, Database>& data);

	static void getter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info);
	static void setter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Value>& info);
	static void deleter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Boolean>& info);
	static void enumerator(const v8::PropertyCallbackInfo<v8::Array>& info);

	bool open(v8::Isolate* isolate, const char* path);

	bool checkError(const char* command, int result);

	void ref();
	void release();
};

#endif
