#include "Task.h"

#include <fstream>
#include <iostream>
#include <libplatform/libplatform.h>
#include <map>
#include <unistd.h>
#include <v8.h>
#include <v8-platform.h>

extern v8::Platform* gPlatform;
std::map<taskid_t, Task*> gTasks;
int gNextTaskId = 1;

v8::Handle<v8::String> loadFile(v8::Isolate* isolate, const char* fileName);
void execute(v8::Handle<v8::String> source);
const char* toString(const v8::String::Utf8Value& value);

int Task::_count;
Mutex Task::_mutex;

class NoDeleter {
public:
	void operator()(Task* task) {}
};

Task::Task(const char* scriptName)
:	_killed(false),
	_isolate(0) {

	{
		Lock lock(_mutex);
		do {
			_id = gNextTaskId++;
		} while (gTasks.find(_id) != gTasks.end());
		gTasks[_id] = this;
	}

	++_count;
	if (scriptName) {
		_scriptName = scriptName;
	}
}

Task::~Task() {
	std::cout << "Task " << this << " destroyed.\n";
	{
		Lock lock(_mutex);
		gTasks.erase(gTasks.find(_id));
		--_count;
	}
}

void Task::Run() {
	std::cout << "Task " << this << " running.\n";
	_isolate = v8::Isolate::New();
	_isolate->SetData(0, this);
	{
		v8::Isolate::Scope isolateScope(_isolate);
		v8::HandleScope handleScope(_isolate);

		v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
		global->Set(v8::String::NewFromUtf8(_isolate, "exit"), v8::FunctionTemplate::New(_isolate, exit));
		global->Set(v8::String::NewFromUtf8(_isolate, "print"), v8::FunctionTemplate::New(_isolate, print));
		global->Set(v8::String::NewFromUtf8(_isolate, "sleep"), v8::FunctionTemplate::New(_isolate, sleep));
		global->Set(v8::String::NewFromUtf8(_isolate, "startScript"), v8::FunctionTemplate::New(_isolate, startScript));
		global->Set(v8::String::NewFromUtf8(_isolate, "invoke"), v8::FunctionTemplate::New(_isolate, invoke));
		global->SetAccessor(v8::String::NewFromUtf8(_isolate, "parent"), parent);
		v8::Local<v8::Context> context = v8::Context::New(_isolate, 0, global);
		v8::Context::Scope contextScope(context);
		v8::Handle<v8::String> script = loadFile(_isolate, _scriptName.c_str());
		if (!script.IsEmpty()) {
			execute(script);
		}

		while (true) {
			std::cout << _id << " waiting for signal\n";
			if (_messageSignal.wait()) {
				if (_killed) {
					std::cout << "task killed, break\n";
					break;
				} else {
					std::cout << this << " got signal?\n";
					Message message;
					if (dequeueMessage(message)) {
						handleMessage(message);
					}
				}
			}
		}
	}
	_promises.clear();
	_isolate->Dispose();
	_isolate = 0;
	std::cout << "Task " << this << " is done.\n";
}

void Task::handleMessage(const Message& message) {
	v8::HandleScope scope(_isolate);
	v8::Local<v8::Context> context = _isolate->GetCurrentContext();

	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(_isolate, "JSON"))->ToObject();
	v8::Handle<v8::Function> parse = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(_isolate, "parse")));
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(_isolate, "stringify")));

	v8::Handle<v8::Value> argAsString = v8::String::NewFromUtf8(_isolate, message._message.c_str(), v8::String::kNormalString, message._message.size());
	v8::Handle<v8::Value> arg = parse->Call(json, 1, &argAsString);

	if (message._response) {
		v8::TryCatch tryCatch;
		v8::Handle<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(_isolate, _promises[message._promise]);
		resolver->Resolve(arg);
		_isolate->RunMicrotasks();
		if (tryCatch.HasCaught()) {
			v8::Local<v8::Value> exception = tryCatch.Exception();
			v8::String::Utf8Value exceptionText(exception);
			std::cerr << "Exception: " << toString(exceptionText) << "\n";
		}
	} else {
		v8::Local<v8::Function> function = v8::Handle<v8::Function>::Cast(context->Global()->Get(v8::String::NewFromUtf8(_isolate, "onMessage")));
		v8::Handle<v8::Value> result = function->Call(context->Global(), 1, &arg);

		Message response;
		response._sender = _id;
		v8::String::Utf8Value responseValue(stringify->Call(json, 1, &result));
		response._message = toString(responseValue);
		response._response = true;
		response._promise = message._promise;

		Lock lock(_mutex);
		if (Task* task = gTasks[message._sender]) {
			task->enqueueMessage(response);
		} else {
			std::cout << "No task!\n";
		}
	}
}

v8::Handle<v8::String> loadFile(v8::Isolate* isolate, const char* fileName) {
	v8::Handle<v8::String> value;
	std::ifstream file(fileName, std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
	std::streampos fileSize = file.tellg();
	if (fileSize >= 0) {
		file.seekg(0, std::ios_base::beg);
		char* buffer = new char[fileSize];
		file.read(buffer, fileSize);
		std::string contents(buffer, buffer + fileSize);
		value = v8::String::NewFromOneByte(isolate, reinterpret_cast<const uint8_t*>(buffer), v8::String::String::kNormalString, fileSize);
		delete[] buffer;
	}
	return value;
}

void Task::print(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(args.GetIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(args.GetIsolate(), "stringify")));
	for (int i = 0; i < args.Length(); i++) {
		if (i != 0) {
			std::cout << ' ';
		}
		v8::Handle<v8::Value> arg = args[i];
		v8::String::Utf8Value value(stringify->Call(json, 1, &arg));
		std::cout << toString(value);
	}
	std::cout << '\n';
}

void Task::exit(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	if (task) {
		v8::V8::TerminateExecution(task->_isolate);
		task->_killed = true;
	}
}

void Task::startScript(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* parent = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::EscapableHandleScope scope(args.GetIsolate());
	Task* task = new Task();
	{
		Lock lock(_mutex);
		task->_parent = parent->_id;
	}
	task->_scriptName = toString(v8::String::Utf8Value(args[0]));
	gPlatform->CallOnBackgroundThread(task, v8::Platform::kLongRunningTask);

	args.GetReturnValue().Set(parent->makeTaskObject(task->_id));
}

void Task::kill(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* self = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	int taskId = args.This().As<v8::Object>()->GetInternalField(0).As<v8::Integer>()->Value();

	Lock lock(_mutex);
	if (Task* task = gTasks[taskId]) {
		if (task->_parent == self->_id) {
			task->kill();
		} else {
			std::cerr << "Task " << taskId << " is not a child of task " << self->_id << "\n";
		}
	} else {
		std::cout << "Could not find task!\n";
	}
}

void Task::kill() {
	v8::V8::TerminateExecution(_isolate);
	_killed = true;
	_messageSignal.signal();
}

void Task::sleep(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	usleep(static_cast<useconds_t>(1000000 * args[0].As<v8::Number>()->Value()));
}

void Task::enqueueMessage(const Message& message) {
	std::cout << _id << "->enqueueMessage\n";
	Lock lock(_messageMutex);
	std::cout << _id << "->enqueueMessage (locked)\n";
	_messages.push_back(message);
	_messageSignal.signal();
}

bool Task::dequeueMessage(Message& message) {
	bool haveMessage = false;
	std::cout << _id << "->dequeueMessage\n";
	Lock lock(_messageMutex);
	std::cout << _id << "->dequeueMessage (locked)\n";

	while (_messages.size() && !_messages.front()._sender) {
		_messages.pop_front();
	}

	while (_messages.size()) {
		message = _messages.front();
		_messages.pop_front();

		Lock lock(_mutex);
		if (gTasks[message._sender]) {
			haveMessage = true;
			break;
		}
	}

	return haveMessage;
}

void execute(v8::Handle<v8::String> source) {
	v8::TryCatch tryCatch;
	v8::Handle<v8::Script> script = v8::Script::Compile(source, source);
	if (!script.IsEmpty()) {
		v8::Handle<v8::Value> result = script->Run();
		v8::String::Utf8Value stringResult(result);
		std::cout << "Script returned: " << toString(stringResult) << '\n';
	} else {
		std::cerr << "Failed to compile script.\n";
	}
	if (tryCatch.HasCaught()) {
		v8::Local<v8::Value> exception = tryCatch.Exception();
		v8::String::Utf8Value exceptionText(exception);
		std::cerr << "Exception: " << toString(exceptionText) << "\n";
	}
}

void Task::invoke(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::EscapableHandleScope scope(args.GetIsolate());
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(args.GetIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(args.GetIsolate(), "stringify")));
	v8::Handle<v8::Value> arg = args[0];
	v8::String::Utf8Value value(stringify->Call(json, 1, &arg));

	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	taskid_t recipientId = args.This().As<v8::Object>()->GetInternalField(0).As<v8::Integer>()->Value();

	if (task) {
		Message message;
		message._message = *value;
		message._sender = task->_id;
		message._response = false;
		v8::Persistent<v8::Promise::Resolver, v8::NonCopyablePersistentTraits<v8::Promise::Resolver> > promise(args.GetIsolate(), v8::Promise::Resolver::New(args.GetIsolate()));
		message._promise = task->_promises.size();
		task->_promises.push_back(promise);
		args.GetReturnValue().Set(promise);

		{
			Lock lock(_mutex);
			if (Task* recipient = gTasks[recipientId]) {
				recipient->enqueueMessage(message);
			}
		}
	}
}

void Task::parent(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	args.GetReturnValue().Set(task->makeTaskObject(task->_parent));
}

v8::Handle<v8::Object> Task::makeTaskObject(taskid_t id) {
	v8::Handle<v8::Object> taskObject;
	if (gTasks[id]) {
		v8::Handle<v8::ObjectTemplate> taskTemplate = v8::ObjectTemplate::New(_isolate);
		taskTemplate->Set(v8::String::NewFromUtf8(_isolate, "kill"), v8::FunctionTemplate::New(_isolate, Task::kill));
		taskTemplate->Set(v8::String::NewFromUtf8(_isolate, "invoke"), v8::FunctionTemplate::New(_isolate, Task::invoke));
		taskTemplate->SetInternalFieldCount(1);
		taskObject = taskTemplate->NewInstance();
		taskObject->SetInternalField(0, v8::Integer::New(_isolate, id));
	}
	return taskObject;
}

const char* toString(const v8::String::Utf8Value& value) {
	return *value ? *value : "(null)";
}
