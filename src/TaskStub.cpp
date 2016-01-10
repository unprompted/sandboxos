#include "TaskStub.h"

#include "PacketStream.h"
#include "Serialize.h"
#include "Task.h"
#include "TaskTryCatch.h"

#include <cstring>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <ws2tcpip.h>
static const int STDIN_FILENO = 0;
static const int STDOUT_FILENO = 1;
static const int STDERR_FILENO = 2;
#else
#include <unistd.h>
#endif

bool TaskStub::_determinedExecutable = false;
char TaskStub::_executable[1024];

void TaskStub::initialize() {
	if (!_determinedExecutable) {
		size_t size = sizeof(_executable);
		uv_exepath(_executable, &size);
		_determinedExecutable = true;
	}
}

TaskStub::TaskStub() {
	initialize();
	std::memset(&_process, 0, sizeof(_process));
}

void TaskStub::ref() {
	if (++_refCount == 1) {
		_taskObject.ClearWeak();
	}
}

void TaskStub::release() {
	if (--_refCount == 0) {
		_taskObject.SetWeak(this, onRelease);
	}
}

TaskStub* TaskStub::createParent(Task* task, uv_file file) {
	v8::Isolate::Scope isolateScope(task->_isolate);
	v8::HandleScope scope(task->_isolate);

	v8::Local<v8::Context> context = v8::Context::New(task->_isolate, 0);
	context->Enter();

	v8::Handle<v8::ObjectTemplate> parentTemplate = v8::ObjectTemplate::New(task->_isolate);
	parentTemplate->SetInternalFieldCount(1);

	v8::Handle<v8::Object> parentObject = parentTemplate->NewInstance();
	TaskStub* parentStub = new TaskStub();
	parentStub->_taskObject.Reset(task->_isolate, v8::Local<v8::Object>::New(task->_isolate, parentObject));
	parentObject->SetInternalField(0, v8::External::New(task->_isolate, parentStub));
	parentStub->_owner = task;
	parentStub->_id = Task::kParentId;

	if (uv_pipe_init(task->_loop, &parentStub->_stream.getStream(), 1) != 0) {
		std::cerr << "uv_pipe_init failed\n";
	}
	parentStub->_stream.setOnReceive(Task::onReceivePacket, parentStub);
	if (uv_pipe_open(&parentStub->_stream.getStream(), file) != 0) {
		std::cerr << "uv_pipe_open failed\n";
	}
	parentStub->_stream.start();

	return parentStub;
}

void TaskStub::create(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* parent = Task::get(args.GetIsolate());
	v8::HandleScope scope(args.GetIsolate());

	TaskStub* stub = new TaskStub();
	v8::Handle<v8::External> data = v8::External::New(args.GetIsolate(), stub);

	v8::Handle<v8::ObjectTemplate> taskTemplate = v8::ObjectTemplate::New(args.GetIsolate());
	taskTemplate->SetAccessor(v8::String::NewFromUtf8(args.GetIsolate(), "trusted"), getTrusted, setTrusted, data);
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "setImports"), v8::FunctionTemplate::New(args.GetIsolate(), setImports, data));
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "getExports"), v8::FunctionTemplate::New(args.GetIsolate(), getExports, data));
	taskTemplate->SetAccessor(v8::String::NewFromUtf8(args.GetIsolate(), "onExit"), getOnExit, setOnExit, data);
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "activate"), v8::FunctionTemplate::New(args.GetIsolate(), TaskStub::activate, data));
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "execute"), v8::FunctionTemplate::New(args.GetIsolate(), TaskStub::execute, data));
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "kill"), v8::FunctionTemplate::New(args.GetIsolate(), TaskStub::kill, data));
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "statistics"), v8::FunctionTemplate::New(args.GetIsolate(), TaskStub::statistics, data));
	taskTemplate->SetInternalFieldCount(1);

	v8::Handle<v8::Object> taskObject = taskTemplate->NewInstance();
	stub->_taskObject.Reset(args.GetIsolate(), taskObject);
	taskObject->SetInternalField(0, v8::External::New(args.GetIsolate(), stub));
	stub->_owner = parent;

	taskid_t id = 0;
	if (parent) {
		do {
			id = parent->_nextTask++;
			if (parent->_nextTask == Task::kParentId) {
				++parent->_nextTask;
			}
		} while (parent->_children.find(id) != parent->_children.end());
		parent->_children[id] = stub;
	}
	stub->_id = id;

	char arg1[] = "--child";
	char* argv[] = { _executable, arg1, 0 };

	uv_pipe_t* pipe = reinterpret_cast<uv_pipe_t*>(&stub->_stream.getStream());
	std::memset(pipe, 0, sizeof(*pipe));
	if (uv_pipe_init(parent->getLoop(), pipe, 1) != 0) {
		std::cerr << "uv_pipe_init failed\n";
	}

	uv_stdio_container_t io[3];
	io[0].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
	io[0].data.stream = reinterpret_cast<uv_stream_t*>(pipe);
	io[1].flags = UV_INHERIT_FD;
	io[1].data.fd = STDOUT_FILENO;
	io[2].flags = UV_INHERIT_FD;
	io[2].data.fd = STDERR_FILENO;

	uv_process_options_t options = {0};
	options.args = argv;
	options.exit_cb = onProcessExit;
	options.stdio = io;
	options.stdio_count = sizeof(io) / sizeof(*io);
	options.file = argv[0];

	stub->_process.data = stub;
	int result = uv_spawn(parent->getLoop(), &stub->_process, &options);
	if (result == 0) {
		stub->_stream.setOnReceive(Task::onReceivePacket, stub);
		stub->_stream.start();

		args.GetReturnValue().Set(taskObject);
	} else {
		std::cerr << "uv_spawn failed: " << uv_strerror(result) << "\n";
	}
}

void TaskStub::onProcessExit(uv_process_t* process, int64_t status, int terminationSignal) {
	TaskStub* stub = reinterpret_cast<TaskStub*>(process->data);
	if (!stub->_onExit.IsEmpty()) {
		TaskTryCatch tryCatch(stub->_owner);
		v8::HandleScope scope(stub->_owner->_isolate);
		v8::Handle<v8::Function> callback = v8::Local<v8::Function>::New(stub->_owner->_isolate, stub->_onExit);
		v8::Handle<v8::Value> args[2];
		args[0] = v8::Integer::New(stub->_owner->_isolate, status);
		args[1] = v8::Integer::New(stub->_owner->_isolate, terminationSignal);
		callback->Call(callback, 2, &args[0]);
	}
	stub->_stream.close();
	stub->_owner->_children.erase(stub->_id);
	uv_close(reinterpret_cast<uv_handle_t*>(process), 0);
}

void TaskStub::onRelease(const v8::WeakCallbackData<v8::Object, TaskStub>& data) {
}

void TaskStub::getTrusted(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	args.GetReturnValue().Set(v8::Boolean::New(args.GetIsolate(), false));
}

void TaskStub::setTrusted(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& args) {
	if (TaskStub* stub = TaskStub::get(args.Data())) {
		bool trusted = value->BooleanValue();
		stub->_stream.send(kSetTrusted, reinterpret_cast<char*>(&trusted), sizeof(trusted));
	}
}

void TaskStub::getExports(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TaskStub* stub = TaskStub::get(args.Data())) {
		TaskTryCatch tryCatch(stub->_owner);
		v8::HandleScope scope(args.GetIsolate());

		promiseid_t promise = stub->_owner->allocatePromise();
		Task::sendPromiseMessage(stub->_owner, stub, kGetExports, promise, v8::Undefined(args.GetIsolate()));
		args.GetReturnValue().Set(stub->_owner->getPromise(promise));
	}
}

void TaskStub::setImports(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TaskStub* stub = TaskStub::get(args.Data())) {
		std::vector<char> buffer;
		Serialize::store(Task::get(args.GetIsolate()), buffer, args[0]);
		stub->_stream.send(kSetImports, &*buffer.begin(), buffer.size());
	}
}

void TaskStub::getOnExit(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	TaskTryCatch tryCatch(TaskStub::get(args.Data())->_owner);
	v8::HandleScope scope(args.GetIsolate());
	args.GetReturnValue().Set(v8::Local<v8::Function>::New(args.GetIsolate(), TaskStub::get(args.Data())->_onExit));
}

void TaskStub::setOnExit(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& args) {
	TaskTryCatch tryCatch(TaskStub::get(args.Data())->_owner);
	v8::HandleScope scope(args.GetIsolate());
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > function(args.GetIsolate(), v8::Handle<v8::Function>::Cast(value));
	TaskStub::get(args.Data())->_onExit = function;
}

TaskStub* TaskStub::get(v8::Handle<v8::Value> object) {
	return reinterpret_cast<TaskStub*>(v8::Handle<v8::External>::Cast(object)->Value());
}

v8::Handle<v8::Object> TaskStub::getTaskObject() {
	return v8::Local<v8::Object>::New(_owner->getIsolate(), _taskObject);
}

void TaskStub::activate(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TaskStub* stub = TaskStub::get(args.Data())) {
		TaskTryCatch tryCatch(stub->_owner);
		v8::HandleScope scope(args.GetIsolate());
		v8::String::Utf8Value fileName(args[0]->ToString(args.GetIsolate()));
		stub->_stream.send(kActivate, 0, 0);
	}
}

void TaskStub::execute(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TaskStub* stub = TaskStub::get(args.Data())) {
		TaskTryCatch tryCatch(stub->_owner);
		v8::HandleScope scope(args.GetIsolate());

		promiseid_t promise = stub->_owner->allocatePromise();
		Task::sendPromiseMessage(stub->_owner, stub, kExecute, promise, args[0]);
		args.GetReturnValue().Set(stub->_owner->getPromise(promise));
	}
}

void TaskStub::kill(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TaskStub* stub = TaskStub::get(args.Data())) {
		uv_process_kill(&stub->_process, SIGTERM);
	}
}

void TaskStub::statistics(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TaskStub* stub = TaskStub::get(args.Data())) {
		TaskTryCatch tryCatch(stub->_owner);
		v8::HandleScope scope(args.GetIsolate());

		promiseid_t promise = stub->_owner->allocatePromise();
		Task::sendPromiseMessage(stub->_owner, stub, kStatistics, promise, v8::Undefined(args.GetIsolate()));
		args.GetReturnValue().Set(stub->_owner->getPromise(promise));
	}
}
