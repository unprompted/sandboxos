#include "Mutex.h"

#include <list>
#include <memory>
#include <string>
#include <v8.h>
#include <v8-platform.h>

class Task;

class Message {
public:
	std::weak_ptr<Task> _sender;
	std::string _message;
};

class Task : public v8::Task {
public:
	Task(const char* scriptName = 0);
	~Task();
	void Run();

	static int getCount() { return _count; }
private:
	static int _count;
	static Mutex _mutex;

	std::shared_ptr<Task> _self;
	std::string _scriptName;
	v8::Isolate* _isolate;
	std::weak_ptr<Task> _parent;
	std::list<std::weak_ptr<Task> > _children;
	std::list<std::string> _messages;

	static void print(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void sleep(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void startScript(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void send(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void receive(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void noDelete(Task* task) {}
};
