/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_SHARED_JOBS_H
#define ENGINE_SHARED_JOBS_H

#include <base/system/lock.h>
#include <base/system/semaphore.h>

#include <base/system/lock.h>
#include <base/system/debug.h>
#include <base/system/fs.h>
#include <base/system/semaphore.h>
typedef int (*JOBFUNC)(void *pData);

class CJobPool;

class CJob
{
	friend class CJobPool;

	CJob *m_pPrev;
	CJob *m_pNext;

	volatile int m_Status;
	volatile int m_Result;

	JOBFUNC m_pfnFunc;
	void *m_pFuncData;

public:
	CJob()
	{
		m_Status = STATE_DONE;
		m_pFuncData = 0;
	}

	enum
	{
		STATE_PENDING = 0,
		STATE_RUNNING,
		STATE_DONE
	};

	int Status() const { return m_Status; }
	int Result() const { return m_Result; }
};

class CJobPool
{
	enum
	{
		MAX_THREADS = 32
	};
	int m_NumThreads;
	void *m_apThreads[MAX_THREADS];
	volatile bool m_Shutdown;

	LOCK m_Lock;
	CJob *m_pFirstJob;
	CJob *m_pLastJob;

	static void WorkerThread(void *pUser);

public:
	CJobPool();
	~CJobPool();

	int Init(int NumThreads);
	void Shutdown();
	int Add(CJob *pJob, JOBFUNC pfnFunc, void *pData);
	int NumThreads() { return m_NumThreads; }
};
#endif
