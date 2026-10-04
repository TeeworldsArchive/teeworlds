/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_THREAD_H
#define BASE_SYSTEM_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Threads */

/*
	Function: sync_barrier
		Ensures memory operation ordering. Guarantees that all writes before
		this point are visible to other threads before any subsequent writes occur.
*/
void sync_barrier();

/*
	Function: thread_sleep
		Suspends the current thread for a given period.

	Parameters:
		milliseconds - Number of milliseconds to sleep.
*/
void thread_sleep(int milliseconds);

/*
	Function: thread_init
		Creates a new thread.

	Parameters:
		threadfunc - Entry point for the new thread.
		user - Pointer to pass to the thread.

*/
void *thread_init(void (*threadfunc)(void *), void *user);

/*
	Function: thread_wait
		Waits for a thread to be done or destroyed.

	Parameters:
		thread - Thread to wait for.
*/
void thread_wait(void *thread);

/*
	Function: thread_yield
		Yield the current threads execution slice.
*/
void thread_yield();

/*
	Function: thread_detach
		Puts the thread in the detached state, guaranteeing that
		resources of the thread will be freed immediately when the
		thread terminates.

	Parameters:
		thread - Thread to detach

	Remarks:
		- This invalidates the thread handle, hence it must not be
		used after detaching the thread.
*/
void thread_detach(void *thread);

/*
	Function: get_hardware_concurrency
	A implementation equivalent to std::thread::hardware_concurrency() (C++11).
	Returns a value that hints at the number of hardware thread contexts.
*/
unsigned get_hardware_concurrency();

/*
	Function: cpu_relax
		Lets the cpu relax a bit.
*/
void cpu_relax();

#ifdef __cplusplus
}
#endif

#endif
