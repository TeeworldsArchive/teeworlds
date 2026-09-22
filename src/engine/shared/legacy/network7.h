/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_LEGACY_NETWORK7_H
#define ENGINE_SHARED_LEGACY_NETWORK7_H

#include <base/system.h>

/*
	Frozen 0.7 connection handshake semantics.

	This is the authoritative record of how a 0.7 peer negotiates the
	connection and the packet payload codec. The 0.8 handshake adds a
	generation marker ({'T','W','8'}) and always uses zstd, so the live engine
	code will diverge; the 0.7 network translator keeps using exactly these
	rules.

	Wire compatibility contract with Teeworlds 0.7.4-0.7.6:
	  - NET_CTRLMSG_CONNECT carries a single optional capability byte behind
	    the 4 byte token; a peer that does not know it leaves it zero.
	  - NET_CTRLMSG_ACCEPT answers with the single capability byte the server
	    picked; zero (or a missing byte) means plain Huffman.
	  - The only defined capability bit is zstd-with-dictionary.

	Do not change anything in this namespace once it has shipped. New
	behaviour belongs to the 0.8 path, not here.
*/
// The 0.7 netversion is frozen here so the translator can recognise and talk to
// 0.7 servers/demos even after the live engine moved to 0.8.
#define LEGACY_NET7_GAME_VERSION "0.7.6"
#define LEGACY_NET7_NETVERSION_HASH "802f1be60a05665f"
#define LEGACY_NET7_NETVERSION "0.7 " LEGACY_NET7_NETVERSION_HASH

class CSnapshotDelta;

namespace legacy
{
	enum
	{
		// Packet payload codec as negotiated during the 0.7 handshake.
		NET7_COMPRESSION_HUFFMAN = 0,
		NET7_COMPRESSION_ZSTD = 1,

		// The only capability bit 0.7 knows about.
		NET7_CTRLFLAG_ZSTD_DICT = 1,

		// Where the capability byte sits. Chunk data starts with the control
		// byte, so the extended CONNECT token request buffer (which begins
		// with the 4 byte token) is shifted by one.
		NET7_CTRL_REQUEST_CAPABILITY_OFFSET = 4, // inside the token request buffer
		NET7_CTRL_CONNECT_CAPABILITY_OFFSET = NET7_CTRL_REQUEST_CAPABILITY_OFFSET + 1, // inside chunk data
		NET7_CTRL_ACCEPT_CAPABILITY_OFFSET = 1, // inside chunk data
	};

	// Build the capability byte a client advertises in NET_CTRLMSG_CONNECT.
	unsigned char Net7BuildClientCapabilities(bool SupportZstdDict);

	// Build the capability byte a server answers with in NET_CTRLMSG_ACCEPT.
	unsigned char Net7BuildAcceptCapabilities(bool UseZstdDict);

	// Server side: pick the codec from the capability byte the client sent.
	int Net7SelectCompression(unsigned char ClientCapabilities);

	// Read the capability byte out of a CONNECT chunk data buffer. Returns 0
	// (Huffman) when the peer did not send an extension byte at all.
	unsigned char Net7ReadConnectCapabilities(const unsigned char *pChunkData, int ChunkDataSize);

	// Read the capability byte out of an ACCEPT chunk data buffer. Returns 0
	// (Huffman) when the server did not send an extension byte at all.
	unsigned char Net7ReadAcceptCapabilities(const unsigned char *pChunkData, int ChunkDataSize);

	// Configure a snapshot delta codec with the frozen 0.7 static item sizes.
	// Needed everywhere 0.7 wire deltas are decoded (the network translator,
	// 0.7 demo playback); the live tables use the 0.8 sizes instead.
	void Net7ConfigureSnapshotDelta(CSnapshotDelta *pDelta);
} // namespace legacy

#endif // ENGINE_SHARED_LEGACY_NETWORK7_H
