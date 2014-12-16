#ifndef INCLUDED_File
#define INCLUDED_File

#include <v8.h>

struct uv_fs_s;
typedef uv_fs_s uv_fs_t;

class File {
public:
	static void configure(v8::Isolate* isolate, v8::Handle<v8::ObjectTemplate> global);

private:
	static void readFile(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void writeFile(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void readDirectory(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void makeDirectory(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void unlinkFile(const v8::FunctionCallbackInfo<v8::Value>& args);
	static void renameFile(const v8::FunctionCallbackInfo<v8::Value>& args);

	static void onMakeDirectory(uv_fs_t* request);
};

#endif
