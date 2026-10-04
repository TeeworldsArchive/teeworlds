/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef BASE_TL_THREADING_H
#define BASE_TL_THREADING_H

#include <base/system/lock.h>
#include <base/system/debug.h>
#include <base/system/semaphore.h>

class semaphore
{
	SEMAPHORE sem;

public:
	semaphore() { sphore_init(&sem); }
	~semaphore() { sphore_destroy(&sem); }
	void wait() { sphore_wait(&sem); }
	void signal() { sphore_signal(&sem); }
};

class lock
{
	LOCK var;

public:
	void take() { lock_wait(var); }
	void release() { lock_unlock(var); }

	lock()
	{
		var = lock_create();
	}

	~lock()
	{
		lock_destroy(var);
	}
};

class scope_lock
{
	lock *var;

public:
	scope_lock(lock *l)
	{
		var = l;
		var->take();
	}

	~scope_lock()
	{
		var->release();
	}
};

#endif // BASE_TL_THREADING_H
