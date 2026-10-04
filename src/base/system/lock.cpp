/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <base/system/lock.h>
#include <base/system/mem.h>

#include <mutex>
#include <new>

/* Private: the public API exposes LOCK only as an opaque void *. */
struct LOCKINTERNAL
{
	std::mutex m_Mutex;
};

LOCK lock_create()
{
	LOCKINTERNAL *pLock = (LOCKINTERNAL *) mem_alloc(sizeof(LOCKINTERNAL));
	// mem_alloc() is a bare malloc(), so the constructor must run here.
	new(&pLock->m_Mutex) std::mutex();
	return (LOCK) pLock;
}

void lock_destroy(LOCK lock)
{
	LOCKINTERNAL *pLock = (LOCKINTERNAL *) lock;
	pLock->m_Mutex.~mutex();
	mem_free(pLock);
}

int lock_trylock(LOCK lock)
{
	return ((LOCKINTERNAL *) lock)->m_Mutex.try_lock();
}

int lock_wait(LOCK lock)
{
	((LOCKINTERNAL *) lock)->m_Mutex.lock();
	return 0;
}

int lock_unlock(LOCK lock)
{
	((LOCKINTERNAL *) lock)->m_Mutex.unlock();
	return 0;
}
