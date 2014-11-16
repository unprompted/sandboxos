#ifndef INCLUDED_Serialize
#define INCLUDED_Serialize

#include <v8.h>
#include <vector>

class Task;

class Serialize {
public:
	static bool store(Task* task, std::vector<char>& buffer, v8::Handle<v8::Value> value);
	static v8::Handle<v8::Value> load(Task* task, const std::vector<char>& buffer);
};

#endif
