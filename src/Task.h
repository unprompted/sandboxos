#ifndef INCLUDED_Task
#define INCLUDED_Task

#include "Mutex.h"

#include <iostream>
#include <list>
#include <map>
#include <string>
#include <v8.h>
#include <v8-platform.h>
#include <vector>

struct ExportRecord;
struct ImportRecord;
class Task;

struct uv_async_s; typedef struct uv_async_s uv_async_t;
struct uv_loop_s; typedef struct uv_loop_s uv_loop_t;

typedef int taskid_t;
typedef int promiseid_t;
typedef int export_t;

enum MessageType {
	kSendMessage,
	kResolvePromise,
	kInvokeExport,
	kReleaseExport,
};

class Message {
public:
	MessageType _type;
	taskid_t _sender;
	taskid_t _recipient;
	std::vector<char> _data;
	std::vector<char> _result;
	int _promise;
	int _export;

	ImportRecord* _record;
};

class Task {
public:
	Task(const char* scriptName = 0);
	~Task();
	void run();

	int getId() const { return _id; }
	const std::string& getName() const { return _scriptName; }
	v8::Isolate* getIsolate() { return _isolate; }
	uv_loop_t* getLoop() { return _loop; }
	void kill();

	promiseid_t allocatePromise();
	v8::Handle<v8::Promise::Resolver> getPromise(promiseid_t promise);
	void resolvePromise(promiseid_t promise, v8::Handle<v8::Value> value);
	void rejectPromise(promiseid_t promise, v8::Handle<v8::Value> value);

	void setTrusted(bool trusted) { _trusted = trusted; }

	static int getCount() { return _count; }
	static Task* get(taskid_t id);
	static Task* get(v8::Isolate* isolate);

	export_t exportFunction(v8::Handle<v8::Function> function);
	static void invokeExport(const v8::FunctionCallbackInfo<v8::Value>& args);
	void addImport(v8::Handle<v8::Function> function, export_t exportId, taskid_t taskId);
	static void releaseExport(taskid_t taskId, export_t exportId);

private:
	static int _count;
	static Mutex _mutex;

	bool _trusted;
	bool _killed;
	taskid_t _id;
	taskid_t _parent;
	std::string _scriptName;
	v8::Isolate* _isolate;

	std::list<Message> _messages;
	Mutex _messageMutex;
	uv_async_t* _asyncMessage;
	std::map<promiseid_t, v8::Persistent<v8::Promise::Resolver, v8::CopyablePersistentTraits<v8::Promise::Resolver> > > _promises;
	promiseid_t _nextPromise;
	uv_loop_t* _loop;
	uv_thread_t _thread;

	std::map<export_t, ExportRecord*> _exports;
	export_t _nextExport;

	std::vector<ImportRecord*> _imports;

	int64_t _memoryAllocated;
	int64_t _memoryLimit;

	void execute(v8::Handle<v8::String> source, v8::Handle<v8::String> name);

	static void exit(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void print(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void sleep(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void startScript(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void invoke(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void invokeThen(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void run(void* data);

	static void parent(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args);

	static void kill(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void getStatistics(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void asyncMessage(uv_async_t* handle);

	static void startInvoke(Message& message);
	static void finishInvoke(Message& message);

	static void sleepCallback(uv_timer_t* timer);

	static void memoryAllocationCallback(v8::ObjectSpace objectSpace, v8::AllocationAction action, int size);

	v8::Handle<v8::Object> makeTaskObject(taskid_t id);

	friend struct ImportRecord;
};

class TaskTryCatch {
public:
	TaskTryCatch(Task* task);
	~TaskTryCatch();

private:
	v8::TryCatch _tryCatch;
	Task* _task;
};

std::ostream& operator<<(std::ostream& stream, const Task& task);

#endif
