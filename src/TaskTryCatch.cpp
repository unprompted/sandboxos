#include "TaskTryCatch.h"

#include "Task.h"

#include <iostream>

const char* TaskTryCatch::toString(const v8::String::Utf8Value& value) {
	return *value ? *value : "(null)";
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
