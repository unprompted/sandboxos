#include "Task.h"

#include "Database.h"
#include "File.h"
#include "Serialize.h"
#include "Socket.h"
#include "TaskStub.h"
#include "TaskTryCatch.h"
#include "TlsContextWrapper.h"

#include <algorithm>
#include <assert.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <libplatform/libplatform.h>
#include <map>
#include <sys/types.h>
#include <uv.h>
#include <v8.h>
#include <v8-platform.h>

#ifdef _WIN32
static const int STDIN_FILENO = 0;
#else
#include <unistd.h>
#endif

extern v8::Platform* gPlatform;
int gNextTaskId = 1;

int Task::_count;

struct ExportRecord {
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _persistent;
	int _useCount;

	ExportRecord(v8::Isolate* isolate, v8::Handle<v8::Function> function)
	:	_persistent(isolate, function),
		_useCount(0) {
	}

	void ref() {
		++_useCount;
	}

	bool release() {
		return --_useCount == 0;
	}
};

struct ImportRecord {
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _persistent;
	exportid_t _export;
	taskid_t _task;
	Task* _owner;
	int _useCount;

	ImportRecord(v8::Isolate* isolate, v8::Handle<v8::Function> function, exportid_t exportId, taskid_t taskId, Task* owner)
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
		import->_owner->releaseExport(import->_task, import->_export);
		for (size_t i = 0; i < import->_owner->_imports.size(); ++i) {
			if (import->_owner->_imports[i] == import) {
				import->_owner->_imports.erase(import->_owner->_imports.begin() + i);
				break;
			}
		}
		import->_persistent.Reset();
		delete import;
	}
};

Task::Task() {
	_loop = uv_loop_new();
	++_count;
	v8::Isolate::CreateParams options;
	options.array_buffer_allocator = &_allocator;
	_isolate = v8::Isolate::New(options);
	_isolate->SetData(0, this);
}

Task::~Task() {
	{
		v8::Isolate::Scope isolateScope(_isolate);
		v8::HandleScope handleScope(_isolate);
		_context.Reset();
	}

	_isolate->Dispose();
	_isolate = 0;

	uv_loop_delete(_loop);
	--_count;
}

void Task::run() {
	{
		v8::Isolate::Scope isolateScope(_isolate);
		v8::HandleScope handleScope(_isolate);
		v8::Context::Scope contextScope(v8::Local<v8::Context>::New(_isolate, _context));
		uv_run(_loop, UV_RUN_DEFAULT);
	}
	_promises.clear();
	_exports.clear();
	_imports.clear();
}

v8::Handle<v8::String> Task::loadFile(v8::Isolate* isolate, const char* fileName) {
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

void Task::activate() {
	v8::Isolate::Scope isolateScope(_isolate);
	v8::HandleScope handleScope(_isolate);

	v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();

	if (!_importObject.IsEmpty()) {
		v8::Local<v8::Object> imports(_importObject.Get(_isolate));
		v8::Handle<v8::Array> keys = imports->GetOwnPropertyNames();
		for (size_t i = 0; i < keys->Length(); ++i) {
			v8::String::Utf8Value value(keys->Get(i).As<v8::String>());
			std::cerr << *value << "\n";
			global->SetAccessor(keys->Get(i).As<v8::String>(), getImportProperty);
		}
	}

	global->Set(v8::String::NewFromUtf8(_isolate, "print"), v8::FunctionTemplate::New(_isolate, print));
	global->Set(v8::String::NewFromUtf8(_isolate, "setTimeout"), v8::FunctionTemplate::New(_isolate, setTimeout));
	global->Set(v8::String::NewFromUtf8(_isolate, "require"), v8::FunctionTemplate::New(_isolate, require));
	global->SetAccessor(v8::String::NewFromUtf8(_isolate, "parent"), parent);
	global->Set(v8::String::NewFromUtf8(_isolate, "exit"), v8::FunctionTemplate::New(_isolate, exit));
	global->Set(v8::String::NewFromUtf8(_isolate, "utf8Length"), v8::FunctionTemplate::New(_isolate, utf8Length));
	global->SetAccessor(v8::String::NewFromUtf8(_isolate, "exports"), getExports, setExports);
	global->SetAccessor(v8::String::NewFromUtf8(_isolate, "imports"), getImports);
	global->SetAccessor(v8::String::NewFromUtf8(_isolate, "version"), version);
	if (_trusted) {
		global->Set(v8::String::NewFromUtf8(_isolate, "Database"), v8::FunctionTemplate::New(_isolate, Database::create));
		global->Set(v8::String::NewFromUtf8(_isolate, "Socket"), v8::FunctionTemplate::New(_isolate, Socket::create));
		global->Set(v8::String::NewFromUtf8(_isolate, "Task"), v8::FunctionTemplate::New(_isolate, TaskStub::create));
		global->Set(v8::String::NewFromUtf8(_isolate, "TlsContext"), v8::FunctionTemplate::New(_isolate, TlsContextWrapper::create));
		File::configure(_isolate, global);
	}

	v8::Local<v8::Context> context = v8::Context::New(_isolate, 0, global);
	_context = v8::Persistent<v8::Context, v8::CopyablePersistentTraits<v8::Context> >(_isolate, context);
}

void Task::activate(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = Task::get(args.GetIsolate());
	task->activate();
}

void Task::print(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	v8::Handle<v8::Object> json = context->Global()->Get(v8::String::NewFromUtf8(args.GetIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(args.GetIsolate(), "stringify")));
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	TaskTryCatch tryCatch(task);
	std::cout << "Task[" << task << ':' << task->_scriptName << "]>";
	for (int i = 0; i < args.Length(); i++) {
		std::cout << ' ';
		v8::Handle<v8::Value> arg = args[i];
		v8::String::Utf8Value value(stringify->Call(json, 1, &arg));
		std::cout << (*value ? *value : "(null)");
	}
	std::cout << '\n';
}

struct TimeoutData {
	Task* _task;
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _callback;
};

void Task::setTimeout(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));

	TimeoutData* timeout = new TimeoutData();
	timeout->_task = task;

	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > function(args.GetIsolate(), v8::Handle<v8::Function>::Cast(args[0]));
	timeout->_callback = function;

	uv_timer_t* timer = new uv_timer_t();
	uv_timer_init(task->_loop, timer);
	timer->data = timeout;
	uv_timer_start(timer, timeoutCallback, static_cast<uint64_t>(args[1].As<v8::Number>()->Value()), 0);
}

void Task::timeoutCallback(uv_timer_t* handle) {
	TimeoutData* timeout = reinterpret_cast<TimeoutData*>(handle->data);
	TaskTryCatch tryCatch(timeout->_task);
	v8::HandleScope scope(timeout->_task->_isolate);
	v8::Handle<v8::Function> function = v8::Local<v8::Function>::New(timeout->_task->_isolate, timeout->_callback);
	function->Call(v8::Undefined(timeout->_task->_isolate), 0, 0);
	delete timeout;
}

void Task::utf8Length(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	TaskTryCatch tryCatch(task);
	v8::HandleScope scope(task->_isolate);
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), args[0].As<v8::String>()->Utf8Length()));
}

void Task::exit(const v8::FunctionCallbackInfo<v8::Value>& args) {
	::exit(args[0]->Int32Value());
}

void Task::kill() {
	if (!_killed && _isolate) {
		_killed = true;
		v8::V8::TerminateExecution(_isolate);
	}
}

void Task::execute(const char* fileName) {
	v8::Isolate::Scope isolateScope(_isolate);
	v8::HandleScope handleScope(_isolate);
	v8::Context::Scope contextScope(v8::Local<v8::Context>::New(_isolate, _context));

	v8::Handle<v8::String> name = v8::String::NewFromUtf8(_isolate, fileName);

	v8::Handle<v8::String> source = loadFile(_isolate, fileName);
	std::cout << "Running script " << fileName << "\n";
	if (!_scriptName.size()) {
		_scriptName = fileName;
	}
	if (!source.IsEmpty()) {
		v8::Handle<v8::Script> script = v8::Script::Compile(source, name);
		if (!script.IsEmpty()) {
			script->Run();
			std::cout << "Script " << fileName << " completed\n";
		} else {
			std::cerr << "Failed to compile: " << fileName << ".\n";
		}
	} else {
		std::string message;
		message = "Failed to load file: ";
		message += fileName;
		_isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(_isolate, message.c_str())));
	}
}

void Task::invokeExport(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* sender = Task::get(args.GetIsolate());
	TaskTryCatch tryCatch(sender);
	v8::Handle<v8::Object> data = v8::Handle<v8::Object>::Cast(args.Data());
	exportid_t exportId = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "export"))->Int32Value();
	taskid_t recipientId = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "task"))->Int32Value();

	for (size_t i = 0; i < sender->_imports.size(); ++i) {
		if (sender->_imports[i]->_task == recipientId && sender->_imports[i]->_export == exportId) {
			sender->_imports[i]->ref();
			break;
		}
	}

	v8::Local<v8::Array> array = v8::Array::New(args.GetIsolate(), args.Length() + 1);
	array->Set(0, args.This());
	for (int i = 0; i < args.Length(); ++i) {
		array->Set(i + 1, args[i]);
	}

	if (TaskStub* recipient = sender->get(recipientId)) {
		promiseid_t promise = sender->allocatePromise();
		sendPromiseExportMessage(sender, recipient, kInvokeExport, promise, exportId, array);
		args.GetReturnValue().Set(sender->getPromise(promise));
	} else {
		args.GetReturnValue().Set(args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), "Invoking a function on a nonexistant task."))));
	}
}

v8::Handle<v8::Value> Task::invokeExport(TaskStub* from, Task* to, exportid_t exportId, const std::vector<char>& buffer) {
	v8::Handle<v8::Value> result;
	if (to->_exports[exportId]) {
		v8::Handle<v8::Array> arguments = v8::Handle<v8::Array>::Cast(Serialize::load(to, from, buffer));
		std::vector<v8::Handle<v8::Value> > argumentArray;
		for (size_t i = 1; i < arguments->Length(); ++i) {
			argumentArray.push_back(arguments->Get(i));
		}
		v8::Handle<v8::Function> function = v8::Local<v8::Function>::New(to->_isolate, to->_exports[exportId]->_persistent);
		v8::Handle<v8::Value>* argumentPointer = 0;
		if (argumentArray.size()) {
			argumentPointer = &*argumentArray.begin();
		}
		result = function->Call(v8::Handle<v8::Object>::Cast(arguments->Get(0)), argumentArray.size(), argumentPointer);
	} else {
		std::cout << to->_scriptName << ": That's not an export we have (exportId=" << exportId << ", exports = " << to->_exports.size() << ")\n";
	}
	from->getStream().send(kReleaseImport, reinterpret_cast<char*>(&exportId), sizeof(exportId));
	return result;
}

void Task::sendPromiseResolve(Task* from, TaskStub* to, promiseid_t promise, v8::Handle<v8::Value> result) {
	if (!result.IsEmpty() && result->IsPromise()) {
		// We're not going to serialize/deserialize a promise...
		v8::Handle<v8::Object> data = v8::Object::New(from->_isolate);
		data->Set(v8::String::NewFromUtf8(from->_isolate, "task"), v8::Int32::New(from->_isolate, to->getId()));
		data->Set(v8::String::NewFromUtf8(from->_isolate, "promise"), v8::Int32::New(from->_isolate, promise));
		v8::Handle<v8::Promise> promise = v8::Handle<v8::Promise>::Cast(result);
		v8::Handle<v8::Function> then = v8::Function::New(from->_isolate, invokeThen, data);
		promise->Then(then);
		v8::Handle<v8::Function> catchCallback = v8::Function::New(from->_isolate, invokeCatch, data);
		promise->Catch(catchCallback);
		from->_isolate->RunMicrotasks();
	} else {
		sendPromiseMessage(from, to, kResolvePromise, promise, result);
	}
}

void Task::sendPromiseReject(Task* from, TaskStub* to, promiseid_t promise, v8::Handle<v8::Value> result) {
	if (!result.IsEmpty() && result->IsPromise()) {
		// We're not going to serialize/deserialize a promise...
		v8::Handle<v8::Object> data = v8::Object::New(from->_isolate);
		data->Set(v8::String::NewFromUtf8(from->_isolate, "task"), v8::Int32::New(from->_isolate, to->getId()));
		data->Set(v8::String::NewFromUtf8(from->_isolate, "promise"), v8::Int32::New(from->_isolate, promise));
		v8::Handle<v8::Promise> promise = v8::Handle<v8::Promise>::Cast(result);
		v8::Handle<v8::Function> then = v8::Function::New(from->_isolate, invokeThen, data);
		promise->Then(then);
		v8::Handle<v8::Function> catchCallback = v8::Function::New(from->_isolate, invokeCatch, data);
		promise->Catch(catchCallback);
		from->_isolate->RunMicrotasks();
	} else {
		sendPromiseMessage(from, to, kRejectPromise, promise, result);
	}
}

void Task::sendPromiseMessage(Task* from, TaskStub* to, MessageType messageType, promiseid_t promise, v8::Handle<v8::Value> result) {
	if (to) {
		std::vector<char> buffer;
		buffer.insert(buffer.end(), reinterpret_cast<char*>(&promise), reinterpret_cast<char*>(&promise) + sizeof(promise));
		if (!result.IsEmpty() && !result->IsUndefined() && !result->IsNull()) {
			Serialize::store(from, buffer, result);
		}
		to->getStream().send(messageType, &*buffer.begin(), buffer.size());
	} else {
		std::cerr << "Sending to a NULL task.\n";
	}
}

void Task::sendPromiseExportMessage(Task* from, TaskStub* to, MessageType messageType, promiseid_t promise, exportid_t exportId, v8::Handle<v8::Value> result) {
	std::vector<char> buffer;
	buffer.insert(buffer.end(), reinterpret_cast<char*>(&promise), reinterpret_cast<char*>(&promise) + sizeof(promise));
	buffer.insert(buffer.end(), reinterpret_cast<char*>(&exportId), reinterpret_cast<char*>(&exportId) + sizeof(exportId));
	if (!result.IsEmpty() && !result->IsUndefined() && !result->IsNull()) {
		Serialize::store(from, buffer, result);
	}
	to->getStream().send(messageType, &*buffer.begin(), buffer.size());
}

TaskStub* Task::get(taskid_t taskId) {
	return taskId == kParentId ? _parent : _children[taskId];
}

void Task::invokeThen(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* from = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::Handle<v8::Object> data = v8::Handle<v8::Object>::Cast(args.Data());
	TaskStub* to = from->get(data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "task"))->Int32Value());
	promiseid_t promise = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "promise"))->Int32Value();
	sendPromiseMessage(from, to, kResolvePromise, promise, args[0]);
}

void Task::invokeCatch(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* from = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::Handle<v8::Object> data = v8::Handle<v8::Object>::Cast(args.Data());
	TaskStub* to = from->get(data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "task"))->Int32Value());
	promiseid_t promise = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "promise"))->Int32Value();
	sendPromiseMessage(from, to, kRejectPromise, promise, args[0]);
}

void Task::parent(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	if (task->_parent) {
		args.GetReturnValue().Set(task->_parent->getTaskObject());
	} else {
		args.GetReturnValue().Set(v8::Undefined(task->_isolate));
	}
}

void Task::version(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	args.GetReturnValue().Set(v8::String::NewFromUtf8(task->_isolate, v8::V8::GetVersion()));
}

void Task::getImportProperty(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	v8::Local<v8::Object> imports = Task::get(args.GetIsolate())->_importObject.Get(args.GetIsolate());
	args.GetReturnValue().Set(imports->Get(property));
}

void Task::getImports(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	args.GetReturnValue().Set(v8::Local<v8::Object>::New(args.GetIsolate(), Task::get(args.GetIsolate())->_importObject));
}

void Task::getExports(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	args.GetReturnValue().Set(v8::Local<v8::Object>::New(args.GetIsolate(), Task::get(args.GetIsolate())->_exportObject));
}

void Task::setExports(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& args) {
	Task::get(args.GetIsolate())->_exportObject = v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> >(args.GetIsolate(), v8::Handle<v8::Object>::Cast(value));
}

Task* Task::get(v8::Isolate* isolate) {
	return reinterpret_cast<Task*>(isolate->GetData(0));
}

promiseid_t Task::allocatePromise() {
	promiseid_t promiseId;
	do {
		promiseId = _nextPromise++;
	} while (_promises.find(promiseId) != _promises.end());
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

exportid_t Task::exportFunction(v8::Handle<v8::Function> function) {
	exportid_t exportId = -1;
	v8::Handle<v8::String> exportName = v8::String::NewFromUtf8(_isolate, "export");

	v8::Local<v8::Value> value = function->GetHiddenValue(exportName);
	if (!value.IsEmpty() && value->IsNumber())
	{
		exportid_t foundId = value->ToInteger(_isolate)->Int32Value();
		if (_exports[foundId]) {
			exportId = foundId;
		}
	}

	if (exportId == -1) {
		do {
			exportId = _nextExport++;
		} while (_exports[_nextExport]);
		ExportRecord* record = new ExportRecord(_isolate, function);
		function->SetHiddenValue(exportName, v8::Integer::New(_isolate, exportId));
		_exports[exportId] = record;
	}

	if (_exports[exportId]) {
		_exports[exportId]->ref();
	}

	return exportId;
}

void Task::releaseExport(taskid_t taskId, exportid_t exportId) {
	if (TaskStub* task = get(taskId)) {
		std::vector<char> buffer;
		buffer.insert(buffer.end(), reinterpret_cast<char*>(&exportId), reinterpret_cast<char*>(&exportId) + sizeof(exportId));
		task->getStream().send(kReleaseExport, &*buffer.begin(), buffer.size());
	}
}

v8::Handle<v8::Function> Task::addImport(taskid_t taskId, exportid_t exportId) {
	v8::Local<v8::Object> data = v8::Object::New(_isolate);
	data->Set(v8::String::NewFromUtf8(_isolate, "export"), v8::Int32::New(_isolate, exportId));
	data->Set(v8::String::NewFromUtf8(_isolate, "task"), v8::Int32::New(_isolate, taskId));
	v8::Local<v8::Function> function = v8::Function::New(_isolate, Task::invokeExport, data);
	_imports.push_back(new ImportRecord(_isolate, function, exportId, taskId, this));
	return function;
}

v8::Handle<v8::Object> Task::getStatistics() {
	v8::Handle<v8::Object> result = v8::Object::New(_isolate);
	result->Set(v8::String::NewFromUtf8(_isolate, "sockets"), v8::Integer::New(_isolate, Socket::getCount()));
	result->Set(v8::String::NewFromUtf8(_isolate, "openSockets"), v8::Integer::New(_isolate, Socket::getOpenCount()));
	result->Set(v8::String::NewFromUtf8(_isolate, "promises"), v8::Integer::New(_isolate, _promises.size()));
	result->Set(v8::String::NewFromUtf8(_isolate, "exports"), v8::Integer::New(_isolate, _exports.size()));
	result->Set(v8::String::NewFromUtf8(_isolate, "imports"), v8::Integer::New(_isolate, _imports.size()));

	uv_rusage_t usage;
	if (uv_getrusage(&usage) == 0) {
		result->Set(v8::String::NewFromUtf8(_isolate, "utime"), v8::Number::New(_isolate, usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0));
		result->Set(v8::String::NewFromUtf8(_isolate, "stime"), v8::Number::New(_isolate, usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0));
		result->Set(v8::String::NewFromUtf8(_isolate, "maxrss"), v8::Number::New(_isolate, usage.ru_maxrss));
	}

	return result;
}

void Task::onReceivePacket(int packetType, const char* begin, size_t length, void* userData) {
	TaskStub* stub = reinterpret_cast<TaskStub*>(userData);
	TaskStub* from = stub;
	Task* to = stub->getOwner();

	TaskTryCatch tryCatch(to);
	v8::HandleScope scope(to->_isolate);

	switch (static_cast<MessageType>(packetType)) {
	case kStatistics:
		{
			promiseid_t promise;
			std::memcpy(&promise, begin, sizeof(promise));
			v8::Handle<v8::Value> result = to->getStatistics();
			sendPromiseResolve(to, from, promise, result);
		}
		break;
	case kInvokeExport:
		{
			promiseid_t promise;
			exportid_t exportId;
			std::memcpy(&promise, begin, sizeof(promise));
			std::memcpy(&exportId, begin + sizeof(promise), sizeof(exportId));

			v8::TryCatch tryCatch;
			v8::Handle<v8::Value> result = invokeExport(from, to, exportId, std::vector<char>(begin + sizeof(promiseid_t) + sizeof(exportid_t), begin + length));
			if (tryCatch.HasCaught()) {
				sendPromiseReject(to, from, promise, Serialize::store(to, tryCatch));
			} else {
				sendPromiseResolve(to, from, promise, result);
			}
		}
		break;
	case kResolvePromise:
	case kRejectPromise:
		{
			v8::Handle<v8::Value> arg;
			promiseid_t promise;
			std::memcpy(&promise, begin, sizeof(promiseid_t));
			if (length > sizeof(promiseid_t)) {
				arg = Serialize::load(to, from, std::vector<char>(begin + sizeof(promiseid_t), begin + length));
			}
			else {
				arg = v8::Undefined(to->_isolate);
			}
			if (static_cast<MessageType>(packetType) == kResolvePromise) {
				to->resolvePromise(promise, arg);
			}
			else {
				to->rejectPromise(promise, arg);
			}
		}
		break;
	case kReleaseExport:
		assert(length == sizeof(exportid_t));
		exportid_t exportId;
		memcpy(&exportId, begin, sizeof(exportId));
		if (to->_exports[exportId]) {
			if (to->_exports[exportId]->release()) {
				to->_exports.erase(exportId);
			}
		}
		break;
	case kReleaseImport:
		{
			assert(length == sizeof(exportid_t));
			exportid_t exportId;
			memcpy(&exportId, begin, sizeof(exportId));
			for (size_t i = 0; i < to->_imports.size(); ++i) {
				if (to->_imports[i]->_task == from->getId() && to->_imports[i]->_export == exportId) {
					to->_imports[i]->release();
					break;
				}
			}
		}
		break;
	case kSetTrusted:
		{
			assert(length == sizeof(bool));
			bool trusted = false;
			memcpy(&trusted, begin, sizeof(bool));
			to->_trusted = trusted;
		}
		break;
	case kActivate:
		to->activate();
		break;
	case kExecute:
		{
			assert(length >= sizeof(promiseid_t));
			v8::Handle<v8::Value> arg;
			promiseid_t promise;
			std::memcpy(&promise, begin, sizeof(promiseid_t));
			arg = Serialize::load(to, from, std::vector<char>(begin + sizeof(promiseid_t), begin + length));
			v8::TryCatch tryCatch(to->_isolate);
			tryCatch.SetCaptureMessage(true);
			tryCatch.SetVerbose(true);
			to->execute(*v8::String::Utf8Value(arg));
			if (tryCatch.HasCaught()) {
				sendPromiseReject(to, from, promise, Serialize::store(to, tryCatch));
			}
			else {
				sendPromiseResolve(to, from, promise, v8::Undefined(to->_isolate));
			}
		}
		break;
	case kKill:
		::exit(1);
		break;
	case kSetImports:
		{
			v8::Handle<v8::Object> result = v8::Handle<v8::Object>::Cast(Serialize::load(to, from, std::vector<char>(begin, begin + length)));
			to->_importObject = v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> >(to->_isolate, result);
		}
		break;
	case kGetExports:
		promiseid_t promise;
		assert(length == sizeof(promise));
		std::memcpy(&promise, begin, sizeof(promiseid_t));
		v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(to->_isolate, to->_exportObject);
		sendPromiseResolve(to, from, promise, result);
		break;
	}
}

void Task::configureFromStdin() {
	_parent = TaskStub::createParent(this, STDIN_FILENO);
}

std::string Task::resolveRequire(const std::string& require) {
	std::string result;
	std::string path = _scriptName;
	size_t position = path.rfind('/');
	if (position != std::string::npos) {
		path.resize(position + 1);
		std::cout << "Looking in " << path << " for " << require << "\n";
		if (require.find("..") == std::string::npos && require.find('/') == std::string::npos) {
			result = path + require;
		}
		if (result.size() && require.rfind(".js") != require.size() - 3) {
			result += ".js";
		}
	}
	return result;
}

void Task::require(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	Task* task = Task::get(args.GetIsolate());
	v8::String::Utf8Value pathValue(args[0]);
	if (*pathValue) {
		std::string unresolved(*pathValue, *pathValue + pathValue.length());
		std::string path = task->resolveRequire(unresolved);
		if (!path.size()) {
			args.GetReturnValue().Set(args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), ("require(): Unable to resolve module: " + unresolved).c_str()))));
		} else {
			ScriptExportMap::iterator it = task->_scriptExports.find(path);
			if (it != task->_scriptExports.end()) {
				v8::Handle<v8::Object> exports = v8::Local<v8::Object>::New(args.GetIsolate(), it->second);
				args.GetReturnValue().Set(exports);
			} else {
				v8::Handle<v8::Object> exports = v8::Object::New(args.GetIsolate());
				task->_scriptExports[path] = v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> >(args.GetIsolate(), exports);

				v8::Handle<v8::String> name = v8::String::NewFromUtf8(args.GetIsolate(), path.c_str());
				v8::Handle<v8::String> source = loadFile(args.GetIsolate(), path.c_str());
				std::cout << "Requiring script " << path << "\n";
				if (!source.IsEmpty()) {
					v8::Handle<v8::Object> global = args.GetIsolate()->GetCurrentContext()->Global();
					v8::Handle<v8::Value> oldExports = global->Get(v8::String::NewFromUtf8(args.GetIsolate(), "exports"));
					global->Set(v8::String::NewFromUtf8(args.GetIsolate(), "exports"), exports);
					v8::Handle<v8::Script> script = v8::Script::Compile(source, name);
					if (!script.IsEmpty()) {
						script->Run();
						std::cout << "Script " << path << " completed\n";
					} else {
						std::cerr << "Failed to compile script.\n";
					}
					global->Set(v8::String::NewFromUtf8(args.GetIsolate(), "exports"), oldExports);
					args.GetReturnValue().Set(exports);
				} else {
					std::cerr << "Failed to load " << path << ".\n";
				}
			}
		}
	} else {
		args.GetReturnValue().Set(args.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(args.GetIsolate(), "require(): No module specified."))));
	}
}
