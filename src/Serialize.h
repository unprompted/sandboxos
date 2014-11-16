#ifndef INCLUDED_Serialize
#define INCLUDED_Serialize

#include <v8.h>
#include <vector>

class Task;

class Serialize {
public:
	static bool store(Task* task, std::vector<char>& buffer, v8::Handle<v8::Value> value);
	static v8::Handle<v8::Value> load(Task* task, Task* from, const std::vector<char>& buffer);

private:
	static bool storeInternal(Task* task, std::vector<char>& buffer, v8::Handle<v8::Value> value, int depth);
	static v8::Handle<v8::Value> loadInternal(Task* task, Task* from, const std::vector<char>& buffer, int& offse, int deptht);

	static void writeInt8(std::vector<char>& buffer, int8_t value);
	static void writeInt32(std::vector<char>& buffer, int32_t value);
	static void writeUint32(std::vector<char>& buffer, uint32_t value);
	static void writeDouble(std::vector<char>& buffer, double value);

	static int8_t readInt8(const std::vector<char>& buffer, int& offset);
	static int32_t readInt32(const std::vector<char>& buffer, int& offset);
	static uint32_t readUint32(const std::vector<char>& buffer, int& offset);
	static double readDouble(const std::vector<char>& buffer, int& offset);
};

#endif
