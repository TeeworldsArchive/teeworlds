/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_MESSAGE_H
#define ENGINE_MESSAGE_H

#include <engine/shared/packer.h>
#include <engine/shared/uuid_manager.h>

class CMsgPacker : public CPacker
{
public:
	CMsgPacker(int Type, bool System = false)
	{
		Reset();
		// NETMSG_EX, NETMSGTYPE_EX for UUID messages
		int NetType = Type < OFFSET_UUID ? Type : 0;
		AddInt((NetType<<1)|(System?1:0));
		if(Type >= OFFSET_UUID)
		{
			g_UuidManager.PackUuid(Type, this);
		}
	}
};

class CMsgUnpacker : public CUnpacker
{
	int m_Type;
	bool m_System;

public:
	CMsgUnpacker(const void *pData, int Size)
	{
		Reset(pData, Size);
		const int Msg = GetInt();
		if(Msg < 0)
			m_Error = true;
		if(m_Error)
		{
			m_System = false;
			m_Type = 0;
			return;
		}
		m_System = Msg & 1;
		m_Type = Msg >> 1;
	}

	int Type() const { return m_Type; }
	bool System() const { return m_System; }
};

#endif
