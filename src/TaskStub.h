#ifndef INCLUDED_TaskStub
#define INCLUDED_TaskStub

#include "PacketStream.h"

#include <v8.h>

class Task;

typedef int taskid_t;

class TaskStub {
public:
	void ref();
	void release();

	static void create(const v8::FunctionCallbackInfo<v8::Value>& args);

	taskid_t getId() { return _id; }
	Task* getTask() { return _task; }
	Task* getOwner() { return _owner; }
	v8::Handle<v8::Object> getTaskObject();
	PacketStream& getStream() { return _stream; }

private:
	v8::Persistent<v8::Object> _taskObject;
	int _refCount = 1;

	Task* _owner = 0;
	PacketStream _stream;
	Task* _task = 0;
	taskid_t _id = -1;
	uv_process_t _process = {0};

	TaskStub(v8::Isolate* isolate, v8::Handle<v8::Object> taskObject);

	static TaskStub* get(v8::Handle<v8::Object> object);

	static void getTrusted(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args);
	static void setTrusted(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& args);

	static void start(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void execute(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void kill(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void invoke(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void onRelease(const v8::WeakCallbackData<v8::Object, TaskStub>& data);
	static void onReceivePacket(int packetType, const char* begin, size_t length, void* userData);

	static void onProcessExit(uv_process_t* process, int64_t status, int terminationSignal);
	static void onPipeWrite(uv_write_t* request, int status);
};

#endif