#include <v8.h>
#include <v8-platform.h>
#include <libplatform/libplatform.h>
#include <unistd.h>

#include "Task.h"

v8::Platform* gPlatform = 0;

int main(int argc, char* argv[]) {
	v8::V8::InitializeICU();
	gPlatform = v8::platform::CreateDefaultPlatform();
	v8::V8::InitializePlatform(gPlatform);
	v8::V8::Initialize();
	v8::V8::SetFlagsFromCommandLine(&argc, argv, true);

	int result = 0;
	gPlatform->CallOnBackgroundThread(new Task("test.js"), v8::Platform::kLongRunningTask);

	while (Task::getCount()) {
		usleep(10000);
	}

	v8::V8::Dispose();

	return result;
}
