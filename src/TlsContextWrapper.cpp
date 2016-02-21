#include "TlsContextWrapper.h"

#include "Task.h"
#include "Tls.h"

#include <assert.h>

int TlsContextWrapper::_count = 0;

void TlsContextWrapper::create(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::HandleScope handleScope(args.GetIsolate());
	if (TlsContextWrapper* wrapper = new TlsContextWrapper(Task::get(args.GetIsolate()))) {
		v8::Handle<v8::Object> result = v8::Local<v8::Object>::New(args.GetIsolate(), wrapper->_object);
		args.GetReturnValue().Set(result);
		wrapper->release();
	}
}

TlsContextWrapper::TlsContextWrapper(Task* task) {
	++_count;
	v8::HandleScope scope(task->getIsolate());
	v8::Handle<v8::External> identifier = v8::External::New(task->getIsolate(), reinterpret_cast<void*>(&TlsContextWrapper::create));
	v8::Handle<v8::External> data = v8::External::New(task->getIsolate(), this);

	v8::Local<v8::ObjectTemplate> wrapperTemplate = v8::ObjectTemplate::New(task->getIsolate());
	wrapperTemplate->SetInternalFieldCount(2);
	wrapperTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "setCertificate"), v8::FunctionTemplate::New(task->getIsolate(), setCertificate, data));
	wrapperTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "setPrivateKey"), v8::FunctionTemplate::New(task->getIsolate(), setPrivateKey, data));
	wrapperTemplate->Set(v8::String::NewFromUtf8(task->getIsolate(), "addTrustedCertificate"), v8::FunctionTemplate::New(task->getIsolate(), addTrustedCertificate, data));

	v8::Local<v8::Object> wrapperObject = wrapperTemplate->NewInstance();
	wrapperObject->SetInternalField(0, identifier);
	wrapperObject->SetInternalField(1, data);
	_object.Reset(task->getIsolate(), wrapperObject);

	_context = TlsContext::create();
	_task = task;
}

TlsContextWrapper::~TlsContextWrapper() {
	close();
	--_count;
}

void TlsContextWrapper::close() {
	if (_context) {
		delete _context;
		_context = 0;
	}
}

void TlsContextWrapper::onRelease(const v8::WeakCallbackData<v8::Object, TlsContextWrapper>& data) {
	data.GetParameter()->_object.Reset();
	delete data.GetParameter();
}

TlsContextWrapper* TlsContextWrapper::get(v8::Handle<v8::Value> value) {
	TlsContextWrapper* result = 0;

	if (!value.IsEmpty()
		&& value->IsObject())
	{
		v8::Handle<v8::Object> object = v8::Handle<v8::Object>::Cast(value);
		if (object->InternalFieldCount() == 2
			&& v8::Handle<v8::External>::Cast(object->GetInternalField(0))->Value() == &TlsContextWrapper::create)
		{
			result = reinterpret_cast<TlsContextWrapper*>(v8::Handle<v8::External>::Cast(object->GetInternalField(1))->Value());
		}
	}

	return result;
}

TlsContextWrapper* TlsContextWrapper::get(const v8::FunctionCallbackInfo<v8::Value>& args) {
	return reinterpret_cast<TlsContextWrapper*>(v8::Handle<v8::External>::Cast(args.Data())->Value());
}

void TlsContextWrapper::ref() {
	if (++_refCount == 1) {
		_object.ClearWeak();
	}
}

void TlsContextWrapper::release() {
	assert(_refCount >= 1);
	if (--_refCount == 0) {
		_object.SetWeak(this, onRelease);
	}
}

void TlsContextWrapper::setCertificate(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TlsContextWrapper* wrapper = TlsContextWrapper::get(args)) {
		v8::String::Utf8Value value(args[0]->ToString(args.GetIsolate()));
		wrapper->_context->setCertificate(*value);
	}
}

void TlsContextWrapper::setPrivateKey(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TlsContextWrapper* wrapper = TlsContextWrapper::get(args)) {
		v8::String::Utf8Value value(args[0]->ToString(args.GetIsolate()));
		wrapper->_context->setPrivateKey(*value);
	}
}

void TlsContextWrapper::addTrustedCertificate(const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (TlsContextWrapper* wrapper = TlsContextWrapper::get(args)) {
		v8::String::Utf8Value value(args[0]->ToString(args.GetIsolate()));
		wrapper->_context->addTrustedCertificate(*value);
	}
}

int TlsContextWrapper::getCount()
{
	return _count;
}
