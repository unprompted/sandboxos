#ifndef INCLUDED_Signal
#define INCLUDED_Signal

#include <pthread.h>

#include "Mutex.h"

class Signal {
public:
	Signal();
	~Signal();

	void signal();
	bool wait();

private:
	int _count;
	pthread_cond_t _condition;
	Mutex _mutex;
};

#endif
