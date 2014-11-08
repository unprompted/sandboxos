#ifndef INCLUDED_Task
#define INCLUDED_Task

#include "Mutex.h"

#include <iostream>
#include <list>
#include <string>
#include <v8.h>
#include <v8-platform.h>
#include <vector>

class Task;

struct uv_async_s; typedef struct uv_async_s uv_async_t;
struct uv_loop_s; typedef struct uv_loop_s uv_loop_t;
struct uv_work_s; typedef struct uv_work_s uv_work_t;

typedef int taskid_t;

class Message {
public:
	bool _isResponse;
	taskid_t _sender;
	taskid_t _recipient;
	std::string _data;
	std::string _result;
	int _promise;
};

class Task : public v8::Task {
public:
	Task(const char* scriptName = 0);
	~Task();
	void Run();

	int getId() const { return _id; }
	static int getCount() { return _count; }
	void kill();
private:
	static int _count;
	static Mutex _mutex;

	bool _killed;
	taskid_t _id;
	taskid_t _parent;
	std::string _scriptName;
	v8::Isolate* _isolate;

	std::list<Message> _messages;
	Mutex _messageMutex;
	uv_async_t* _asyncMessage;
	std::vector<v8::Persistent<v8::Promise::Resolver, v8::CopyablePersistentTraits<v8::Promise::Resolver> > > _promises;
	uv_loop_t* _loop;

	static void exit(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void print(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void sleep(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void startScript(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void invoke(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void parent(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args);

	static void kill(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void asyncMessage(uv_async_t* handle, int status);

	static void startInvoke(Message& message);
	static void finishInvoke(Message& message);

	v8::Handle<v8::Object> makeTaskObject(taskid_t id);

};

std::ostream& operator<<(std::ostream& stream, const Task& task);

#endif
