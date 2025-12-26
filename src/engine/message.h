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
		if(Type < 0 || Type > 0x3FFFFFFF)
		{
			m_Error = true;
			return;
		}
		if(Type < OFFSET_UUID)
		{
			AddInt((Type << 1) | (System ? 1 : 0));
		}
		else
		{
			AddInt(System ? 1 : 0);
			g_UuidManager.PackUuid(Type, this);
		}
	}
};

class CMsgUnpacker : public CUnpacker
{
	int m_Type;
	bool m_System;
	Uuid m_Uuid;

public:
	CMsgUnpacker() = default;

	CMsgUnpacker(const void *pData, int Size)
	{
		m_Uuid = UUID_ZEROED;

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
		if(m_Type == 0)
		{
			m_Type = g_UuidManager.UnpackUuid(this, &m_Uuid);
			if(m_Type == UUID_INVALID || m_Type == UUID_UNKNOWN)
				m_Error = true;
		}
	}

	int Type() const { return m_Type; }
	bool System() const { return m_System; }
	const Uuid *MsgUuid() const { return &m_Uuid; }
};

#endif
