#include "TaskStub.h"

#include "PacketStream.h"
#include "Task.h"
#include "TaskTryCatch.h"

#include <cstring>
#include <unistd.h>

TaskStub::TaskStub(v8::Isolate* isolate, v8::Handle<v8::Object> taskObject)
:	_taskObject(isolate, taskObject) {
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

TaskStub* TaskStub::createParent(Task* task, uv_stream_t* handle) {
	v8::Isolate::Scope isolateScope(task->_isolate);
	v8::HandleScope scope(task->_isolate);

	v8::Local<v8::Context> context = v8::Context::New(task->_isolate, 0);
	context->Enter();

	v8::Handle<v8::ObjectTemplate> parentTemplate = v8::ObjectTemplate::New(task->_isolate);
	parentTemplate->Set(v8::String::NewFromUtf8(task->_isolate, "invoke"), v8::FunctionTemplate::New(task->_isolate, TaskStub::invoke));
	parentTemplate->SetInternalFieldCount(1);

	v8::Handle<v8::Object> parentObject = parentTemplate->NewInstance();
	TaskStub* parentStub = new TaskStub(task->_isolate, v8::Local<v8::Object>::New(task->_isolate, parentObject));
	parentObject->SetInternalField(0, v8::External::New(task->_isolate, parentStub));
	parentStub->_owner = task;
	parentStub->_id = Task::kParentId;

	parentStub->_stream.setOnReceive(Task::onReceivePacket, parentStub);
	parentStub->_stream.accept(handle);
	return parentStub;
}

void TaskStub::create(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* parent = Task::get(args.GetIsolate());
	v8::HandleScope scope(args.GetIsolate());

	v8::Handle<v8::ObjectTemplate> taskTemplate = v8::ObjectTemplate::New(args.GetIsolate());
	taskTemplate->SetAccessor(v8::String::NewFromUtf8(args.GetIsolate(), "trusted"), getTrusted, setTrusted);
	taskTemplate->SetAccessor(v8::String::NewFromUtf8(args.GetIsolate(), "onExit"), getOnExit, setOnExit);
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "execute"), v8::FunctionTemplate::New(args.GetIsolate(), TaskStub::execute));
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "kill"), v8::FunctionTemplate::New(args.GetIsolate(), TaskStub::kill));
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "invoke"), v8::FunctionTemplate::New(args.GetIsolate(), TaskStub::invoke));
	taskTemplate->Set(v8::String::NewFromUtf8(args.GetIsolate(), "statistics"), v8::FunctionTemplate::New(args.GetIsolate(), TaskStub::statistics));
	taskTemplate->SetInternalFieldCount(1);

	v8::Handle<v8::Object> taskObject = taskTemplate->NewInstance();
	TaskStub* stub = new TaskStub(args.GetIsolate(), taskObject);
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

	uv_os_sock_t sock[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock) != 0) {
		perror("socketpair");
	}

	char executable[1024];
	size_t size = sizeof(executable);
	uv_exepath(executable, &size);

	char arg1[] = "--child";
	char* argv[] = { executable, arg1, 0 };

	uv_pipe_t* pipe = new uv_pipe_t;
	if (uv_pipe_init(parent->getLoop(), pipe, 1) != 0) {
		std::cerr << "uv_pipe_init failed\n";
	}

	uv_stdio_container_t io[3];
	io[0].flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE);
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
	if (uv_spawn(parent->getLoop(), &stub->_process, &options) != 0) {
		std::cerr << "uv_spawn failed\n";
	}

	uv_tcp_t stream;
	if (uv_tcp_init(parent->getLoop(), &stream) != 0) {
		std::cerr << "uv_tcp_init failed\n";
	}
	if (uv_tcp_open(&stream, sock[0]) != 0) {
		std::cerr << "uv_tcp_open failed\n";
	}

	stub->_stream.setOnReceive(Task::onReceivePacket, stub);
	stub->_stream.createFrom(parent->getLoop(), sock[1]);

	uv_write_t* request = reinterpret_cast<uv_write_t*>(new char[sizeof(uv_write_t) + sizeof(int)]);
	uv_buf_t buf;
	buf.base = reinterpret_cast<char*>(request) + sizeof(uv_write_t);
	buf.len = sizeof(int);
	std::memset(buf.base, 0, sizeof(int));

	if (uv_write2(request, reinterpret_cast<uv_stream_t*>(pipe), &buf, 1, reinterpret_cast<uv_stream_t*>(&stream), onPipeWrite) != 0) {
		std::cerr << "uv_write2 failed\n";
	}

	args.GetReturnValue().Set(taskObject);
}

void TaskStub::onPipeWrite(uv_write_t* request, int status) {
	uv_close(reinterpret_cast<uv_handle_t*>(request->handle), onPipeClose);
	delete reinterpret_cast<char*>(request);
}

void TaskStub::onPipeClose(uv_handle_t* handle) {
	delete reinterpret_cast<uv_pipe_t*>(handle);
}

void TaskStub::onProcessExit(uv_process_t* process, int64_t status, int terminationSignal) {
	TaskStub* stub = reinterpret_cast<TaskStub*>(process->data);
	std::cout << "PROCESS " << stub->_id << " EXITED: " << status << " " << terminationSignal << "\n";
	if (!stub->_onExit.IsEmpty()) {
		TaskTryCatch tryCatch(stub->_owner);
		v8::HandleScope scope(stub->_owner->_isolate);
		v8::Handle<v8::Function> callback = v8::Local<v8::Function>::New(stub->_owner->_isolate, stub->_onExit);
		v8::Handle<v8::Value> args[2];
		args[0] = v8::Integer::New(stub->_owner->_isolate, status);
		args[1] = v8::Integer::New(stub->_owner->_isolate, terminationSignal);
		callback->Call(callback, 2, &args[0]);
	}
	uv_close(reinterpret_cast<uv_handle_t*>(process), 0);
}

void TaskStub::onRelease(const v8::WeakCallbackData<v8::Object, TaskStub>& data) {
}

void TaskStub::getTrusted(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	args.GetReturnValue().Set(v8::Boolean::New(args.GetIsolate(), false /*TaskStub::get(args.This())->_task->_trusted)*/));
}

void TaskStub::setTrusted(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& args) {
	if (TaskStub* stub = TaskStub::get(args.This())) {
		bool trusted = value->BooleanValue();
		stub->_stream.send(kSetTrusted, reinterpret_cast<char*>(&trusted), sizeof(trusted));
	}
}

void TaskStub::getOnExit(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
	TaskTryCatch tryCatch(TaskStub::get(args.This())->_owner);
	v8::HandleScope scope(args.GetIsolate());
	args.GetReturnValue().Set(v8::Local<v8::Function>::New(args.GetIsolate(), TaskStub::get(args.This())->_onExit));
}

void TaskStub::setOnExit(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& args) {
	TaskTryCatch tryCatch(TaskStub::get(args.This())->_owner);
	v8::HandleScope scope(args.GetIsolate());
	v8::Persistent<v8::Function, v8::CopyablePersistentTraits<v8::Function> > function(args.GetIsolate(), v8::Handle<v8::Function>::Cast(value));
	TaskStub::get(args.This())->_onExit = function;
}

TaskStub* TaskStub::get(v8::Handle<v8::Object> object) {
	return reinterpret_cast<TaskStub*>(v8::Handle<v8::External>::Cast(object->GetInternalField(0))->Value());
}

v8::Handle<v8::Object> TaskStub::getTaskObject() {
	return v8::Local<v8::Object>::New(_owner->getIsolate(), _taskObject);
}

void TaskStub::execute(const v8::FunctionCallbackInfo<v8::Value>& args) {
	TaskStub* stub = TaskStub::get(args.This());
	TaskTryCatch tryCatch(stub->_owner);
	v8::HandleScope scope(args.GetIsolate());
	v8::String::Utf8Value fileName(args[0]->ToString(args.GetIsolate()));
	stub->_stream.send(kExecute, *fileName, fileName.length());
}

void TaskStub::kill(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TaskStub* stub = TaskStub::get(args.This())) {
		stub->_stream.send(kKill, 0, 0);
	}
}

void TaskStub::invoke(const v8::FunctionCallbackInfo<v8::Value>& args) {
	TaskStub* stub = TaskStub::get(args.This());
	TaskTryCatch tryCatch(stub->_owner);
	v8::HandleScope scope(args.GetIsolate());

	promiseid_t promise = stub->_owner->allocatePromise();
	Task::sendPromiseMessage(stub->_owner, stub, kSendMessage, promise, args[0]);
	args.GetReturnValue().Set(stub->_owner->getPromise(promise));
}

void TaskStub::statistics(const v8::FunctionCallbackInfo<v8::Value>& args) {
	TaskStub* stub = TaskStub::get(args.This());
	TaskTryCatch tryCatch(stub->_owner);
	v8::HandleScope scope(args.GetIsolate());

	promiseid_t promise = stub->_owner->allocatePromise();
	Task::sendPromiseMessage(stub->_owner, stub, kStatistics, promise, v8::Undefined(args.GetIsolate()));
	args.GetReturnValue().Set(stub->_owner->getPromise(promise));
}
