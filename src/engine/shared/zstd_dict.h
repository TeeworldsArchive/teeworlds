#ifndef ENGINE_SHARED_ZSTD_DICT_H
#define ENGINE_SHARED_ZSTD_DICT_H

#include <zstd.h>

// Packet payload compression with zstd and a dictionary that both peers embed.
//
// This is only used when the connection handshake negotiated it (see the
// NET_CTRLFLAG_* exchange in CNetConnection); connections that did not agree
// keep using the legacy CHuffman coder.
//
// The dictionary makes this pay off on the small packets that dominate a game
// connection: unlike Huffman or plain deflate, zstd can emit a match into data
// that was never in the current packet. Like the Huffman frequency table it is
// a single build-time constant, so there is nothing to negotiate about its
// content. It costs 4 KiB of read-only memory per process and nothing per
// connection.
class CZstdDict
{
	ZSTD_CCtx *m_pCompressContext;
	ZSTD_DCtx *m_pDecompressContext;

	// Reads the embedded dictionary into the contexts. Doing this once instead
	// of per packet matters: re-digesting the dictionary every call made
	// decompression about five times slower in the benchmark.
	void LoadDictionary();

public:
	CZstdDict();
	~CZstdDict();

	void Init();

	// Returns the compressed size, or a negative value if the payload did not
	// fit or compression failed. The caller falls back to an uncompressed
	// packet in that case.
	int Compress(const void *pInput, int InputSize, void *pOutput, int OutputSize);
	int Decompress(const void *pInput, int InputSize, void *pOutput, int OutputSize);
};

#endif // ENGINE_SHARED_ZSTD_DICT_H
