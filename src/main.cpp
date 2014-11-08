#include <v8.h>
#include <v8-platform.h>
#include <libplatform/libplatform.h>

#include "Task.h"

v8::Platform* gPlatform = 0;

int main(int argc, char* argv[]) {
	v8::V8::InitializeICU();
	gPlatform = v8::platform::CreateDefaultPlatform();
	v8::V8::InitializePlatform(gPlatform);
	v8::V8::Initialize();
	v8::V8::SetFlagsFromCommandLine(&argc, argv, true);

	{
		Task task("system.js");
		task.setTrusted(true);
		task.run();
	}

	v8::V8::Dispose();

	return 0;
}
