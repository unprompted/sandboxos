#include <cstring>
#include <libplatform/libplatform.h>
#include <unistd.h>
#include <uv.h>
#include <sys/prctl.h>
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
		prctl(PR_SET_PDEATHSIG, SIGHUP);
		Task task;
		task.configureFromStdin();
		task.run();
	} else {
		setpgid(0, 0);
		Task task;
		task.setTrusted(true);
		task.execute("packages/system/system.js");
		task.run();
	}

	v8::V8::Dispose();

	return 0;
}
