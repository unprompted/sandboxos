#include <cstring>
#include <libplatform/libplatform.h>
#include <uv.h>
#include <v8.h>
#include <v8-platform.h>

#include "Task.h"

v8::Platform* gPlatform = 0;

int main(int argc, char* argv[]) {
	uv_setup_args(argc, argv);
	v8::V8::InitializeICU();
	gPlatform = v8::platform::CreateDefaultPlatform();
	v8::V8::InitializePlatform(gPlatform);
	v8::V8::Initialize();
	v8::V8::SetFlagsFromCommandLine(&argc, argv, true);

	bool isChild = false;

	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--child")) {
			isChild = true;
		}
	}

	if (isChild) {
		Task task;
		task.configureFromStdin();
		task.start();
		task.wait();
	} else {
		Task task;
		task.setTrusted(true);
		task.execute("test.js");
		task.start();
		task.wait();
	}

	v8::V8::Dispose();

	return 0;
}
