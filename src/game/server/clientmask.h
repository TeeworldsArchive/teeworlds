#ifndef GAME_SERVER_CLIENTMASK_H
#define GAME_SERVER_CLIENTMASK_H

#include <stdint.h>

#include <base/system.h>
#include <engine/shared/protocol.h>

// A set of client slots. 0.8 raised MAX_CLIENTS to 128, which no longer fits in
// a single 64 bit integer, so this is a small fixed-size bitset.
class CClientMask
{
	enum
	{
		NUM_WORDS = (MAX_CLIENTS + 63) / 64,
	};

	uint64_t m_aBits[NUM_WORDS];

public:
	CClientMask() { Clear(); }

	void Clear()
	{
		for(int i = 0; i < NUM_WORDS; i++)
			m_aBits[i] = 0;
	}

	void Set(int ClientID)
	{
		m_aBits[ClientID / 64] |= (uint64_t) 1 << (ClientID % 64);
	}

	void Unset(int ClientID)
	{
		m_aBits[ClientID / 64] &= ~((uint64_t) 1 << (ClientID % 64));
	}

	bool IsSet(int ClientID) const
	{
		return (m_aBits[ClientID / 64] & ((uint64_t) 1 << (ClientID % 64))) != 0;
	}

	CClientMask &operator|=(const CClientMask &Other)
	{
		for(int i = 0; i < NUM_WORDS; i++)
			m_aBits[i] |= Other.m_aBits[i];
		return *this;
	}

	CClientMask operator^(const CClientMask &Other) const
	{
		CClientMask Result;
		for(int i = 0; i < NUM_WORDS; i++)
			Result.m_aBits[i] = m_aBits[i] ^ Other.m_aBits[i];
		return Result;
	}

	static CClientMask All()
	{
		CClientMask Result;
		for(int i = 0; i < NUM_WORDS; i++)
			Result.m_aBits[i] = ~(uint64_t) 0;
		return Result;
	}
};

#endif // GAME_SERVER_CLIENTMASK_H
