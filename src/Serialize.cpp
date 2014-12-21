#include "Serialize.h"

#include "Task.h"
#include "TaskStub.h"

#include <cstring>

void Serialize::writeInt8(std::vector<char>& buffer, int8_t value) {
	buffer.insert(buffer.end(), value);
}

void Serialize::writeInt32(std::vector<char>& buffer, int32_t value) {
	const char* p = reinterpret_cast<char*>(&value);
	buffer.insert(buffer.end(), p, p + sizeof(value));
}

void Serialize::writeUint32(std::vector<char>& buffer, uint32_t value) {
	const char* p = reinterpret_cast<char*>(&value);
	buffer.insert(buffer.end(), p, p + sizeof(value));
}

void Serialize::writeDouble(std::vector<char>& buffer, double value) {
	const char* p = reinterpret_cast<char*>(&value);
	buffer.insert(buffer.end(), p, p + sizeof(value));
}

int8_t Serialize::readInt8(const std::vector<char>& buffer, int& offset) {
	int8_t result;
	std::memcpy(&result, &*buffer.begin() + offset, sizeof(result));
	offset += sizeof(result);
	return result;
}

int32_t Serialize::readInt32(const std::vector<char>& buffer, int& offset) {
	int32_t result;
	std::memcpy(&result, &*buffer.begin() + offset, sizeof(result));
	offset += sizeof(result);
	return result;
}

uint32_t Serialize::readUint32(const std::vector<char>& buffer, int& offset) {
	uint32_t result;
	std::memcpy(&result, &*buffer.begin() + offset, sizeof(result));
	offset += sizeof(result);
	return result;
}

double Serialize::readDouble(const std::vector<char>& buffer, int& offset) {
	double result;
	std::memcpy(&result, &*buffer.begin() + offset, sizeof(result));
	offset += sizeof(result);
	return result;
}

bool Serialize::store(Task* task, std::vector<char>& buffer, v8::Handle<v8::Value> value) {
	return storeInternal(task, buffer, value, 0);
}

bool Serialize::storeInternal(Task* task, std::vector<char>& buffer, v8::Handle<v8::Value> value, int depth) {
	if (value.IsEmpty()) {
		return false;
	} else if (value->IsUndefined()) {
		writeInt32(buffer, kUndefined);
	} else if (value->IsNull()) {
		writeInt32(buffer, kNull);
	} else if (value->IsBoolean()) {
		writeInt32(buffer, kBoolean);
		writeInt8(buffer, value->IsTrue() ? 1 : 0);
	} else if (value->IsInt32()) {
		writeInt32(buffer, kInt32);
		writeInt32(buffer, value->Int32Value());
	} else if (value->IsUint32()) {
		writeInt32(buffer, kUint32);
		writeInt32(buffer, value->Uint32Value());
	} else if (value->IsNumber()) {
		writeInt32(buffer, kNumber);
		writeDouble(buffer, value->NumberValue());
	} else if (value->IsString()) {
		writeInt32(buffer, kString);
		v8::String::Utf8Value utf8(value->ToString());
		writeInt32(buffer, utf8.length());
		buffer.insert(buffer.end(), *utf8, *utf8 + utf8.length());
	} else if (value->IsArray()) {
		writeInt32(buffer, kArray);
		v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(value);
		writeInt32(buffer, array->Length());
		for (size_t i = 0; i < array->Length(); ++i) {
			storeInternal(task, buffer, array->Get(i), depth + 1);
		}
	} else if (value->IsFunction()) {
		writeInt32(buffer, kFunction);
		exportid_t exportId = task->exportFunction(v8::Handle<v8::Function>::Cast(value));
		writeInt32(buffer, exportId);
	} else if (value->IsObject()) {
		writeInt32(buffer, kObject);
		v8::Handle<v8::Object> object = value->ToObject();
		v8::Handle<v8::Array> keys = object->GetOwnPropertyNames();
		writeInt32(buffer, keys->Length());
		for (size_t i = 0; i < keys->Length(); ++i) {
			v8::Handle<v8::Value> key = keys->Get(i);
			storeInternal(task, buffer, key, depth + 1);
			storeInternal(task, buffer, object->Get(key), depth + 1);
		}
	} else {
		writeInt32(buffer, kString);
		v8::String::Utf8Value utf8(value->ToString());
		writeInt32(buffer, utf8.length());
		buffer.insert(buffer.end(), *utf8, *utf8 + utf8.length());
	}

	return true;
}

v8::Handle<v8::Value> Serialize::store(Task* task, v8::TryCatch& tryCatch) {
	v8::Handle<v8::Object> error = v8::Object::New(task->getIsolate());
	error->Set(v8::String::NewFromUtf8(task->getIsolate(), "message"), tryCatch.Message()->Get());
	error->Set(v8::String::NewFromUtf8(task->getIsolate(), "fileName"), tryCatch.Message()->GetScriptResourceName());
	error->Set(v8::String::NewFromUtf8(task->getIsolate(), "lineNumber"), v8::Integer::New(task->getIsolate(), tryCatch.Message()->GetLineNumber()));
	error->Set(v8::String::NewFromUtf8(task->getIsolate(), "sourceLine"), tryCatch.Message()->GetSourceLine());
	error->Set(v8::String::NewFromUtf8(task->getIsolate(), "exception"), tryCatch.Exception());
	error->Set(v8::String::NewFromUtf8(task->getIsolate(), "stackTrace"), tryCatch.StackTrace());
	return error;
}

v8::Handle<v8::Value> Serialize::load(Task* task, TaskStub* from, const std::vector<char>& buffer) {
	int offset = 0;
	return loadInternal(task, from, buffer, offset, 0);
}

v8::Handle<v8::Value> Serialize::loadInternal(Task* task, TaskStub* from, const std::vector<char>& buffer, int& offset, int depth) {
	if (static_cast<size_t>(offset) >= buffer.size()) {
		return v8::Undefined(task->getIsolate());
	} else {
		int32_t type = readInt32(buffer, offset);
		v8::Handle<v8::Value> result;

		switch (type) {
		case kUndefined:
			result = v8::Undefined(task->getIsolate());
			break;
		case kNull:
			result = v8::Null(task->getIsolate());
			break;
		case kBoolean:
			result = v8::Boolean::New(task->getIsolate(), readInt8(buffer, offset) != 0);
			break;
		case kInt32:
			result = v8::Int32::New(task->getIsolate(), readInt32(buffer, offset));
			break;
		case kUint32:
			result = v8::Uint32::New(task->getIsolate(), readUint32(buffer, offset));
			break;
		case kNumber:
			result = v8::Number::New(task->getIsolate(), readDouble(buffer, offset));
			break;
		case kString:
			{
				int32_t length = readInt32(buffer, offset);
				result = v8::String::NewFromUtf8(task->getIsolate(), &*buffer.begin() + offset, v8::String::kNormalString, length);
				offset += length;
			}
			break;
		case kArray:
			{
				int32_t length = readInt32(buffer, offset);
				v8::Handle<v8::Array> array = v8::Array::New(task->getIsolate());
				for (int i = 0; i < length; ++i) {
					v8::Handle<v8::Value> value = loadInternal(task, from, buffer, offset, depth + 1);
					array->Set(i, value);
				}
				result = array;
			}
			break;
		case kFunction:
			{
				exportid_t exportId = readInt32(buffer, offset);
				result = task->addImport(from->getId(), exportId);
			}
			break;
		case kObject:
			{
				int32_t length = readInt32(buffer, offset);
				v8::Handle<v8::Object> object = v8::Object::New(task->getIsolate());
				for (int i = 0; i < length; ++i) {
					v8::Handle<v8::Value> key = loadInternal(task, from, buffer, offset, depth + 1);
					v8::Handle<v8::Value> value = loadInternal(task, from, buffer, offset, depth + 1);
					object->Set(key, value);
				}
				result = object;
			}
			break;
		}
		return result;
	}
}
