/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <base/system/base.h>
#include <base/system/mem.h>
#include <base/system/thread.h>

#include <atomic>
#include <chrono>
#include <new>
#include <thread>

#if defined(CONF_ARCH_IA32) || defined(CONF_ARCH_AMD64)
#include <immintrin.h> //_mm_pause
#endif

/* Thread arguments, freed by the thread itself on exit. Kept separate from
   THREAD_HANDLE so a detached thread never touches the caller's memory. */
struct THREAD_RUN
{
	void (*m_pfnThreadfunc)(void *);
	void *m_pUser;
};

/* What thread_init() returns as void *. Owns the std::thread; THREAD_RUN
   outlives it when the thread is detached. */
struct THREAD_HANDLE
{
	std::thread m_Thread;
	THREAD_RUN *m_pRun;
	bool m_Detached;
};

static void thread_run(THREAD_RUN *pData)
{
	void (*pfnThreadfunc)(void *) = pData->m_pfnThreadfunc;
	void *pUser = pData->m_pUser;
	mem_free(pData);
	pfnThreadfunc(pUser);
}

void *thread_init(void (*threadfunc)(void *), void *user)
{
	THREAD_RUN *pRun = (THREAD_RUN *) mem_alloc(sizeof(THREAD_RUN));
	pRun->m_pfnThreadfunc = threadfunc;
	pRun->m_pUser = user;

	THREAD_HANDLE *pHandle = (THREAD_HANDLE *) mem_alloc(sizeof(THREAD_HANDLE));
	pHandle->m_pRun = pRun;
	pHandle->m_Detached = false;
	new(&pHandle->m_Thread) std::thread(thread_run, pRun);
	return (void *) pHandle;
}

void thread_wait(void *thread)
{
	THREAD_HANDLE *pHandle = (THREAD_HANDLE *) thread;
	if(!pHandle)
	{
		return;
	}
	if(pHandle->m_Thread.joinable())
	{
		pHandle->m_Thread.join();
	}
	pHandle->m_Thread.~thread();
	mem_free(pHandle);
}

void sync_barrier()
{
	std::atomic_thread_fence(std::memory_order_seq_cst);
}

void thread_yield()
{
	std::this_thread::yield();
}

void thread_sleep(int milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void thread_detach(void *thread)
{
	THREAD_HANDLE *pHandle = (THREAD_HANDLE *) thread;
	if(!pHandle)
	{
		return;
	}
	if(pHandle->m_Thread.joinable())
	{
		pHandle->m_Thread.detach();
	}
	// Freeing the handle is safe: THREAD_RUN is owned by the thread now.
	pHandle->m_Thread.~thread();
	mem_free(pHandle);
}

unsigned get_hardware_concurrency()
{
	unsigned Cores = std::thread::hardware_concurrency();
	return Cores;
}

void cpu_relax()
{
#if defined(CONF_ARCH_IA32) || defined(CONF_ARCH_AMD64)
	_mm_pause();
#else
	(void) 0;
#endif
}
