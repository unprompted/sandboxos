#include <v8.h>
#include <v8-platform.h>
#include <libplatform/libplatform.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

v8::Platform* gPlatform = 0;

void print(const v8::FunctionCallbackInfo<v8::Value>& args);
void sleep(const v8::FunctionCallbackInfo<v8::Value>& args);
void startScript(const v8::FunctionCallbackInfo<v8::Value>& args);
void execute(v8::Handle<v8::String> source);

const char* toString(const v8::String::Utf8Value& value) {
	return *value ? *value : "(null)";
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

class Task : public v8::Task {
public:
	Task();
	~Task();
	void Run();
	void setScript(const char* scriptName) { _scriptName = scriptName; }

	static int getCount() { return _count; }
private:
	static int _count;
	std::string _scriptName;
};

int Task::_count;

Task::Task() {
	++_count;
}

Task::~Task() {
	std::cout << "Task " << this << " destroyed.\n";
	--_count;
}

void Task::Run() {
	std::cout << "Task " << this << " running.\n";
	v8::Isolate* isolate = v8::Isolate::New();
	{
		v8::Isolate::Scope isolateScope(isolate);
		v8::HandleScope handleScope(isolate);

		v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
		global->Set(v8::String::NewFromUtf8(isolate, "print"), v8::FunctionTemplate::New(isolate, print));
		global->Set(v8::String::NewFromUtf8(isolate, "sleep"), v8::FunctionTemplate::New(isolate, sleep));
		global->Set(v8::String::NewFromUtf8(isolate, "startScript"), v8::FunctionTemplate::New(isolate, startScript));
		v8::Local<v8::Context> context0 = v8::Context::New(isolate, 0, global);
		context0->Enter();
		v8::Handle<v8::String> script = loadFile(isolate, _scriptName.c_str());
		if (!script.IsEmpty()) {
			execute(script);
		}
		context0->Exit();
	}
	isolate->Dispose();
	std::cout << "Task " << this << " is done.\n";
}

void print(const v8::FunctionCallbackInfo<v8::Value>& args) {
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

void sleep(const v8::FunctionCallbackInfo<v8::Value>& args) {
	usleep(static_cast<useconds_t>(1000000 * args[0].As<v8::Number>()->Value()));
}

void startScript(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	Task* task = new Task();
	task->setScript("test2.js");
	gPlatform->CallOnBackgroundThread(task, v8::Platform::kLongRunningTask);
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

int main(int argc, char* argv[]) {
	v8::V8::InitializeICU();
	gPlatform = v8::platform::CreateDefaultPlatform();
	v8::V8::InitializePlatform(gPlatform);
	v8::V8::Initialize();
	v8::V8::SetFlagsFromCommandLine(&argc, argv, true);

	int result = 0;
	Task* task = new Task();
	task->setScript("test.js");
	gPlatform->CallOnBackgroundThread(task, v8::Platform::kLongRunningTask);

	while (Task::getCount()) {
		usleep(10000);
	}

	v8::V8::Dispose();

	return result;
}
