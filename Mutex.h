#include <pthread.h>

class Mutex {
public:
	Mutex();
	~Mutex();

	void lock();
	void unlock();

private:
	pthread_mutex_t _mutex;
};

class Lock {
public:
	Lock(Mutex& mutex);
	~Lock();
private:
	Mutex& _mutex;
};
