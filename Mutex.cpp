#include "Mutex.h"
#include <iostream>
#include <assert.h>

Mutex::Mutex() {
	pthread_mutex_init(&_mutex, 0);
}

Mutex::~Mutex() {
	pthread_mutex_destroy(&_mutex);
}

void Mutex::lock() {
	int result = pthread_mutex_lock(&_mutex);
	assert_perror(result);
}

void Mutex::unlock() {
	int result = pthread_mutex_unlock(&_mutex);
	assert_perror(result);
}

Lock::Lock(Mutex& mutex)
:	_mutex(mutex) {
	_mutex.lock();
}

Lock::~Lock() {
	_mutex.unlock();
}
