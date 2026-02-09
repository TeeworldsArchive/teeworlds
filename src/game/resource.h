#ifndef GAME_RESOURCE_H
#define GAME_RESOURCE_H

#include <base/hash.h>
#include <base/uuid.h>

struct CResource
{
	enum
	{
		CHUNK_SIZE = 1200,
	};

    char m_aName[64];
    SHA256_DIGEST m_Sha256;
    unsigned m_Crc;
    unsigned char *m_pData;
    int m_DataSize;
    int m_Type;
    Uuid m_Uuid;
    
    bool operator<(const CResource &Other) const { return m_Uuid < Other.m_Uuid; }
    bool operator<=(const CResource &Other) const { return m_Uuid <= Other.m_Uuid; }
    bool operator==(const CResource &Other) const { return m_Uuid == Other.m_Uuid; }
};

#endif // GAME_RESOURCE_H