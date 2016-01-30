#include "Database.h"

#include "Task.h"

#include <assert.h>
#include <sstream>

int Database::_count = 0;

Database::Database(Task* task) {
	++_count;

	_task = task;

	v8::Handle<v8::External> data = v8::External::New(task->getIsolate(), this);

	v8::Local<v8::ObjectTemplate> databaseTemplate = v8::ObjectTemplate::New(task->getIsolate());
	databaseTemplate->SetInternalFieldCount(1);
	databaseTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "get"), v8::FunctionTemplate::New(task->getIsolate(), get, data));
	databaseTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "set"), v8::FunctionTemplate::New(task->getIsolate(), set, data));
	databaseTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "remove"), v8::FunctionTemplate::New(task->getIsolate(), remove, data));
	databaseTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "getAll"), v8::FunctionTemplate::New(task->getIsolate(), getAll, data));

	v8::Local<v8::Object> databaseObject = databaseTemplate->NewInstance();
	databaseObject->SetInternalField(0, v8::External::New(task->getIsolate(), this));
	_object = v8::Persistent<v8::Object, v8::CopyablePersistentTraits<v8::Object> >(task->getIsolate(), databaseObject);
}

Database::~Database() {
	--_count;
}

void Database::create(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope handleScope(args.GetIsolate());
	if (Database* database = new Database(Task::get(args.GetIsolate()))) {
		if (database->open(args.GetIsolate(), *v8::String::Utf8Value(args[0].As<v8::String>()))) {
			v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), database->_object);
			args.GetReturnValue().Set(result);
		}
		database->release();
	}
}

bool Database::checkError(const char* command, int result) {
	bool isError = false;
	if (result != MDB_SUCCESS) {
		isError = true;

		std::ostringstream buffer;
		buffer << command << " failed (" << result << "): " << mdb_strerror(result);
		_task->getIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(_task->getIsolate(), buffer.str().c_str())));
	}
	return isError;
}

bool Database::open(v8::Isolate* isolate, const char* path) {
	int result = mdb_env_create(&_environment);
	if (checkError("mdb_env_create", result)) {
		return false;
	}

	result = mdb_env_set_maxdbs(_environment, 10);
	checkError("mdb_env_set_maxdbs", result);

	result = mdb_env_open(_environment, path, 0, 0644);
	if (!checkError("mdb_env_open", result)) {
		result = mdb_txn_begin(_environment, 0, 0, &_transaction);
		if (!checkError("mdb_txn_begin", result)) {
			result = mdb_dbi_open(_transaction, NULL, MDB_CREATE, &_database);
			if (!checkError("mdb_dbi_open", result)) {
				result = mdb_txn_commit(_transaction);
				checkError("mdb_txn_commit", result);
			}
		}

		if (result != MDB_SUCCESS) {
			mdb_txn_abort(_transaction);
		}
	}

	if (result != MDB_SUCCESS) {
		mdb_env_close(_environment);
	}

	return result == MDB_SUCCESS;
}

void Database::get(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Database* database = Database::get(args.Data())) {
		int result = mdb_txn_begin(database->_environment, 0, MDB_RDONLY, &database->_transaction);
		if (!database->checkError("mdb_txn_begin", result)) {
			MDB_val key;
			MDB_val value;
			v8::String::Utf8Value keyString(args[0].As<v8::String>());
			key.mv_data = *keyString;
			key.mv_size = keyString.length();
			if (mdb_get(database->_transaction, database->_database, &key, &value) == MDB_SUCCESS) {
				args.GetReturnValue().Set(v8::String::NewFromUtf8(args.GetIsolate(), reinterpret_cast<const char*>(value.mv_data), v8::String::kNormalString, value.mv_size));
			}
			mdb_txn_reset(database->_transaction);
		}
	}
}

void Database::set(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Database* database = Database::get(args.Data())) {
		int result = mdb_txn_begin(database->_environment, 0, 0, &database->_transaction);
		if (!database->checkError("mdb_txn_begin", result)) {
			MDB_val key;
			MDB_val data;
			v8::String::Utf8Value keyString(args[0].As<v8::String>());
			key.mv_data = *keyString;
			key.mv_size = keyString.length();
			v8::String::Utf8Value valueString(args[1]->ToString(args.GetIsolate()));
			data.mv_data = *valueString;
			data.mv_size = valueString.length();
			result = mdb_put(database->_transaction, database->_database, &key, &data, 0);
			database->checkError("mdb_put", result);
			mdb_txn_commit(database->_transaction);
		}
	}
}

void Database::remove(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Database* database = Database::get(args.Data())) {
		int result = mdb_txn_begin(database->_environment, 0, 0, &database->_transaction);
		if (!database->checkError("mdb_txn_begin", result)) {
			MDB_val key;
			v8::String::Utf8Value keyString(args[0].As<v8::String>());
			key.mv_data = *keyString;
			key.mv_size = keyString.length();
			result = mdb_del(database->_transaction, database->_database, &key, 0);
			database->checkError("mdb_del", result);
			mdb_txn_commit(database->_transaction);
		}
	}
}

void Database::getAll(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (Database* database = Database::get(args.Data())) {
		int result = mdb_txn_begin(database->_environment, 0, MDB_RDONLY, &database->_transaction);
		if (!database->checkError("mdb_txn_begin", result)) {
			MDB_cursor* cursor;
			result = mdb_cursor_open(database->_transaction, database->_database, &cursor);
			if (!database->checkError("mdb_cursor_open", result)) {
				int expectedCount = 0;
				MDB_stat statistics;
				if (mdb_stat(database->_transaction, database->_database, &statistics) == 0) {
					expectedCount = statistics.ms_entries;
				}
				v8::Local<v8::Array> array = v8::Array::New(args.GetIsolate(), expectedCount);

				MDB_val key;
				int index = 0;
				while ((result = mdb_cursor_get(cursor, &key, 0, MDB_NEXT)) == 0) {
					array->Set(index++, v8::String::NewFromUtf8(args.GetIsolate(), reinterpret_cast<const char*>(key.mv_data), v8::String::kNormalString, key.mv_size));
				}
				if (result == MDB_NOTFOUND) {
					args.GetReturnValue().Set(array);
				} else {
					database->checkError("mdb_cursor_get", result);
				}
				mdb_cursor_close(cursor);
			}
			mdb_txn_reset(database->_transaction);
		}
	}
}

void Database::onRelease(const v8::WeakCallbackData<v8::Object, Database>& data) {
	data.GetParameter()->_object.Reset();
	delete data.GetParameter();
}

void Database::ref() {
	if (++_refCount == 1) {
		_object.ClearWeak();
	}
}

void Database::release() {
	assert(_refCount >= 1);
	if (--_refCount == 0) {
		_object.SetWeak(this, onRelease);
	}
}

Database* Database::get(v8::Handle<v8::Value> databaseObject) {
	return reinterpret_cast<Database*>(v8::Handle<v8::External>::Cast(databaseObject)->Value());
}
