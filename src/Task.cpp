#include "Task.h"

#include "File.h"
#include "Serialize.h"
#include "Socket.h"
#include "TaskStub.h"
#include "TaskTryCatch.h"

#include <algorithm>
#include <assert.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <libplatform/libplatform.h>
#include <map>
#include <sys/types.h>
#include <unistd.h>
#include <uv.h>
#include <v8.h>
#include <v8-platform.h>

extern v8::Platform* gPlatform;
int gNextTaskId = 1;

v8::Handle<v8::String> loadFile(v8::Isolate* isolate, const char* fileName);

int Task::_count;

struct ExportRecord {
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > _persistent;

	ExportRecord(v8::Isolate* isolate, v8::Handle<v8::Function> function)
	:	_persistent(isolate, function) {
	}

	static void onRelease(const v8::WeakCallbackData<v8::Function, ExportRecord>& data) {
		data.GetParameter()->_persistent.Reset();
		delete data.GetParameter();
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
	_isolate = v8::Isolate::New();
	_isolate->SetData(0, this);
}

Task::~Task() {
	_isolate->Dispose();
	_isolate = 0;

	uv_loop_delete(_loop);
	--_count;
}

v8::Handle<v8::ObjectTemplate> Task::createGlobal() {
	v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
	global->Set(v8::String::NewFromUtf8(_isolate, "print"), v8::FunctionTemplate::New(_isolate, print));
	global->SetAccessor(v8::String::NewFromUtf8(_isolate, "parent"), parent);
	global->Set(v8::String::NewFromUtf8(_isolate, "exit"), v8::FunctionTemplate::New(_isolate, exit));

	if (_trusted) {
		global->Set(v8::String::NewFromUtf8(_isolate, "Socket"), v8::FunctionTemplate::New(_isolate, Socket::create));
		global->Set(v8::String::NewFromUtf8(_isolate, "Task"), v8::FunctionTemplate::New(_isolate, TaskStub::create));
		File::configure(_isolate, global);
	}
	return global;
}

void Task::run() {
	{
		v8::Isolate::Scope isolateScope(_isolate);
		v8::HandleScope handleScope(_isolate);
		std::cout << "Starting running: " << _scriptName << "\n";
		uv_run(_loop, UV_RUN_DEFAULT);
		std::cout << "Done running: " << _scriptName << "\n";
	}
	_promises.clear();
	_exports.clear();
	_imports.clear();
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
	std::cout << "Task[" << task << ':' << task->_scriptName << "]>";
	for (int i = 0; i < args.Length(); i++) {
		std::cout << ' ';
		v8::Handle<v8::Value> arg = args[i];
		v8::String::Utf8Value value(stringify->Call(json, 1, &arg));
		std::cout << *value ? *value : "(null)";
	}
	std::cout << '\n';
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
	v8::Local<v8::Context> context = v8::Context::New(_isolate, 0, createGlobal());
	context->Enter();
	v8::Handle<v8::String> script = loadFile(_isolate, fileName);
	std::cout << "Running script " << fileName << "\n";
	_scriptName = fileName;
	if (!script.IsEmpty()) {
		execute(script, v8::String::NewFromUtf8(_isolate, fileName));
	}
}

void Task::execute(v8::Handle<v8::String> source, v8::Handle<v8::String> name) {
	TaskTryCatch tryCatch(this);
	v8::Handle<v8::Script> script = v8::Script::Compile(source, name);
	if (!script.IsEmpty()) {
		v8::Handle<v8::Value> result = script->Run();
		v8::String::Utf8Value stringResult(result);
		std::cout << "Script returned: " << *stringResult << '\n';
	} else {
		std::cerr << "Failed to compile script.\n";
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

	v8::Local<v8::Array> array = v8::Array::New(args.GetIsolate(), args.Length());
	for (int i = 0; i < args.Length(); ++i) {
		array->Set(i, args[i]);
	}

	TaskStub* recipient = sender->get(recipientId);
	promiseid_t promise = sender->allocatePromise();
	sendPromiseExportMessage(sender, recipient, kInvokeExport, promise, exportId, array);
	args.GetReturnValue().Set(sender->getPromise(promise));
}

v8::Handle<v8::Value> Task::invokeOnMessage(TaskStub* from, Task* to, const std::vector<char>& buffer) {
	TaskTryCatch tryCatch(to);
	v8::Local<v8::Context> context = to->_isolate->GetCurrentContext();
	v8::Handle<v8::Value> args[2];
	args[0] = from->getTaskObject();
	args[1] = Serialize::load(to, from, buffer);
	v8::Handle<v8::Function> function = v8::Handle<v8::Function>::Cast(context->Global()->Get(v8::String::NewFromUtf8(to->_isolate, "onMessage")));
	v8::Handle<v8::Value> result = function->Call(context->Global(), 2, &args[0]);
	return result;
}

v8::Handle<v8::Value> Task::invokeExport(TaskStub* from, Task* to, exportid_t exportId, const std::vector<char>& buffer) {
	v8::Handle<v8::Value> result;
	v8::Handle<v8::Array> arguments = v8::Handle<v8::Array>::Cast(Serialize::load(to, from, buffer));
	std::vector<v8::Handle<v8::Value> > array;
	for (size_t i = 0; i < arguments->Length(); ++i) {
		array.push_back(arguments->Get(i));
	}
	v8::Handle<v8::Function> function = v8::Local<v8::Function>::New(to->_isolate, to->_exports[exportId]->_persistent);
	if (function.IsEmpty()) {
		std::cout << "I COULD NOT FIND THE FUNCTION " << exportId << " ON " << to->_scriptName << " (" << to->_exports.size() << ") " << to->_exports[exportId]->_persistent.IsEmpty() << "\n";
		result = v8::Undefined(to->_isolate);
	} else {
		result = function->Call(function, array.size(), &*array.begin());
	}

	/* XXX for (size_t i = 0; i < from->_imports.size(); ++i) {
		if (from->_imports[i]->_task == to->_id && from->_imports[i]->_export == exportId) {
			from->_imports[i]->release();
			break;
		}
	}*/
	return result;
}

void Task::sendInvokeResult(Task* from, TaskStub* to, promiseid_t promise, v8::Handle<v8::Value> result) {
	if (!result.IsEmpty() && result->IsPromise()) {
		// We're not going to serialize/deserialize a promise...
		v8::Handle<v8::Object> data = v8::Object::New(from->_isolate);
		data->Set(v8::String::NewFromUtf8(from->_isolate, "task"), v8::Int32::New(from->_isolate, to->getId()));
		data->Set(v8::String::NewFromUtf8(from->_isolate, "promise"), v8::Int32::New(from->_isolate, promise));
		v8::Handle<v8::Function> then = v8::Function::New(from->_isolate, invokeThen, data);
		v8::Handle<v8::Promise> promise = v8::Handle<v8::Promise>::Cast(result);
		promise->Then(then);
	} else {
		sendPromiseMessage(from, to, kResolvePromise, promise, result);
	}
}

PacketStream& Task::getPacketStream(Task* from, TaskStub* to) {
	return to->getStream();
}

void Task::sendPromiseMessage(Task* from, TaskStub* to, MessageType messageType, promiseid_t promise, v8::Handle<v8::Value> result) {
	std::vector<char> buffer;
	buffer.insert(buffer.end(), reinterpret_cast<char*>(&promise), reinterpret_cast<char*>(&promise) + sizeof(promise));
	if (!result.IsEmpty() && !result->IsUndefined() && !result->IsNull()) {
		Serialize::store(from, buffer, result);
	}
	getPacketStream(from, to).send(messageType, &*buffer.begin(), buffer.size());
}

void Task::sendPromiseExportMessage(Task* from, TaskStub* to, MessageType messageType, promiseid_t promise, exportid_t exportId, v8::Handle<v8::Value> result) {
	std::vector<char> buffer;
	buffer.insert(buffer.end(), reinterpret_cast<char*>(&promise), reinterpret_cast<char*>(&promise) + sizeof(promise));
	buffer.insert(buffer.end(), reinterpret_cast<char*>(&exportId), reinterpret_cast<char*>(&exportId) + sizeof(exportId));
	if (!result.IsEmpty() && !result->IsUndefined() && !result->IsNull()) {
		Serialize::store(from, buffer, result);
	}
	getPacketStream(from, to).send(messageType, &*buffer.begin(), buffer.size());
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

void Task::parent(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	if (task->_parent) {
		args.GetReturnValue().Set(task->_parent->getTaskObject());
	} else {
		args.GetReturnValue().Set(v8::Undefined(task->_isolate));
	}
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

exportid_t Task::exportFunction(v8::Handle<v8::Function> function) {
	bool found = false;
	exportid_t exportId = -1;
	typedef std::map<exportid_t, ExportRecord*> ExportMap;
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

void Task::releaseExport(taskid_t taskId, exportid_t exportId) {
	if (TaskStub* task = get(taskId)) {
		std::vector<char> buffer;
		buffer.insert(buffer.end(), reinterpret_cast<char*>(&exportId), reinterpret_cast<char*>(&exportId) + sizeof(exportId));
		getPacketStream(this, task).send(kReleaseExport, &*buffer.begin(), buffer.size());
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

void Task::onReceivePacket(int packetType, const char* begin, size_t length, void* userData) {
	TaskStub* stub = reinterpret_cast<TaskStub*>(userData);
	TaskStub* from = stub;
	Task* to = stub->getOwner();

	TaskTryCatch tryCatch(to);
	v8::HandleScope scope(to->_isolate);

	switch (static_cast<MessageType>(packetType)) {
	case kSendMessage: {
		promiseid_t promise;
		std::memcpy(&promise, begin, sizeof(promise));
		v8::Handle<v8::Value> result = invokeOnMessage(from, to, std::vector<char>(begin + sizeof(promiseid_t), begin + length));
		sendInvokeResult(to, from, promise, result);
		}
		break;
	case kInvokeExport: {
		promiseid_t promise;
		exportid_t exportId;
		std::memcpy(&promise, begin, sizeof(promise));
		std::memcpy(&exportId, begin + sizeof(promise), sizeof(exportId));
		v8::Handle<v8::Value> result = invokeExport(from, to, exportId, std::vector<char>(begin + sizeof(promiseid_t) + sizeof(exportid_t), begin + length));
		sendInvokeResult(to, from, promise, result);
		}
		break;
	case kResolvePromise: {
		v8::Handle<v8::Value> arg;
		promiseid_t promise;
		std::memcpy(&promise, begin, sizeof(promiseid_t));
		if (length > sizeof(promiseid_t)) {
			arg = Serialize::load(to, from, std::vector<char>(begin + sizeof(promiseid_t), begin + length));
		} else {
			arg = v8::Undefined(to->_isolate);
		}
		to->resolvePromise(promise, arg);
		}
		break;
	case kReleaseExport:
		assert(length == sizeof(exportid_t));
		exportid_t exportId;
		memcpy(&exportId, begin, sizeof(exportId));
		to->_exports[exportId]->_persistent.Reset();
		delete to->_exports[exportId];
		to->_exports.erase(exportId);
		break;
	case kSetTrusted: {
		assert(length == sizeof(bool));
		bool trusted = false;
		memcpy(&trusted, begin, sizeof(bool));
		to->_trusted = trusted;
		}
		break;
	case kExecute:
		to->execute(std::string(begin, begin + length).c_str());
		break;
	case kKill:
		to->kill();
		break;
	}
}

void Task::configureFromStdin() {
	uv_pipe_t* pipe = new uv_pipe_t;
	pipe->data = this;
	if (uv_pipe_init(_loop, pipe, 1) != 0) {
		std::cerr << "uv_pipe_init failed\n";
	}
	if (uv_pipe_open(pipe, STDIN_FILENO) != 0) {
		std::cerr << "uv_pipe_open failed\n";
	}
	if (uv_read_start(reinterpret_cast<uv_stream_t*>(pipe), onPipeAllocate, onPipeRead) != 0) {
		std::cerr << "uv_read_start failed\n";
	}
}

void Task::onPipeAllocate(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer) {
	buffer->base = new char[suggestedSize];
	buffer->len = suggestedSize;
}

void Task::onPipeRead(uv_stream_t* handle, ssize_t count, const uv_buf_t* buffer) {
	Task* task = reinterpret_cast<Task*>(handle->data);
	if (uv_pipe_pending_count(reinterpret_cast<uv_pipe_t*>(handle)) != 0) {
		task->_parent = TaskStub::createParent(task, handle);

		uv_read_stop(handle);
		uv_close(reinterpret_cast<uv_handle_t*>(handle), 0);
		delete reinterpret_cast<uv_pipe_t*>(handle);
	}
	delete buffer->base;
}
