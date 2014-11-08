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
		global->Set(v8::String::NewFromUtf8(_isolate, "send"), v8::FunctionTemplate::New(_isolate, send));
		global->Set(v8::String::NewFromUtf8(_isolate, "syscall"), v8::FunctionTemplate::New(_isolate, syscall));
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
	_callbacks.clear();
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

	if (message._callback != -1) {
		v8::Handle<v8::Function> function = v8::Local<v8::Function>::New(_isolate, _callbacks[message._callback]);

		v8::Handle<v8::Object> object;
		if (!message._response) {
			v8::Local<v8::Value> value = context->Global()->Get(v8::String::NewFromUtf8(_isolate, "onMessage"));
			function = v8::Handle<v8::Function>::Cast(value);
			object = context->Global();
		} else {
			object = function;
		}


		v8::Handle<v8::Value> argAsString = v8::String::NewFromUtf8(_isolate, message._message.c_str(), v8::String::kNormalString, message._message.size());
		v8::Handle<v8::Value> arg = parse->Call(json, 1, &argAsString);
		v8::Handle<v8::Value> result = function->Call(object, 1, &arg);

		if (!message._response) {
			Message response;
			response._sender = _id;
			v8::String::Utf8Value responseValue(stringify->Call(json, 1, &result));
			response._message = toString(responseValue);
			response._response = true;
			response._callback = message._callback;

			Lock lock(_mutex);
			if (Task* task = gTasks[message._sender]) {
				task->enqueueMessage(response);
			}
		}
	} else if (message._promise != -1) {
		v8::Handle<v8::Value> argAsString = v8::String::NewFromUtf8(_isolate, message._message.c_str(), v8::String::kNormalString, message._message.size());
		v8::Handle<v8::Value> arg = parse->Call(json, 1, &argAsString);

		if (message._response) {
			v8::TryCatch tryCatch;
			std::cout << "handling response?\n";
			v8::Handle<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(_isolate, _promises[message._promise]);
			std::cout << "Resolving!\n";
			resolver->Resolve(arg);
			_isolate->RunMicrotasks();
			if (tryCatch.HasCaught()) {
				v8::Local<v8::Value> exception = tryCatch.Exception();
				v8::String::Utf8Value exceptionText(exception);
				std::cerr << "Exception: " << toString(exceptionText) << "\n";
			}
			std::cout << "Resolved!\n";
			//_promises.clear();
			//std::cout << "cleared\n";
		} else {
			v8::Local<v8::Function> function = v8::Handle<v8::Function>::Cast(context->Global()->Get(v8::String::NewFromUtf8(_isolate, "onMessage")));
			v8::Handle<v8::Value> result = function->Call(context->Global(), 1, &arg);

			Message response;
			response._sender = _id;
			v8::String::Utf8Value responseValue(stringify->Call(json, 1, &result));
			response._message = toString(responseValue);
			std::cout << "going to try to resolve to " << response._message << "\n";
			response._response = true;
			response._callback = -1;
			response._promise = message._promise;

			Lock lock(_mutex);
			if (Task* task = gTasks[message._sender]) {
				task->enqueueMessage(response);
				std::cout << "response enqueued on " << task << "\n";
			} else {
				std::cout << "No task!\n";
			}
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
	v8::EscapableHandleScope scope(args.GetIsolate());
	Task* task = new Task();
	{
		Lock lock(_mutex);
		Task* parent = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
		if (parent) {
			task->_parent = parent->_id;
		}
	}
	task->_scriptName = toString(v8::String::Utf8Value(args[0]));
	gPlatform->CallOnBackgroundThread(task, v8::Platform::kLongRunningTask);

	v8::Handle<v8::ObjectTemplate> taskTemplate =  v8::ObjectTemplate::New(args.GetIsolate());
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "kill"), v8::FunctionTemplate::New(args.GetIsolate(), Task::kill));
	taskTemplate->SetInternalFieldCount(1);
	v8::Handle<v8::Object> taskObject = taskTemplate->NewInstance();
	taskObject->SetInternalField(0, v8::Integer::New(args.GetIsolate(), task->_id));
	args.GetReturnValue().Set(taskObject);
}

void Task::kill(const v8::FunctionCallbackInfo<v8::Value>& args) {
	int taskId = args.This().As<v8::Object>()->GetInternalField(0).As<v8::Integer>()->Value();
	std::cout << "kill?  KILL KILL!?!?! " << taskId << "\n";

	Lock lock(_mutex);
	if (Task* task = gTasks[taskId]) {
		task->kill();
	} else {
		std::cout << "Could not find task!\n";
	}
}

void Task::kill() {
	v8::V8::TerminateExecution(_isolate);
	_killed = true;
	std::cout << "signalling " << this << "\n";
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

		if (gTasks[message._sender]) {
			haveMessage = true;
			break;
		}
	}

	return haveMessage;
}

void Task::send(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(args.GetIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(args.GetIsolate(), "stringify")));
	v8::Handle<v8::Value> arg = args[0];
	v8::String::Utf8Value value(stringify->Call(json, 1, &arg));

	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	if (task) {
		bool enqueued = false;
		Message message;
		message._message = *value;
		message._sender = task->_id;
		message._response = false;
		v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > function(args.GetIsolate(), args[1].As<v8::Function>());
		task->_callbacks.push_back(function);
		message._callback = task->_callbacks.size() - 1;
		message._promise = -1;

		{
			Lock lock(_mutex);
			if (Task* parent = gTasks[task->_parent]) {
				parent->enqueueMessage(message);
				enqueued = true;
			}
		}
		args.GetReturnValue().Set(v8::Boolean::New(args.GetIsolate(), enqueued));
	}
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

void Task::syscall(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::EscapableHandleScope scope(args.GetIsolate());
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(args.GetIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(args.GetIsolate(), "stringify")));
	v8::Handle<v8::Value> arg = args[0];
	v8::String::Utf8Value value(stringify->Call(json, 1, &arg));

	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	if (task) {
		Message message;
		message._message = *value;
		message._sender = task->_id;
		message._response = false;
		std::cout << "making resolver\n";
		v8::Persistent<v8::Promise::Resolver, v8::NonCopyablePersistentTraits<v8::Promise::Resolver> > promise(args.GetIsolate(), v8::Promise::Resolver::New(args.GetIsolate()));
		std::cout << "made resolver\n";

		message._callback = -1;
		message._promise = task->_promises.size();

		task->_promises.push_back(promise);
		args.GetReturnValue().Set(promise);

		{
			Lock lock(_mutex);
			if (Task* parent = gTasks[task->_parent]) {
				parent->enqueueMessage(message);
			}
		}
	}
}

const char* toString(const v8::String::Utf8Value& value) {
	return *value ? *value : "(null)";
}
