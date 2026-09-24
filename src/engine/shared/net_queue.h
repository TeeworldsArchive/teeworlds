/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_NET_QUEUE_H
#define ENGINE_SHARED_NET_QUEUE_H

#include <base/system.h>
#include <base/tl/array.h>
#include <base/tl/threading.h>

/*
	The hand-off between the network thread and the game thread.

	The network thread never touches game state and the game thread never
	touches a connection or a socket: the two sides only meet in one of these
	queues, which owns its own lock. This mirrors the way TWICE separates its
	transport thread from its game thread.
*/
template<typename T>
class CNetQueue
{
	/*
		The lock is held by pointer because CNetServer::Open zeroes its whole
		object with mem_zero, which would destroy one embedded by value.
		Setup() is called after that and creates it again; it is idempotent.
	*/
	lock *m_pLock;
	array<T> m_Entries;

public:
	CNetQueue() :
		m_pLock(0)
	{
	}

	~CNetQueue()
	{
		delete m_pLock;
	}

	CNetQueue(const CNetQueue &) = delete;
	CNetQueue &operator=(const CNetQueue &) = delete;

	// (re)create the lock and drop anything queued
	void Setup()
	{
		if(!m_pLock)
			m_pLock = new lock();
		m_Entries.clear();
	}

	// add one entry
	void Push(const T &Entry)
	{
		if(!m_pLock)
			return;
		scope_lock Guard(m_pLock);
		m_Entries.add(Entry);
	}

	// move everything queued into Out, oldest first
	void Drain(array<T> &Out)
	{
		if(!m_pLock)
			return;
		scope_lock Guard(m_pLock);
		for(int i = 0; i < m_Entries.size(); i++)
			Out.add(m_Entries[i]);
		m_Entries.clear();
	}

	// take the oldest entry, returns false when the queue is empty
	bool Pop(T &Out)
	{
		if(!m_pLock)
			return false;
		scope_lock Guard(m_pLock);
		if(m_Entries.size() == 0)
			return false;
		Out = m_Entries[0];
		m_Entries.remove_index(0);
		return true;
	}

	int NumEntries()
	{
		if(!m_pLock)
			return 0;
		scope_lock Guard(m_pLock);
		return m_Entries.size();
	}

	void Clear()
	{
		if(!m_pLock)
			return;
		scope_lock Guard(m_pLock);
		m_Entries.clear();
	}
};

#endif
