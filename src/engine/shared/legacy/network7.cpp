/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "network7.h"

#include <engine/shared/snapshot.h>

#include <generated/protocol7.h>

namespace legacy
{
	unsigned char Net7BuildClientCapabilities(bool SupportZstdDict)
	{
		unsigned char Capabilities = 0;
		if(SupportZstdDict)
			Capabilities |= NET7_CTRLFLAG_ZSTD_DICT;
		return Capabilities;
	}

	unsigned char Net7BuildAcceptCapabilities(bool UseZstdDict)
	{
		return Net7BuildClientCapabilities(UseZstdDict);
	}

	int Net7SelectCompression(unsigned char ClientCapabilities)
	{
		if(ClientCapabilities & NET7_CTRLFLAG_ZSTD_DICT)
			return NET7_COMPRESSION_ZSTD;
		return NET7_COMPRESSION_HUFFMAN;
	}

	unsigned char Net7ReadConnectCapabilities(const unsigned char *pChunkData, int ChunkDataSize)
	{
		if(ChunkDataSize > NET7_CTRL_CONNECT_CAPABILITY_OFFSET)
			return pChunkData[NET7_CTRL_CONNECT_CAPABILITY_OFFSET];
		return 0;
	}

	unsigned char Net7ReadAcceptCapabilities(const unsigned char *pChunkData, int ChunkDataSize)
	{
		if(ChunkDataSize == NET7_CTRL_ACCEPT_CAPABILITY_OFFSET + 1)
			return pChunkData[NET7_CTRL_ACCEPT_CAPABILITY_OFFSET];
		return 0;
	}

	void Net7ConfigureSnapshotDelta(CSnapshotDelta *pDelta)
	{
		protocol7::CNetObjHandler Handler7;
		static const int OLD_NUM_NETOBJTYPES7 = 23;
		for(int i = 0; i < OLD_NUM_NETOBJTYPES7; i++)
			pDelta->SetStaticsize(i, Handler7.GetObjSize(i));
	}
} // namespace legacy
