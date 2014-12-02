#include "Task.h"

#include "File.h"
#include "Serialize.h"
#include "Socket.h"
#include "TaskTryCatch.h"

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

extern v8::Platform* gPlatform;
std::map<taskid_t, Task*> gTasks;
int gNextTaskId = 1;

v8::Handle<v8::String> loadFile(v8::Isolate* isolate, const char* fileName);

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

Task::Task(const char* scriptName)
:	_trusted(false),
	_killed(false),
	_parent(0),
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

	++_count;
	if (scriptName) {
		_scriptName = scriptName;
	}
}

Task::~Task() {
	Lock lock(_mutex);
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
		global->SetAccessor(v8::String::NewFromUtf8(_isolate, "parent"), parent);

		if (_trusted) {
			global->Set(v8::String::NewFromUtf8(_isolate, "startScript"), v8::FunctionTemplate::New(_isolate, startScript));
			global->Set(v8::String::NewFromUtf8(_isolate, "exit"), v8::FunctionTemplate::New(_isolate, exit));

			global->Set(v8::String::NewFromUtf8(_isolate, "Socket"), v8::FunctionTemplate::New(_isolate, Socket::create));
			File::configure(_isolate, global);
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
		std::cout << *value ? *value : "(null)";
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
	task->_parent = parent->_id;
	task->_scriptName = *v8::String::Utf8Value(args[0]);
	task->_trusted = parent->_trusted && !args[1].IsEmpty() && args[1]->BooleanValue();
	uv_thread_create(&task->_thread, run, task);

	uv_os_sock_t sock[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock) != 0) {
		perror("socketpair");
	}
	task->_parentStreamData[0] = task;
	task->_parentStreamData[1] = parent;
	task->_parentStream.setOnReceive(onReceivePacket, task->_parentStreamData);
	task->_parentStream.createFrom(parent->_loop, sock[0]);
	task->_childStreamData[0] = parent;
	task->_childStreamData[1] = task;
	task->_childStream.setOnReceive(onReceivePacket, task->_childStreamData);
	task->_childStream.createFrom(task->_loop, sock[1]);

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

	if (Task* task = Task::get(taskId)) {
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
	if (!_killed && _isolate) {
		v8::V8::TerminateExecution(_isolate);
		_killed = true;
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

	if (Task* task = Task::get(data->_task)) {
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
		std::cout << "Script returned: " << *stringResult << '\n';
	} else {
		std::cerr << "Failed to compile script.\n";
	}
}

void Task::invoke(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	TaskTryCatch tryCatch(task);
	v8::HandleScope scope(args.GetIsolate());

	promiseid_t promise = task->allocatePromise();
	taskid_t recipientId = args.This().As<v8::Object>()->GetInternalField(0).As<v8::Integer>()->Value();

	if (Task* recipient = Task::get(recipientId)) {
		sendPromiseMessage(task, recipient, kSendMessage, promise, args[0]);
		args.GetReturnValue().Set(task->getPromise(promise));
	}
}

struct InvokeRecord {
	v8::Persistent<v8::Function> _persistent;
};

void Task::invokeExport(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* sender = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
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

	Task* recipient = Task::get(recipientId);
	promiseid_t promise = sender->allocatePromise();
	sendPromiseExportMessage(sender, recipient, kInvokeExport, promise, exportId, array);
	args.GetReturnValue().Set(sender->getPromise(promise));
}

v8::Handle<v8::Value> Task::invokeOnMessage(Task* from, Task* to, const std::vector<char>& buffer) {
	v8::Local<v8::Context> context = to->_isolate->GetCurrentContext();
	v8::Handle<v8::Value> args[2];
	args[0] = to->makeTaskObject(from->_id);
	args[1] = Serialize::load(to, from, buffer);
	v8::Handle<v8::Function> function = v8::Handle<v8::Function>::Cast(context->Global()->Get(v8::String::NewFromUtf8(to->_isolate, "onMessage")));
	return function->Call(context->Global(), 2, &args[0]);
}

v8::Handle<v8::Value> Task::invokeExport(Task* from, Task* to, exportid_t exportId, const std::vector<char>& buffer) {
	v8::Handle<v8::Value> result;
	v8::Handle<v8::Array> arguments = v8::Handle<v8::Array>::Cast(Serialize::load(to, from, buffer));
	std::vector<v8::Handle<v8::Value> > array;
	for (size_t i = 0; i < arguments->Length(); ++i) {
		array.push_back(arguments->Get(i));
	}
	v8::Handle<v8::Function> function = v8::Local<v8::Function>::New(to->_isolate, to->_exports[exportId]->_persistent);
	if (function.IsEmpty()) {
		std::cout << "I COULD NOT FIND THE FUNCTION " << exportId << " ON " << to->_id << " (" << to->_exports.size() << ") " << to->_exports[exportId]->_persistent.IsEmpty() << "\n";
		result = v8::Undefined(to->_isolate);
	} else {
		result = function->Call(function, array.size(), &*array.begin());
	}

	for (size_t i = 0; i < from->_imports.size(); ++i) {
		if (from->_imports[i]->_task == to->_id && from->_imports[i]->_export == exportId) {
			from->_imports[i]->release();
			break;
		}
	}
	return result;
}

void Task::sendInvokeResult(Task* from, Task* to, promiseid_t promise, v8::Handle<v8::Value> result) {
	if (!result.IsEmpty() && result->IsPromise()) {
		// We're not going to serialize/deserialize a promise...
		v8::Handle<v8::Object> data = v8::Object::New(from->_isolate);
		data->Set(v8::String::NewFromUtf8(from->_isolate, "task"), v8::Int32::New(from->_isolate, to->_id));
		data->Set(v8::String::NewFromUtf8(from->_isolate, "promise"), v8::Int32::New(from->_isolate, promise));
		v8::Handle<v8::Function> then = v8::Function::New(from->_isolate, invokeThen, data);
		v8::Handle<v8::Promise> promise = v8::Handle<v8::Promise>::Cast(result);
		promise->Then(then);
	} else {
		sendPromiseMessage(from, to, kResolvePromise, promise, result);
	}
}

PacketStream& Task::getPacketStream(Task* from, Task* to) {
	return to->_parent ? to->_parentStream : from->_childStream;
}

void Task::sendPromiseMessage(Task* from, Task* to, MessageType messageType, promiseid_t promise, v8::Handle<v8::Value> result) {
	std::vector<char> buffer;
	buffer.insert(buffer.end(), reinterpret_cast<char*>(&promise), reinterpret_cast<char*>(&promise) + sizeof(promise));
	if (!result.IsEmpty() && !result->IsUndefined() && !result->IsNull()) {
		Serialize::store(from, buffer, result);
	}
	getPacketStream(from, to).send(messageType, &*buffer.begin(), buffer.size());
}

void Task::sendPromiseExportMessage(Task* from, Task* to, MessageType messageType, promiseid_t promise, exportid_t exportId, v8::Handle<v8::Value> result) {
	std::vector<char> buffer;
	buffer.insert(buffer.end(), reinterpret_cast<char*>(&promise), reinterpret_cast<char*>(&promise) + sizeof(promise));
	buffer.insert(buffer.end(), reinterpret_cast<char*>(&exportId), reinterpret_cast<char*>(&exportId) + sizeof(exportId));
	if (!result.IsEmpty() && !result->IsUndefined() && !result->IsNull()) {
		Serialize::store(from, buffer, result);
	}
	getPacketStream(from, to).send(messageType, &*buffer.begin(), buffer.size());
}

void Task::invokeThen(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* from = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::Handle<v8::Object> data = v8::Handle<v8::Object>::Cast(args.Data());
	Task* to = Task::get(data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "task"))->Int32Value());
	promiseid_t promise = data->Get(v8::String::NewFromUtf8(args.GetIsolate(), "promise"))->Int32Value();
	sendPromiseMessage(from, to, kResolvePromise, promise, args[0]);
}

void Task::parent(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	args.GetReturnValue().Set(task->makeTaskObject(task->_parent));
}

v8::Handle<v8::Object> Task::makeTaskObject(taskid_t id) {
	v8::Handle<v8::Object> taskObject;
	if (Task::get(id)) {
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
	if (Task* task = Task::get(taskId)) {
		std::vector<char> buffer;
		buffer.insert(buffer.end(), reinterpret_cast<char*>(&exportId), reinterpret_cast<char*>(&exportId) + sizeof(exportId));
		getPacketStream(this, task).send(kReleaseExport, &*buffer.begin(), buffer.size());
	}
}

void Task::addImport(v8::Handle<v8::Function> function, exportid_t exportId, taskid_t taskId) {
	_imports.push_back(new ImportRecord(_isolate, function, exportId, taskId, this));
}

void Task::onReceivePacket(int packetType, const char* begin, size_t length, void* userData) {
	Task** tasks = reinterpret_cast<Task**>(userData);
	Task* from = tasks[0];
	Task* to = tasks[1];

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
		promiseid_t promise = -1;
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
	}
}
