#include "File.h"

#include "Task.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <uv.h>

#ifdef WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

struct FileRequest {
	uv_fs_t request;
	Task* task;
	promiseid_t promise;
};

void File::configure(v8::Isolate* isolate, v8::Handle<v8::ObjectTemplate> global) {
	v8::Local<v8::ObjectTemplate> fileTemplate = v8::ObjectTemplate::New(isolate);
	fileTemplate->Set(v8::String::NewFromUtf8(isolate, "readFile"), v8::FunctionTemplate::New(isolate, readFile));
	fileTemplate->Set(v8::String::NewFromUtf8(isolate, "readDirectory"), v8::FunctionTemplate::New(isolate, readDirectory));
	fileTemplate->Set(v8::String::NewFromUtf8(isolate, "makeDirectory"), v8::FunctionTemplate::New(isolate, makeDirectory));
	fileTemplate->Set(v8::String::NewFromUtf8(isolate, "writeFile"), v8::FunctionTemplate::New(isolate, writeFile));
	fileTemplate->Set(v8::String::NewFromUtf8(isolate, "renameFile"), v8::FunctionTemplate::New(isolate, renameFile));
	fileTemplate->Set(v8::String::NewFromUtf8(isolate, "unlinkFile"), v8::FunctionTemplate::New(isolate, unlinkFile));
	global->Set(v8::String::NewFromUtf8(isolate, "File"), fileTemplate);
}

void File::readFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> fileName = args[0]->ToString();

	v8::String::Utf8Value utf8FileName(fileName);
	std::ifstream file(*utf8FileName, std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
	std::streampos fileSize = file.tellg();
	if (fileSize >= 0 && fileSize < 4 * 1024 * 1024) {
		file.seekg(0, std::ios_base::beg);
		char* buffer = new char[fileSize];
		file.read(buffer, fileSize);
		std::string contents(buffer, buffer + fileSize);
		args.GetReturnValue().Set(v8::String::NewFromOneByte(args.GetIsolate(), reinterpret_cast<const uint8_t*>(buffer), v8::String::kNormalString, fileSize));
		delete[] buffer;
	}
}

void File::writeFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> fileName = args[0]->ToString();
	v8::Handle<v8::String> contents = args[1]->ToString();

	v8::String::Utf8Value utf8FileName(fileName);
	std::ofstream file(*utf8FileName, std::ios_base::out | std::ios_base::binary);

	v8::String::Utf8Value utf8Contents(contents);
	if (!file.write(*utf8Contents, utf8Contents.length())) {
		args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), -1));
	}
}

void File::renameFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::HandleScope scope(args.GetIsolate());

	v8::String::Utf8Value oldName(args[0]->ToString());
	v8::String::Utf8Value newName(args[1]->ToString());

	uv_fs_t req;
	int result = uv_fs_rename(task->getLoop(), &req, *oldName, *newName, 0);
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), result));
}

void File::unlinkFile(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = reinterpret_cast<Task*>(args.GetIsolate()->GetData(0));
	v8::HandleScope scope(args.GetIsolate());

	v8::String::Utf8Value fileName(args[0]->ToString());

	uv_fs_t req;
	int result = uv_fs_unlink(task->getLoop(), &req, *fileName, 0);
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), result));
}

void File::readDirectory(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> directory = args[0]->ToString();

	v8::Handle<v8::Array> array = v8::Array::New(args.GetIsolate(), 0);

#ifdef WIN32
	WIN32_FIND_DATA find;
	std::string pattern = *v8::String::Utf8Value(directory);
	pattern += "\\*";
	HANDLE handle = FindFirstFile(pattern.c_str(), &find);
	if (handle != INVALID_HANDLE_VALUE) {
		int index = 0;
		do {
			array->Set(v8::Integer::New(args.GetIsolate(), index++), v8::String::NewFromUtf8(args.GetIsolate(), find.cFileName));
		} while (FindNextFile(handle, &find) != 0);
		FindClose(handle);
	}
#else
	if (DIR* dir = opendir(*v8::String::Utf8Value(directory))) {
		int index = 0;
		while (struct dirent* entry = readdir(dir)) {
			array->Set(v8::Integer::New(args.GetIsolate(), index++), v8::String::NewFromUtf8(args.GetIsolate(), entry->d_name));
		}
		closedir(dir);
	}
#endif

	args.GetReturnValue().Set(array);
}

void File::makeDirectory(const v8::FunctionCallbackInfo<v8::Value>& args) {
	Task* task = Task::get(args.GetIsolate());
	v8::HandleScope scope(args.GetIsolate());
	v8::Handle<v8::String> directory = args[0]->ToString();

	FileRequest* request = new FileRequest;
	std::memset(request, 0, sizeof(*request));
	request->request.data = request;
	request->task = task;
	request->promise = task->allocatePromise();
	uv_fs_mkdir(task->getLoop(), &request->request, *v8::String::Utf8Value(directory), 0777, onMakeDirectory);
	args.GetReturnValue().Set(task->getPromise(request->promise));
}

void File::onMakeDirectory(uv_fs_t* request) {
	FileRequest* fileRequest = reinterpret_cast<FileRequest*>(request->data);
	if (request->result != 0) {
		fileRequest->task->rejectPromise(fileRequest->promise, v8::Integer::New(fileRequest->task->getIsolate(), request->result));
	} else {
		fileRequest->task->resolvePromise(fileRequest->promise, v8::Integer::New(fileRequest->task->getIsolate(), request->result));
	}
	delete fileRequest;
}
