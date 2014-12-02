#ifndef INCLUDED_TaskTryCatch
#define INCLUDED_TaskTryCatch

#include <v8.h>

class Task;

class TaskTryCatch {
public:
	TaskTryCatch(Task* task);
	~TaskTryCatch();

private:
	v8::TryCatch _tryCatch;
	Task* _task;
	static const char* toString(const v8::String::Utf8Value& value);
};

#endif
