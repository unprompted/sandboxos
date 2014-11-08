#ifndef INCLUDED_Mutex
#define INCLUDED_Mutex

#include <uv.h>

class Mutex {
public:
	Mutex();
	~Mutex();

	void lock();
	void unlock();

private:
	uv_mutex_t _mutex;

	friend class Signal;
};

class Lock {
public:
	Lock(Mutex& mutex);
	~Lock();
private:
	Mutex& _mutex;
};

#endif
