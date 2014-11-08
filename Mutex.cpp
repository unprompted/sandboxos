#include "Mutex.h"

#include <iostream>
#include <assert.h>

Mutex::Mutex() {
	int result = uv_mutex_init(&_mutex);
	if (result != 0) {
		assert("Mutex lock failed.");
	}
}

Mutex::~Mutex() {
	uv_mutex_destroy(&_mutex);
}

void Mutex::lock() {
	uv_mutex_lock(&_mutex);
}

void Mutex::unlock() {
	uv_mutex_unlock(&_mutex);
}

Lock::Lock(Mutex& mutex)
:	_mutex(mutex) {
	_mutex.lock();
}

Lock::~Lock() {
	_mutex.unlock();
}
