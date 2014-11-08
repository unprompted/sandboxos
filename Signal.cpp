#include "Signal.h"

#include <assert.h>
#include <iostream>

Signal::Signal()
:	_count(0) {
	int result = pthread_cond_init(&_condition, 0);
	assert_perror(result);
}

Signal::~Signal() {
	int result = pthread_cond_destroy(&_condition);
	assert_perror(result);
}

void Signal::signal() {
	Lock lock(_mutex);
	++_count;
	std::cout << "count => " << _count << "\n";
	pthread_cond_signal(&_condition);
}

bool Signal::wait() {
	bool gotSignal = false;
	std::cout << "count = " << _count << "\n";
	{
		Lock lock(_mutex);
		while (_count == 0) {
			pthread_cond_wait(&_condition, &_mutex._mutex);
		}
		gotSignal = _count > 0;
		if (gotSignal) {
			--_count;
		}
	}
	return gotSignal;
}
