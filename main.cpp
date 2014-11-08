#include <v8.h>
#include <v8-platform.h>
#include <libplatform/libplatform.h>
#include <unistd.h>
#include <uv.h>

#include "Task.h"

v8::Platform* gPlatform = 0;

int main(int argc, char* argv[]) {
	v8::V8::InitializeICU();
	gPlatform = v8::platform::CreateDefaultPlatform();
	v8::V8::InitializePlatform(gPlatform);
	v8::V8::Initialize();
	v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
	uv_loop_t* loop = uv_default_loop();

	gPlatform->CallOnBackgroundThread(new Task("test.js"), v8::Platform::kLongRunningTask);
	int result = uv_run(loop, UV_RUN_DEFAULT);

	while (true) {
		usleep(10000);
	}

	v8::V8::Dispose();

	return result;
}
