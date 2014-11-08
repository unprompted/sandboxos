#include <v8-platform.h>
#include <string>

class Task : public v8::Task {
public:
	Task();
	~Task();
	void Run();
	void setScript(const char* scriptName) { _scriptName = scriptName; }

	static int getCount() { return _count; }
private:
	static int _count;
	std::string _scriptName;
};
