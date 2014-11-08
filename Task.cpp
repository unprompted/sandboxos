#include "Task.h"

#include <v8.h>
#include <v8-platform.h>
#include <libplatform/libplatform.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

extern v8::Platform* gPlatform;

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
:	_self(this, NoDeleter()),
	_isolate(0) {
	++_count;
	if (scriptName) {
		_scriptName = scriptName;
	}
}

Task::~Task() {
	std::cout << "Task " << this << " destroyed.\n";
	--_count;
}

void Task::Run() {
	std::cout << "Task " << this << " running.\n";
	_isolate = v8::Isolate::New();
	_isolate->SetData(0, this);
	{
		v8::Isolate::Scope isolateScope(_isolate);
		v8::HandleScope handleScope(_isolate);

		v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
		global->Set(v8::String::NewFromUtf8(_isolate, "print"), v8::FunctionTemplate::New(_isolate, print));
		global->Set(v8::String::NewFromUtf8(_isolate, "sleep"), v8::FunctionTemplate::New(_isolate, sleep));
		global->Set(v8::String::NewFromUtf8(_isolate, "startScript"), v8::FunctionTemplate::New(_isolate, startScript));
		global->Set(v8::String::NewFromUtf8(_isolate, "send"), v8::FunctionTemplate::New(_isolate, send));
		v8::Local<v8::Context> context = v8::Context::New(_isolate, 0, global);
		v8::Context::Scope contextScope(context);
		v8::Handle<v8::String> script = loadFile(_isolate, _scriptName.c_str());
		if (!script.IsEmpty()) {
			execute(script);
		}

		while (true) {
			if (_messageSignal.wait()) {
				Message message;
				bool haveMessage = false;
				{
					Lock lock(_mutex);
					if (_messages.size()) {
						message = _messages.front();
						_messages.pop_front();
						haveMessage = true;
					}
				}
				if (haveMessage) {
					handleMessage(message);
				} else {
					break;
				}
			}
		}
	}
	_isolate->Dispose();
	_isolate = 0;
	std::cout << "Task " << this << " is done.\n";
}

void Task::handleMessage(const Message& message) {
	v8::HandleScope scope(_isolate);
	v8::Local<v8::Context> context = _isolate->GetCurrentContext();

	v8::Handle<v8::Function> function = v8::Local<v8::Function>::New(_isolate, message._callback);

	v8::Handle<v8::Object> object;
	if (!message._response) {
		v8::Local<v8::Value> value = context->Global()->Get(v8::String::NewFromUtf8(_isolate, "onMessage"));
		function = v8::Handle<v8::Function>::Cast(value);
		object = context->Global();
	} else {
		object = function;
	}

	v8::Handle<v8::Value> arg = v8::String::NewFromUtf8(_isolate, message._message.c_str(), v8::String::kNormalString, message._message.size());
	v8::Handle<v8::Value> result = function->Call(object, 1, &arg);

	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(_isolate, "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(_isolate, "stringify")));

	if (!message._response) {
		Message response;
		response._sender = _self;
		v8::String::Utf8Value responseValue(stringify->Call(json, 1, &result));
		response._message = toString(responseValue);
		response._response = true;
		response._callback = v8::Persistent<v8::Function>(_isolate, message._callback);
		message._sender.lock()->enqueueMessage(response);
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

void Task::startScript(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	Task* task = new Task();
	{
		Lock lock(_mutex);
		Task* parent = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
		if (parent) {
			task->_parent = parent->_self;
			parent->_children.push_back(task->_self);
		}
	}
	task->_scriptName = toString(v8::String::Utf8Value(args[0]));
	gPlatform->CallOnBackgroundThread(task, v8::Platform::kLongRunningTask);
}

void Task::sleep(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	usleep(static_cast<useconds_t>(1000000 * args[0].As<v8::Number>()->Value()));
}

void Task::enqueueMessage(const Message& message) {
	Lock lock(_mutex);
	_messages.push_back(message);
	_messageSignal.signal();
}

void Task::send(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::EscapableHandleScope scope(args.GetIsolate());
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(args.GetIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(args.GetIsolate(), "stringify")));
	v8::Handle<v8::Value> arg = args[0];
	v8::String::Utf8Value value(stringify->Call(json, 1, &arg));

	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	if (task) {
		std::shared_ptr<Task> parent(task->_parent.lock());
		if (parent) {
			Message message;
			message._message = *value;
			message._sender = task->_self;
			message._response = false;
			v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > function(args.GetIsolate(), args[1].As<v8::Function>());
			message._callback = function;
			parent->enqueueMessage(message);
			args.GetReturnValue().Set(v8::Boolean::New(args.GetIsolate(), true));
		}
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

const char* toString(const v8::String::Utf8Value& value) {
	return *value ? *value : "(null)";
}
