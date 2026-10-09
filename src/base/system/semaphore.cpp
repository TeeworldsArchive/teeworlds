/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <base/system/mem.h>
#include <base/system/semaphore.h>

#include <condition_variable>
#include <mutex>
#include <new>

/* Private: the public API exposes SEMAPHORE only as an opaque pointer. */
struct SEMINTERNAL
{
	std::mutex m_Mutex;
	std::condition_variable m_Condition;
	int m_Count;
	int m_Waiters;
};

void sphore_init(SEMAPHORE *sem)
{
	SEMINTERNAL *pSem = (SEMINTERNAL *) mem_alloc(sizeof(SEMINTERNAL));

	// mem_alloc() is a bare malloc(), so the constructors must run here.
	new(&pSem->m_Mutex) std::mutex();
	new(&pSem->m_Condition) std::condition_variable();
	pSem->m_Count = 0;
	pSem->m_Waiters = 0;

	*sem = pSem;
}

void sphore_wait(SEMAPHORE *sem)
{
	SEMINTERNAL *pSem = *sem;
	std::unique_lock<std::mutex> Lock(pSem->m_Mutex);
	while(pSem->m_Count == 0)
	{
		pSem->m_Waiters++;
		pSem->m_Condition.wait(Lock);
		pSem->m_Waiters--;
	}
	pSem->m_Count--;
}

void sphore_signal(SEMAPHORE *sem)
{
	SEMINTERNAL *pSem = *sem;
	std::lock_guard<std::mutex> Lock(pSem->m_Mutex);

	if(pSem->m_Waiters)
		pSem->m_Condition.notify_one();

	pSem->m_Count++;
}

void sphore_destroy(SEMAPHORE *sem)
{
	SEMINTERNAL *pSem = *sem;
	pSem->m_Condition.~condition_variable();
	pSem->m_Mutex.~mutex();
	mem_free(pSem);
	*sem = 0;
}
