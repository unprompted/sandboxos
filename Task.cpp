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
		global->Set(v8::String::NewFromUtf8(_isolate, "receive"), v8::FunctionTemplate::New(_isolate, receive));
		v8::Local<v8::Context> context = v8::Context::New(_isolate, 0, global);
		v8::Context::Scope contextScope(context);
		v8::Handle<v8::String> script = loadFile(_isolate, _scriptName.c_str());
		if (!script.IsEmpty()) {
			execute(script);
		}
	}
	_isolate->Dispose();
	_isolate = 0;
	std::cout << "Task " << this << " is done.\n";
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
			std::cout << "adding " << task << " to " << parent << "\n";
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

void Task::send(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(args.GetIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(args.GetIsolate(), "stringify")));
	v8::Handle<v8::Value> arg = args[0];
	v8::String::Utf8Value value(stringify->Call(json, 1, &arg));

	{
		Lock lock(_mutex);
		Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
		if (task) {
			std::shared_ptr<Task> parent(task->_parent.lock());
			if (parent) {
				parent->_messages.push_back(*value);
				args.GetReturnValue().Set(v8::Boolean::New(args.GetIsolate(), true));
			}
		}
	}
}

void Task::receive(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	if (task) {
		Lock lock(_mutex);
		std::string message = task->_messages.front();
		task->_messages.pop_front();

		v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(args.GetIsolate(), "JSON"))->ToObject();
		v8::Handle<v8::Function> parse = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(args.GetIsolate(), "parse")));
		v8::Handle<v8::Value> contents(v8::String::NewFromUtf8(args.GetIsolate(), message.c_str(), v8::String::kNormalString, message.size()));
		args.GetReturnValue().Set(parse->Call(json, 1, &contents));
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
