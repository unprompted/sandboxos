#include "Task.h"

#include "Serialize.h"
#include "Socket.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <libplatform/libplatform.h>
#include <map>
#include <sys/types.h>
#include <uv.h>
#include <v8.h>
#include <v8-platform.h>

#ifdef WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

extern v8::Platform* gPlatform;
std::map<taskid_t, Task*> gTasks;
int gNextTaskId = 1;

v8::Handle<v8::String> loadFile(v8::Isolate* isolate, const char* fileName);
const char* toString(const v8::String::Utf8Value& value);

int Task::_count;
Mutex Task::_mutex;

struct SleepData {
	taskid_t _task;
	int _promise;
};

struct ExportRecord {
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _persistent;

	ExportRecord(v8::Isolate* isolate, v8::Handle<v8::Function> function)
	:	_persistent(isolate, function) {
	}

	static void onRelease(const v8::WeakCallbackData<v8::Function, ExportRecord >& data) {
		data.GetParameter()->_persistent.Reset();
		delete data.GetParameter();
	}
};

struct ImportRecord {
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _persistent;
	export_t _export;
	taskid_t _task;
	Task* _owner;
	int _useCount;

	ImportRecord(v8::Isolate* isolate, v8::Handle<v8::Function> function, export_t exportId, taskid_t taskId, Task* owner)
	:	_persistent(isolate, function),
		_export(exportId),
		_task(taskId),
		_owner(owner),
		_useCount(0) {
		_persistent.SetWeak(this, ImportRecord::onRelease);
	}

	void ref() {
		if (_useCount++ == 0) {
			// Make a strong ref again until an in-flight function call is finished.
			_persistent.ClearWeak();
		}
	}

	void release() {
		if (--_useCount == 0) {
			// All in-flight calls are finished.  Make weak.
			_persistent.SetWeak(this, ImportRecord::onRelease);
		}
	}

	static void onRelease(const v8::WeakCallbackData<v8::Function, ImportRecord >& data) {
		ImportRecord* import = data.GetParameter();
		Task::releaseExport(import->_task, import->_export);
		for (int i = 0; i < import->_owner->_imports.size(); ++i) {
			if (import->_owner->_imports[i] == import) {
				import->_owner->_imports.erase(import->_owner->_imports.begin() + i);
				break;
			}
		}
		import->_persistent.Reset();
		delete import;
	}
};

Task::Task(const char* scriptName)
:	_trusted(false),
	_killed(false),
	_isolate(0),
	_nextPromise(0),
	_nextExport(0),
	_memoryAllocated(0),
	_memoryLimit(64 * 1024 * 1024) {

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

void Task::run() {
	v8::Isolate::CreateParams params;
	//params.constraints.set_max_old_space_size(_memoryLimit);
	_isolate = v8::Isolate::New(params);
	_isolate->SetData(0, this);
	_isolate->AddMemoryAllocationCallback(memoryAllocationCallback, v8::kObjectSpaceAll, v8::kAllocationActionAll);
	{
		v8::Isolate::Scope isolateScope(_isolate);
		v8::HandleScope handleScope(_isolate);

		v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
		global->Set(v8::String::NewFromUtf8(_isolate, "print"), v8::FunctionTemplate::New(_isolate, print));
		global->Set(v8::String::NewFromUtf8(_isolate, "sleep"), v8::FunctionTemplate::New(_isolate, sleep));
		global->Set(v8::String::NewFromUtf8(_isolate, "invoke"), v8::FunctionTemplate::New(_isolate, invoke));
		global->SetAccessor(v8::String::NewFromUtf8(_isolate, "parent"), parent);

		if (_trusted) {
			global->Set(v8::String::NewFromUtf8(_isolate, "startScript"), v8::FunctionTemplate::New(_isolate, startScript));
			global->Set(v8::String::NewFromUtf8(_isolate, "exit"), v8::FunctionTemplate::New(_isolate, exit));
			global->Set(v8::String::NewFromUtf8(_isolate, "readLine"), v8::FunctionTemplate::New(_isolate, readLine));
			global->Set(v8::String::NewFromUtf8(_isolate, "readFile"), v8::FunctionTemplate::New(_isolate, readFile));
			global->Set(v8::String::NewFromUtf8(_isolate, "readDirectory"), v8::FunctionTemplate::New(_isolate, readDirectory));
			global->Set(v8::String::NewFromUtf8(_isolate, "makeDirectory"), v8::FunctionTemplate::New(_isolate, makeDirectory));
			global->Set(v8::String::NewFromUtf8(_isolate, "writeFile"), v8::FunctionTemplate::New(_isolate, writeFile));
			global->Set(v8::String::NewFromUtf8(_isolate, "renameFile"), v8::FunctionTemplate::New(_isolate, renameFile));
			global->Set(v8::String::NewFromUtf8(_isolate, "unlinkFile"), v8::FunctionTemplate::New(_isolate, unlinkFile));
			global->Set(v8::String::NewFromUtf8(_isolate, "Socket"), v8::FunctionTemplate::New(_isolate, Socket::create));
		}

		v8::Local<v8::Context> context = v8::Context::New(_isolate, 0, global);
		v8::Context::Scope contextScope(context);
		v8::Handle<v8::String> script = loadFile(_isolate, _scriptName.c_str());
		std::cout << "Running script " << _scriptName << "\n";
		if (!script.IsEmpty()) {
			execute(script, v8::String::NewFromUtf8(_isolate, _scriptName.c_str()));
		}

		uv_run(_loop, UV_RUN_DEFAULT);
	}
	_promises.clear();
	_exports.clear();
	_imports.clear();
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
		value = v8::String::NewFromOneByte(isolate, reinterpret_cast<const uint8_t*>(buffer), v8::String::kNormalString, fileSize);
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
	TaskTryCatch tryCatch(task);
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
	v8::HandleScope scope(args.GetIsolate());
	Task* task = new Task();
	{
		Lock lock(_mutex);
		task->_parent = parent->_id;
	}
	task->_scriptName = toString(v8::String::Utf8Value(args[0]));
	task->_trusted = parent->_trusted && !args[1].IsEmpty() && args[1]->BooleanValue();
	uv_thread_create(&task->_thread, run, task);

	args.GetReturnValue().Set(parent->makeTaskObject(task->_id));
}

void Task::run(void* data) {
	Task* task = reinterpret_cast<Task*>(data);
	task->run();
	delete task;
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
		//uv_thread_join(&_thread);
	}
}

void Task::sleep(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::HandleScope scope(args.GetIsolate());

	SleepData* data = new SleepData;
	data->_task = task->_id;
	data->_promise = task->allocatePromise();

	uv_timer_t* timer = new uv_timer_t();
	uv_timer_init(task->_loop, timer);
	timer->data = data;
	uv_timer_start(timer, sleepCallback, static_cast<uint64_t>(args[0].As<v8::Number>()->Value() * 1000), 0);

	args.GetReturnValue().Set(task->getPromise(data->_promise));
}

void Task::sleepCallback(uv_timer_t* timer) {
	SleepData* data = reinterpret_cast<SleepData*>(timer->data);

	Task* task = 0;
	{
		Lock lock(_mutex);
		task = gTasks[data->_task];
	}

	if (task) {
		task->resolvePromise(data->_promise, v8::Undefined(task->_isolate));
	}

	delete data;
}

void Task::execute(v8::Handle<v8::String> source, v8::Handle<v8::String> name) {
	TaskTryCatch tryCatch(this);
	v8::Handle<v8::Script> script = v8::Script::Compile(source, name);
	if (!script.IsEmpty()) {
		v8::Handle<v8::Value> result = script->Run();
		v8::String::Utf8Value stringResult(result);
		std::cout << "Script returned: " << toString(stringResult) << '\n';
	} else {
		std::cerr << "Failed to compile script.\n";
	}
}

void Task::invoke(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	TaskTryCatch tryCatch(task);
	v8::HandleScope scope(args.GetIsolate());

	taskid_t recipientId = args.This().As<v8::Object>()->GetInternalField(0).As<v8::Integer>()->Value();

	if (task) {
		Message message;
		Serialize::store(task, message._data, args[0]);
		message._sender = task->_id;
		message._recipient = recipientId;
		message._promise = task->allocatePromise();
		message._type = kSendMessage;
		args.GetReturnValue().Set(task->getPromise(message._promise));

		Task* recipient = 0;
		{
			Lock lock(_mutex);
			recipient = gTasks[recipientId];
		}

		if (recipient) {
			{
				Lock messageLock(recipient->_messageMutex);
				recipient->_messages.push_back(message);
			}

			uv_async_send(recipient->_asyncMessage);
		}
	}
}

struct InvokeRecord {
	v8::Persistent<v8::Function> _persistent;
};

void Task::invokeExport(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* sender = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	TaskTryCatch tryCatch(sender);
	v8::Handle<v8::Object> data = v8::Handle<v8::Object>::Cast(args.Data());
	export_t exportId = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "export"))->Int32Value();
	taskid_t recipientId = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "task"))->Int32Value();

	for (int i = 0; i < sender->_imports.size(); ++i) {
		if (sender->_imports[i]->_task == recipientId && sender->_imports[i]->_export == exportId) {
			sender->_imports[i]->ref();
			break;
		}
	}

	Message message;

	v8::Local<v8::Array> array = v8::Array::New(args.GetIsolate(), args.Length());
	for (int i = 0; i < args.Length(); ++i) {
		array->Set(i, args[i]);
	}

	Serialize::store(sender, message._data, array);
	message._sender = sender->_id;
	message._recipient = recipientId;
	message._promise = sender->allocatePromise();
	message._type = kInvokeExport;
	message._export = exportId;

	args.GetReturnValue().Set(sender->getPromise(message._promise));

	Task* recipient = 0;
	{
		Lock lock(_mutex);
		recipient = gTasks[recipientId];
	}

	if (recipient) {
		{
			Lock messageLock(recipient->_messageMutex);
			recipient->_messages.push_back(message);
		}
		uv_async_send(recipient->_asyncMessage);
	}
}

void Task::asyncMessage(uv_async_t* work) {
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
					if (nextMessage._type == kResolvePromise) {
						finishInvoke(nextMessage);
					} else if (nextMessage._type == kSendMessage) {
						startInvoke(nextMessage);
					} else if (nextMessage._type == kInvokeExport) {
						startInvoke(nextMessage);
					}
				}
			}
			task->_isolate->IdleNotification(100);
		}
	}
}

void Task::startInvoke(Message& message) {
	Task* task = 0;
	Task* from = 0;
	{
		Lock lock(_mutex);
		task = gTasks[message._recipient];
		from = gTasks[message._sender];
	}

	if (!task) {
		std::cerr << "processInvoke with invalid recipient\n";
		return;
	}

	TaskTryCatch tryCatch(task);
	v8::HandleScope scope(task->_isolate);
	v8::Local<v8::Context> context = task->_isolate->GetCurrentContext();

	v8::Handle<v8::Value> result;

	if (message._type == kSendMessage) {
		v8::Handle<v8::Value> args[2];
		args[0] = task->makeTaskObject(message._sender);
		args[1] = Serialize::load(task, from, message._data);
		v8::Handle<v8::Function> function = v8::Handle<v8::Function>::Cast(context->Global()->Get(v8::String::NewFromUtf8(task->_isolate, "onMessage")));
		result = function->Call(context->Global(), 2, &args[0]);
	} else if (message._type == kInvokeExport) {
		v8::Handle<v8::Array> arguments = v8::Handle<v8::Array>::Cast(Serialize::load(task, from, message._data));
		std::vector<v8::Handle<v8::Value> > array;
		for (int i = 0; i < arguments->Length(); ++i) {
			array.push_back(arguments->Get(i));
		}
		v8::Handle<v8::Function> function = v8::Local<v8::Function>::New(task->_isolate, task->_exports[message._export]->_persistent);
		if (function.IsEmpty()) {
			std::cout << "I COULD NOT FIND THE FUNCTION " << message._export << " ON " << task->_id << " (" << task->_exports.size() << ") " << task->_exports[message._export]->_persistent.IsEmpty() << "\n";
			result = v8::Undefined(task->_isolate);
		} else {
			result = function->Call(function, array.size(), &*array.begin());
		}

		for (int i = 0; i < from->_imports.size(); ++i) {
			if (from->_imports[i]->_task == message._recipient && from->_imports[i]->_export == message._export) {
				from->_imports[i]->release();
				break;
			}
		}
	}

	if (!result.IsEmpty() && result->IsPromise()) {
		// We're not going to serialize/deserialize a promise...
		v8::Handle<v8::Object> data = v8::Object::New(task->_isolate);
		data->Set(v8::String::NewFromUtf8(task->_isolate, "task"), v8::Int32::New(task->_isolate, message._sender));
		data->Set(v8::String::NewFromUtf8(task->_isolate, "promise"), v8::Int32::New(task->_isolate, message._promise));
		v8::Handle<v8::Function> then = v8::Function::New(task->_isolate, invokeThen, data);
		v8::Handle<v8::Promise> promise = v8::Handle<v8::Promise>::Cast(result);
		promise->Then(then);
	} else {
		if (result.IsEmpty() || result->IsUndefined() || result->IsNull()) {
			message._result.clear();
		} else {
			Serialize::store(task, message._result, result);
		}

		message._type = kResolvePromise;

		Task* sender = 0;
		{
			Lock lock(_mutex);
			sender = gTasks[message._sender];
		}

		if (sender) {
			Lock messageLock(sender->_messageMutex);
			sender->_messages.push_back(message);
		}
		uv_async_send(sender->_asyncMessage);
	}
}

void Task::invokeThen(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));

	Message message;
	message._recipient = task->_id;

	v8::Handle<v8::Object> data = v8::Handle<v8::Object>::Cast(args.Data());
	message._sender = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "task"))->Int32Value();
	Serialize::store(task, message._result, args[0]);
	message._type = kResolvePromise;
	message._promise = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "promise"))->Int32Value();

	Task* sender = 0;
	{
		Lock lock(_mutex);
		sender = gTasks[message._sender];
	}

	if (sender) {
		Lock messageLock(sender->_messageMutex);
		sender->_messages.push_back(message);
	}

	if (sender) {
		uv_async_send(sender->_asyncMessage);
	}
}

void Task::finishInvoke(Message& message) {
	Task* task = 0;
	Task* from = 0;
	{
		Lock lock(_mutex);
		task = gTasks[message._sender];
		from = gTasks[message._recipient];
	}

	if (!task) {
		std::cerr << "invokeComplete with invalid sender\n";
		return;
	}

	TaskTryCatch tryCatch(task);
	v8::HandleScope scope(task->_isolate);

	v8::Handle<v8::Value> arg;

	if (message._result.size()) {
		arg = Serialize::load(task, from, message._result);
	} else {
		arg = v8::Undefined(task->_isolate);
	}

	task->resolvePromise(message._promise, arg);
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
		taskTemplate->SetAccessorProperty(v8::String::NewFromUtf8(_isolate, "statistics"), v8::FunctionTemplate::New(_isolate, Task::getStatistics));
		taskTemplate->SetInternalFieldCount(1);
		taskObject = taskTemplate->NewInstance();
		taskObject->SetInternalField(0, v8::Integer::New(_isolate, id));
		taskObject->Set(v8::String::NewFromUtf8(_isolate, "id"), v8::Integer::New(_isolate, id));
	}
	return taskObject;
}

void Task::getStatistics(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::Handle<v8::Object> result = v8::Object::New(args.GetIsolate());
	result->Set(v8::String::NewFromUtf8(args.GetIsolate(), "sockets"), v8::Integer::New(args.GetIsolate(), Socket::getCount()));
	result->Set(v8::String::NewFromUtf8(args.GetIsolate(), "promises"), v8::Integer::New(args.GetIsolate(), task->_promises.size()));
	result->Set(v8::String::NewFromUtf8(args.GetIsolate(), "exports"), v8::Integer::New(args.GetIsolate(), task->_exports.size()));
	result->Set(v8::String::NewFromUtf8(args.GetIsolate(), "imports"), v8::Integer::New(args.GetIsolate(), task->_imports.size()));
	args.GetReturnValue().Set(result);
}

void Task::readFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> fileName = args[0]->ToString();

	v8::String::Utf8Value utf8FileName(fileName);
	std::ifstream file(*utf8FileName, std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
	std::streampos fileSize = file.tellg();
	if (fileSize >= 0 && fileSize < 4 * 1024 * 1024) {
		file.seekg(0, std::ios_base::beg);
		char* buffer = new char[fileSize];
		file.read(buffer, fileSize);
		std::string contents(buffer, buffer + fileSize);
		args.GetReturnValue().Set(v8::String::NewFromOneByte(args.GetIsolate(), reinterpret_cast<const uint8_t*>(buffer), v8::String::kNormalString, fileSize));
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

void Task::renameFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::HandleScope scope(args.GetIsolate());

	v8::String::Utf8Value oldName(args[0]->ToString());
	v8::String::Utf8Value newName(args[1]->ToString());

	uv_fs_t req;
	int result = uv_fs_rename(task->_loop, &req, *oldName, *newName, 0);
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), result));
}

void Task::unlinkFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::HandleScope scope(args.GetIsolate());

	v8::String::Utf8Value fileName(args[0]->ToString());

	uv_fs_t req;
	int result = uv_fs_unlink(task->_loop, &req, *fileName, 0);
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), result));
}

void Task::readDirectory(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> directory = args[0]->ToString();

	v8::Handle<v8::Array> array = v8::Array::New(args.GetIsolate(), 0);

#ifdef WIN32
	WIN32_FIND_DATA find;
	std::string pattern = *v8::String::Utf8Value(directory);
	pattern += "\\*";
	HANDLE handle = FindFirstFile(pattern.c_str(), &find);
	if (handle != INVALID_HANDLE_VALUE) {
		int index = 0;
		do {
			array->Set(v8::Integer::New(args.GetIsolate(), index++), v8::String::NewFromUtf8(args.GetIsolate(), find.cFileName));
		} while (FindNextFile(handle, &find) != 0);
		FindClose(handle);
	}
#else
	if (DIR* dir = opendir(*v8::String::Utf8Value(directory))) {
		int index = 0;
		while (struct dirent* entry = readdir(dir)) {
			array->Set(v8::Integer::New(args.GetIsolate(), index++), v8::String::NewFromUtf8(args.GetIsolate(), entry->d_name));
		}
		closedir(dir);
	}
#endif

	args.GetReturnValue().Set(array);
}

void Task::makeDirectory(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> directory = args[0]->ToString();

#ifdef WIN32
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), CreateDirectory(*v8::String::Utf8Value(directory), 0) == 0 ? -1 : 0));
#else
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), mkdir(*v8::String::Utf8Value(directory), 0777)));
#endif
}

void Task::readLine(const v8::FunctionCallbackInfo<v8::Value>& args) {
	std::string line;
	if (std::getline(std::cin, line)) {
		args.GetReturnValue().Set(v8::String::NewFromUtf8(args.GetIsolate(), line.c_str()));
	}
}

const char* toString(const v8::String::Utf8Value& value) {
	return *value ? *value : "(null)";
}

std::ostream& operator<<(std::ostream& stream, const Task& task) {
	if (&task) {
		return stream << "Task[" << task.getId() << ':' + task.getName() << ']';
	} else {
		return stream << "Task[Null]";
	}
}

Task* Task::get(taskid_t id) {
	Lock lock(_mutex);
	return gTasks[id];
}

Task* Task::get(v8::Isolate* isolate) {
	return reinterpret_cast<Task*>(isolate->GetData(0));
}

promiseid_t Task::allocatePromise() {
	promiseid_t promiseId = _nextPromise++;
	v8::Persistent<v8::Promise::Resolver, v8::NonCopyablePersistentTraits<v8::Promise::Resolver> > promise(_isolate, v8::Promise::Resolver::New(_isolate));
	_promises[promiseId] = promise;
	return promiseId;
}

v8::Handle<v8::Promise::Resolver> Task::getPromise(promiseid_t promise) {
	v8::Handle<v8::Promise::Resolver> result;
	if (!_promises[promise].IsEmpty()) {
		result = v8::Local<v8::Promise::Resolver>::New(_isolate, _promises[promise]);
	}
	return result;
}

void Task::resolvePromise(promiseid_t promise, v8::Handle<v8::Value> value) {
	TaskTryCatch tryCatch(this);
	if (!_promises[promise].IsEmpty()) {
		v8::HandleScope handleScope(_isolate);
		v8::Handle<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(_isolate, _promises[promise]);
		resolver->Resolve(value);
		_isolate->RunMicrotasks();
		_promises[promise].Reset();
		_promises.erase(promise);
	}
}

void Task::rejectPromise(promiseid_t promise, v8::Handle<v8::Value> value) {
	TaskTryCatch tryCatch(this);
	if (!_promises[promise].IsEmpty()) {
		v8::HandleScope handleScope(_isolate);
		v8::Handle<v8::Promise::Resolver> resolver = v8::Local<v8::Promise::Resolver>::New(_isolate, _promises[promise]);
		resolver->Reject(value);
		_isolate->RunMicrotasks();
		_promises[promise].Reset();
		_promises.erase(promise);
	}
}

TaskTryCatch::TaskTryCatch(Task* task)
:	_task(task) {
	_tryCatch.SetCaptureMessage(true);
	_tryCatch.SetVerbose(true);
}

TaskTryCatch::~TaskTryCatch() {
	if (_tryCatch.HasCaught()) {
		if (v8::Isolate* isolate = v8::Isolate::GetCurrent()) {
			if (Task* task = reinterpret_cast<Task*>(isolate->GetData(0))) {
				std::cerr << *task << ' ';
			}
		}
		std::cerr << "Exception:\n";

		v8::Handle<v8::Message> message(_tryCatch.Message());
		if (!message.IsEmpty()) {
			std::cerr
				<< toString(v8::String::Utf8Value(message->GetScriptResourceName()))
				<< ':'
				<< message->GetLineNumber()
				<< ": "
				<< toString(v8::String::Utf8Value(_tryCatch.Exception()))
				<< '\n';
			std::cerr << toString(v8::String::Utf8Value(message->GetSourceLine())) << '\n';

			for (int i = 0; i < message->GetStartColumn(); ++i) {
				std::cerr << ' ';
			}
			for (int i = message->GetStartColumn(); i < message->GetEndColumn(); ++i) {
				std::cerr << '^';
			}
			if (!message->GetStackTrace().IsEmpty()) {
				for (int i = 0; i < message->GetStackTrace()->GetFrameCount(); ++i) {
					std::cerr << "oops " << i << "\n";
				}
			}
			std::cerr << '\n';
		} else {
			std::cerr << toString(v8::String::Utf8Value(_tryCatch.Exception())) << '\n';
		}

		v8::String::Utf8Value stackTrace(_tryCatch.StackTrace());
		if (stackTrace.length() > 0) {
			std::cerr << *stackTrace << '\n';
		}
	}
}

void Task::memoryAllocationCallback(v8::ObjectSpace objectSpace, v8::AllocationAction action, int size) {
	if (v8::Isolate* isolate = v8::Isolate::GetCurrent()) {
		if (Task* task = reinterpret_cast<Task*>(isolate->GetData(0))) {
			if (action == v8::kAllocationActionAllocate) {
				task->_memoryAllocated += size;
				if (task->_memoryAllocated > task->_memoryLimit) {
					std::cout << *task << " OOM " << task->_memoryAllocated << " allocated " << task->_memoryLimit << " limit.\n";
					task->kill();
				}
			} else if (action == v8::kAllocationActionFree) {
				task->_memoryAllocated -= size;
			}
		}
	}
}

export_t Task::exportFunction(v8::Handle<v8::Function> function) {
	bool found = false;
	export_t exportId = -1;
	typedef std::map<export_t, ExportRecord*> ExportMap;
	for (ExportMap::iterator it = _exports.begin(); it != _exports.end(); ++it) {
		if (it->second->_persistent == function) {
			found = true;
			exportId = it->first;
			break;
		}
	}

	if (!found) {
		exportId = _nextExport++;
		ExportRecord* record = new ExportRecord(_isolate, function);
		_exports[exportId] = record;
	}

	return exportId;
}

void Task::releaseExport(taskid_t taskId, export_t exportId) {
	Lock lock(_mutex);
	if (Task* task = gTasks[taskId]) {
		task->_exports[exportId]->_persistent.Reset();
		delete task->_exports[exportId];
		task->_exports.erase(exportId);
	}
}

void Task::addImport(v8::Handle<v8::Function> function, export_t exportId, taskid_t taskId) {
	_imports.push_back(new ImportRecord(_isolate, function, exportId, taskId, this));
}
