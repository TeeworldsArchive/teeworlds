/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_NET_QUEUE_H
#define ENGINE_SHARED_NET_QUEUE_H

#include <base/system.h>
#include <base/tl/array.h>
#include <base/tl/threading.h>

/*
	A queue that hands entries from the network thread to the game thread.

	The two threads never touch the same connection or socket, they only meet
	in one of these queues, which owns its own lock.

	The lock and the array are held behind pointers because CNetClient::Open
	and CNetServer::Open wipe their whole object with mem_zero, which would
	clobber an embedded array's heap pointer and leak it. Zeroing a pointer is
	harmless: Setup() creates the two again after that, Destroy() releases
	them.
*/
template<typename T>
class CNetQueue
{
	lock *m_pLock;
	array<T> *m_pEntries;

public:
	CNetQueue() :
		m_pLock(0),
		m_pEntries(0)
	{
	}

	~CNetQueue()
	{
		Destroy();
	}

	CNetQueue(const CNetQueue &) = delete;
	CNetQueue &operator=(const CNetQueue &) = delete;

	// (re)create the lock and the entry array, dropping anything queued
	void Setup()
	{
		if(!m_pLock)
			m_pLock = new lock();
		if(!m_pEntries)
			m_pEntries = new array<T>();
		else
			m_pEntries->clear();
	}

	// release everything; the queue is unusable until Setup() runs again
	void Destroy()
	{
		delete m_pEntries;
		m_pEntries = 0;
		delete m_pLock;
		m_pLock = 0;
	}

	// add one entry
	void Push(const T &Entry)
	{
		if(!m_pLock || !m_pEntries)
			return;
		scope_lock Guard(m_pLock);
		m_pEntries->add(Entry);
	}

	// move everything queued into Out, oldest first
	void Drain(array<T> &Out)
	{
		if(!m_pLock || !m_pEntries)
			return;
		scope_lock Guard(m_pLock);
		for(int i = 0; i < m_pEntries->size(); i++)
			Out.add((*m_pEntries)[i]);
		m_pEntries->clear();
	}

	// take the oldest entry, returns false when the queue is empty
	bool Pop(T &Out)
	{
		if(!m_pLock || !m_pEntries)
			return false;
		scope_lock Guard(m_pLock);
		if(m_pEntries->size() == 0)
			return false;
		Out = (*m_pEntries)[0];
		m_pEntries->remove_index(0);
		return true;
	}

	int NumEntries()
	{
		if(!m_pLock || !m_pEntries)
			return 0;
		scope_lock Guard(m_pLock);
		return m_pEntries->size();
	}

	void Clear()
	{
		if(!m_pLock || !m_pEntries)
			return;
		scope_lock Guard(m_pLock);
		m_pEntries->clear();
	}
};

#endif
