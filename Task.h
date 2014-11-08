#ifndef INCLUDED_Task
#define INCLUDED_Task

#include "Mutex.h"
#include "Signal.h"

#include <list>
#include <string>
#include <v8.h>
#include <v8-platform.h>
#include <vector>

class Task;

typedef int taskid_t;

class Message {
public:
	bool _response;
	taskid_t _sender;
	taskid_t _recipient;
	std::string _message;
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
	Signal _messageSignal;
	std::vector<v8::Persistent<v8::Promise::Resolver, v8::CopyablePersistentTraits<v8::Promise::Resolver> > > _promises;

	void enqueueMessage(const Message& message);
	bool dequeueMessage(Message& message);
	void handleMessage(const Message& message);

	static void exit(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void print(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void sleep(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void startScript(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void invoke(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void parent(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args);

	static void kill(const v8::FunctionCallbackInfo<v8::Value>& args);

	v8::Handle<v8::Object> makeTaskObject(taskid_t id);
};

#endif
