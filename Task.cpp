#include "Task.h"

#include "Socket.h"

#include <fstream>
#include <iostream>
#include <libplatform/libplatform.h>
#include <map>
#include <unistd.h>
#include <uv.h>
#include <v8.h>
#include <v8-platform.h>

extern v8::Platform* gPlatform;
std::map<taskid_t, Task*> gTasks;
int gNextTaskId = 1;

v8::Handle<v8::String> loadFile(v8::Isolate* isolate, const char* fileName);
void execute(v8::Isolate* isolate, v8::Handle<v8::String> source);
const char* toString(const v8::String::Utf8Value& value);

int Task::_count;
Mutex Task::_mutex;

struct SleepData {
	taskid_t _task;
	int _promise;
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

	_loop = uv_loop_new();
	_asyncMessage = new uv_async_t();
	_asyncMessage->data = this;
	uv_async_init(_loop, _asyncMessage, asyncMessage);

	++_count;
	if (scriptName) {
		_scriptName = scriptName;
	}
}

Task::~Task() {
	Lock lock(_mutex);
	Lock messageLock(_messageMutex);
	uv_loop_delete(_loop);
	{
		gTasks.erase(gTasks.find(_id));
		--_count;
	}
}

void Task::Run() {
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
		global->Set(v8::String::NewFromUtf8(_isolate, "readFile"), v8::FunctionTemplate::New(_isolate, readFile));
		global->Set(v8::String::NewFromUtf8(_isolate, "writeFile"), v8::FunctionTemplate::New(_isolate, writeFile));
		global->Set(v8::String::NewFromUtf8(_isolate, "readLine"), v8::FunctionTemplate::New(_isolate, readLine));
		global->Set(v8::String::NewFromUtf8(_isolate, "Socket"), v8::FunctionTemplate::New(_isolate, createSocket));
		global->SetAccessor(v8::String::NewFromUtf8(_isolate, "parent"), parent);
		v8::Local<v8::Context> context = v8::Context::New(_isolate, 0, global);
		v8::Context::Scope contextScope(context);
		v8::Handle<v8::String> script = loadFile(_isolate, _scriptName.c_str());
		if (!script.IsEmpty()) {
			execute(_isolate, script);
		}

		uv_run(_loop, UV_RUN_DEFAULT);
	}
	_promises.clear();
	_isolate->Dispose();
	_isolate = 0;
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
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	std::cout << *task << '>';
	for (int i = 0; i < args.Length(); i++) {
		std::cout << ' ';
		v8::Handle<v8::Value> arg = args[i];
		v8::String::Utf8Value value(stringify->Call(json, 1, &arg));
		std::cout << toString(value);
	}
	std::cout << '\n';
}

void Task::exit(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	if (task) {
		task->kill();
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
			std::cerr << *task << " is not a child of " << *self << "\n";
		}
	} else {
		std::cout << "Could not find task!\n";
	}
}

void Task::kill() {
	if (!_killed) {
		v8::V8::TerminateExecution(_isolate);
		_killed = true;
		uv_async_send(_asyncMessage);
	}
}

void Task::sleep(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::HandleScope scope(args.GetIsolate());

	SleepData* data = new SleepData;
	data->_task = task->_id;
	data->_promise = task->_promises.size();

	uv_timer_t* timer = new uv_timer_t();
	uv_timer_init(task->_loop, timer);
	timer->data = data;
	uv_timer_start(timer, sleepCallback, static_cast<uint64_t>(args[0].As<v8::Number>()->Value() * 1000), 0);

	v8::Persistent<v8::Promise::Resolver, v8::NonCopyablePersistentTraits<v8::Promise::Resolver> > promise(args.GetIsolate(), v8::Promise::Resolver::New(args.GetIsolate()));
	task->_promises.push_back(promise);
	args.GetReturnValue().Set(promise);
}

void Task::sleepCallback(uv_timer_t* timer, int status) {
	SleepData* data = reinterpret_cast<SleepData*>(timer->data);

	Task* task = 0;
	{
		Lock lock(_mutex);
		task = gTasks[data->_task];
	}

	if (task) {
		v8::Handle<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(task->_isolate, task->_promises[data->_promise]);
		resolver->Resolve(v8::Undefined(task->_isolate));
		task->_isolate->RunMicrotasks();
		task->_promises[data->_promise].Reset();
	}

	delete data;
}

void execute(v8::Isolate* isolate, v8::Handle<v8::String> source) {
	v8::TryCatch tryCatch;
	tryCatch.SetVerbose(true);
	tryCatch.SetCaptureMessage(true);
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
		v8::String::Utf8Value exceptionText(exception->ToString());
		tryCatch.Message()->PrintCurrentStackTrace(isolate, stderr);
		std::cerr << __LINE__ << " - Exception: " << toString(exceptionText) << "\n";
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
		message._data = *value;
		message._sender = task->_id;
		message._recipient = recipientId;
		message._promise = task->_promises.size();
		message._isResponse = false;

		v8::Persistent<v8::Promise::Resolver, v8::NonCopyablePersistentTraits<v8::Promise::Resolver> > promise(args.GetIsolate(), v8::Promise::Resolver::New(args.GetIsolate()));
		task->_promises.push_back(promise);
		args.GetReturnValue().Set(promise);

		{
			Lock lock(_mutex);
			if (Task* recipient = gTasks[recipientId]) {
				{
					Lock messageLock(recipient->_messageMutex);
					recipient->_messages.push_back(message);
				}

				uv_async_send(recipient->_asyncMessage);
			}
		}
	}
}

void Task::asyncMessage(uv_async_t* work, int status) {
	bool moreMessages = true;
	Task* task = (Task*)work->data;
	if (task) {
		if (task->_killed) {
			uv_close(reinterpret_cast<uv_handle_t*>(task->_asyncMessage), 0);
		} else {
			while (moreMessages) {
				moreMessages = false;
				Message nextMessage;

				{
					Lock lock(task->_messageMutex);
					if (task->_messages.size()) {
						moreMessages = true;
						nextMessage = task->_messages.front();
						task->_messages.pop_front();
					}
				}

				if (moreMessages) {
					if (nextMessage._isResponse) {
						finishInvoke(nextMessage);
					} else {
						startInvoke(nextMessage);
					}
				}
			}
		}
	}
}

void Task::startInvoke(Message& message) {
	Task* task = 0;
	{
		Lock lock(_mutex);
		task = gTasks[message._recipient];
	}

	if (!task) {
		std::cerr << "processInvoke with invalid recipient\n";
		return;
	}

	v8::HandleScope scope(task->_isolate);
	v8::Local<v8::Context> context = task->_isolate->GetCurrentContext();

	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(task->_isolate, "JSON"))->ToObject();
	v8::Handle<v8::Function> parse = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(task->_isolate, "parse")));
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(task->_isolate, "stringify")));

	v8::Handle<v8::Value> argAsString = v8::String::NewFromUtf8(task->_isolate, message._data.c_str(), v8::String::kNormalString, message._data.size());
	v8::Handle<v8::Value> arg;

	{
		v8::TryCatch tryCatch;
		arg = parse->Call(json, 1, &argAsString);

		if (tryCatch.HasCaught()) {
			v8::Local<v8::Value> exception = tryCatch.Exception();
			v8::String::Utf8Value exceptionText(exception);
			std::cerr << __LINE__ << " - Exception: " << toString(exceptionText) << "\n";
		}
	}

	v8::Local<v8::Function> function = v8::Handle<v8::Function>::Cast(context->Global()->Get(v8::String::NewFromUtf8(task->_isolate, "onMessage")));
	v8::Handle<v8::Value> result = function->Call(context->Global(), 1, &arg);

	v8::String::Utf8Value responseValue(stringify->Call(json, 1, &result));
	message._result = toString(responseValue);

	message._isResponse = true;

	Task* sender = 0;
	{
		Lock lock(_mutex);
		sender = gTasks[message._sender];
		if (sender) {
			Lock messageLock(sender->_messageMutex);
			sender->_messages.push_back(message);
			uv_async_send(sender->_asyncMessage);
		}
	}
}

void Task::finishInvoke(Message& message) {
	Task* task = 0;
	{
		Lock lock(_mutex);
		task = gTasks[message._sender];
	}

	if (!task) {
		std::cerr << "invokeComplete with invalid sender\n";
		return;
	}

	v8::HandleScope scope(task->_isolate);
	v8::Local<v8::Context> context = task->_isolate->GetCurrentContext();

	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(task->_isolate, "JSON"))->ToObject();
	v8::Handle<v8::Function> parse = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(task->_isolate, "parse")));

	v8::Handle<v8::Value> argAsString = v8::String::NewFromUtf8(task->_isolate, message._result.c_str(), v8::String::kNormalString, message._result.size());
	v8::Handle<v8::Value> arg = parse->Call(json, 1, &argAsString);

	v8::TryCatch tryCatch;
	v8::Handle<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(task->_isolate, task->_promises[message._promise]);
	resolver->Resolve(arg);
	task->_isolate->RunMicrotasks();
	if (tryCatch.HasCaught()) {
		v8::Local<v8::Value> exception = tryCatch.Exception();
		v8::String::Utf8Value exceptionText(exception);
		std::cerr << __LINE__ << " - Exception: " << toString(exceptionText) << "\n";
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

void Task::readFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> fileName = args[0]->ToString();

	v8::String::Utf8Value utf8FileName(fileName);
	std::ifstream file(*utf8FileName, std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
	std::streampos fileSize = file.tellg();
	if (fileSize >= 0) {
		file.seekg(0, std::ios_base::beg);
		char* buffer = new char[fileSize];
		file.read(buffer, fileSize);
		std::string contents(buffer, buffer + fileSize);
		args.GetReturnValue().Set(v8::String::NewFromOneByte(args.GetIsolate(), reinterpret_cast<const uint8_t*>(buffer), v8::String::String::kNormalString, fileSize));
		delete[] buffer;
	}
}

void Task::writeFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> fileName = args[0]->ToString();
	v8::Handle<v8::String> contents = args[1]->ToString();

	v8::String::Utf8Value utf8FileName(fileName);
	std::ofstream file(*utf8FileName, std::ios_base::out | std::ios_base::binary);

	v8::String::Utf8Value utf8Contents(contents);
	if (!file.write(*utf8Contents, utf8Contents.length())) {
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), -1));
	}
}

void Task::readLine(const v8::FunctionCallbackInfo<v8::Value>& args) {
	std::string line;
	if (std::getline(std::cin, line)) {
		args.GetReturnValue().Set(v8::String::NewFromUtf8(args.GetIsolate(), line.c_str()));
	}
}

void Task::createSocket(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	args.GetReturnValue().Set(Socket::create(task));
}

const char* toString(const v8::String::Utf8Value& value) {
	return *value ? *value : "(null)";
}

std::ostream& operator<<(std::ostream& stream, const Task& task) {
	if (&task) {
		return stream << "Task[" << task.getId() << ']';
	} else {
		return stream << "Task[Null]";
	}
}

socketid_t Task::allocateSocket() {
	socketid_t id = _sockets.size();
	_sockets.push_back(new Socket(this));
	return id;
}

Task* Task::get(taskid_t id) {
	Lock lock(_mutex);
	return gTasks[id];
}

Socket* Task::getSocket(socketid_t id) {
	return _sockets[id];
}

promiseid_t Task::allocatePromise() {
	promiseid_t promiseId = _promises.size();
	v8::Persistent<v8::Promise::Resolver, v8::NonCopyablePersistentTraits<v8::Promise::Resolver> > promise(_isolate, v8::Promise::Resolver::New(_isolate));
	_promises.push_back(promise);
	return promiseId;
}

v8::Handle<v8::Promise::Resolver> Task::getPromise(promiseid_t promise) {
	v8::Handle<v8::Promise::Resolver> result;
	if (promise >= 0 && promise < _promises.size() && !_promises[promise].IsEmpty()) {
		result = v8::Local<v8::Promise::Resolver>::New(_isolate, _promises[promise]);
	}
	return result;
}

void Task::resolvePromise(promiseid_t promise, v8::Handle<v8::Value> value) {
	if (promise >= 0 && promise < _promises.size() && !_promises[promise].IsEmpty()) {
		v8::HandleScope handleScope(_isolate);
		v8::Handle<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(_isolate, _promises[promise]);
		resolver->Resolve(value);
		_isolate->RunMicrotasks();
		_promises[promise].Reset();
	}
}

void Task::rejectPromise(promiseid_t promise, v8::Handle<v8::Value> value) {
	if (promise >= 0 && promise < _promises.size() && !_promises[promise].IsEmpty()) {
		v8::HandleScope handleScope(_isolate);
		v8::Handle<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(_isolate, _promises[promise]);
		resolver->Reject(value);
		_isolate->RunMicrotasks();
		_promises[promise].Reset();
	}
}
