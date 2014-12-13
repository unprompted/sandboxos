#include "Task.h"

#include <cstring>
#include <libplatform/libplatform.h>
#include <uv.h>
#include <v8.h>
#include <v8-platform.h>

#if !defined (WIN32) && !defined (__MACH__)
#include <sys/prctl.h>
#include <unistd.h>
#endif

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
#if !defined (WIN32) && !defined (__MACH__)
		prctl(PR_SET_PDEATHSIG, SIGHUP);
#endif
		Task task;
		task.configureFromStdin();
		task.activate();
		task.run();
	} else {
#if !defined (WIN32) && !defined (__MACH__)
		setpgid(0, 0);
#endif
		Task task;
		task.setTrusted(true);
		task.activate();
		task.execute("packages/system/system.js");
		task.run();
	}

	v8::V8::Dispose();

	return 0;
}
