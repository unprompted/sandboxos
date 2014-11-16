#include "Serialize.h"

#include "Task.h"

bool Serialize::store(Task* task, std::vector<char>& buffer, v8::Handle<v8::Value> value) {
	v8::Handle<v8::Object> json = task->getIsolate()->GetCurrentContext()->Global()->Get(v8::String::NewFromUtf8(task->getIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> stringify = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(task->getIsolate(), "stringify")));
	v8::String::Utf8Value stored(stringify->Call(json, 1, &value));
	buffer.insert(buffer.end(), *stored, *stored + stored.length());
	return true;
}

v8::Handle<v8::Value> Serialize::load(Task* task, const std::vector<char>& buffer) {
	v8::Handle<v8::Object> json = task->getIsolate()->GetCurrentContext()->Global()->Get(v8::String::NewFromUtf8(task->getIsolate(), "JSON"))->ToObject();
	v8::Handle<v8::Function> parse = v8::Handle<v8::Function>::Cast(json->Get(v8::String::NewFromUtf8(task->getIsolate(), "parse")));
	v8::Handle<v8::Value> arg = v8::String::NewFromUtf8(task->getIsolate(), &*buffer.begin(), v8::String::kNormalString, buffer.size());
	return parse->Call(json, 1, &arg);
}
