/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <gtest/gtest.h>

#include <base/system.h>
#include <engine/shared/compression.h>
#include <engine/shared/huffman.h>
#include <engine/shared/network.h>
#include <engine/shared/zstd_dict.h>

static const int DATA[] = {0, 1, -1, 32, 64, 256, -512, 12345, -123456, 1234567, 12345678, 123456789, 2147483647, (-2147483647 - 1)};
static const int NUM = sizeof(DATA) / sizeof(int);
static const int SIZES[NUM] = {1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5};

TEST(CVariableInt, RoundtripPackUnpack)
{
	for(int i = 0; i < NUM; i++)
	{
		unsigned char aPacked[CVariableInt::MAX_BYTES_PACKED];
		int Result;
		EXPECT_EQ(int(CVariableInt::Pack(aPacked, DATA[i], sizeof(aPacked)) - aPacked), SIZES[i]);
		EXPECT_EQ(int(CVariableInt::Unpack(aPacked, &Result, sizeof(aPacked)) - aPacked), SIZES[i]);
		EXPECT_EQ(Result, DATA[i]);
	}
}

TEST(CVariableInt, UnpackInvalid)
{
	unsigned char aPacked[CVariableInt::MAX_BYTES_PACKED];
	for(unsigned i = 0; i < sizeof(aPacked); i++)
		aPacked[i] = 0xFF;

	int Result;
	EXPECT_EQ(int(CVariableInt::Unpack(aPacked, &Result, sizeof(aPacked)) - aPacked), int(CVariableInt::MAX_BYTES_PACKED));
	EXPECT_EQ(Result, (-2147483647 - 1));

	aPacked[0] &= ~0x40; // unset sign bit

	EXPECT_EQ(int(CVariableInt::Unpack(aPacked, &Result, sizeof(aPacked)) - aPacked), int(CVariableInt::MAX_BYTES_PACKED));
	EXPECT_EQ(Result, 2147483647);
}

TEST(CVariableInt, PackBufferTooSmall)
{
	unsigned char aPacked[CVariableInt::MAX_BYTES_PACKED / 2]; // too small
	EXPECT_EQ(CVariableInt::Pack(aPacked, 2147483647, sizeof(aPacked)), (const unsigned char *) 0x0);
}

TEST(CVariableInt, UnpackBufferTooSmall)
{
	unsigned char aPacked[CVariableInt::MAX_BYTES_PACKED / 2];
	for(unsigned i = 0; i < sizeof(aPacked); i++)
		aPacked[i] = 0xFF; // extended bits are set, but buffer ends too early

	int UnusedResult;
	EXPECT_EQ(CVariableInt::Unpack(aPacked, &UnusedResult, sizeof(aPacked)), (const unsigned char *) 0x0);
}

TEST(CVariableInt, RoundtripCompressDecompress)
{
	unsigned char aCompressed[NUM * CVariableInt::MAX_BYTES_PACKED];
	int aDecompressed[NUM];
	long ExpectedCompressedSize = 0;
	for(int i = 0; i < NUM; i++)
		ExpectedCompressedSize += SIZES[i];

	long CompressedSize = CVariableInt::Compress(DATA, sizeof(DATA), aCompressed, sizeof(aCompressed));
	ASSERT_EQ(CompressedSize, ExpectedCompressedSize);
	long DecompressedSize = CVariableInt::Decompress(aCompressed, ExpectedCompressedSize, aDecompressed, sizeof(aDecompressed));
	ASSERT_EQ(DecompressedSize, sizeof(DATA));
	for(int i = 0; i < NUM; i++)
	{
		EXPECT_EQ(aDecompressed[i], DATA[i]);
	}
}

TEST(CVariableInt, CompressBufferTooSmall)
{
	unsigned char aCompressed[NUM]; // too small
	long CompressedSize = CVariableInt::Compress(DATA, sizeof(DATA), aCompressed, sizeof(aCompressed));
	ASSERT_EQ(CompressedSize, -1);
}

TEST(CVariableInt, DecompressBufferTooSmall)
{
	unsigned char aCompressed[] = {0x00, 0x01, 0x40, 0x20, 0x80, 0x01, 0x80, 0x04, 0xFF, 0x07, 0xB9, 0xC0, 0x01};
	int aUncompressed[4]; // too small
	long CompressedSize = CVariableInt::Decompress(aCompressed, sizeof(aCompressed), aUncompressed, sizeof(aUncompressed));
	ASSERT_EQ(CompressedSize, -1);
}

// Packet payload sizes that the network coders have to handle. NET_MAX_PAYLOAD
// is the largest one a packet can carry and 900 is MAX_SNAPSHOT_PACKSIZE.
static const int CHUNK_SIZES[] = {1, 2, 16, 64, 256, 512, 900, 1391};
static const int NUM_CHUNK_SIZES = sizeof(CHUNK_SIZES) / sizeof(int);

// The shapes that packet chunk data comes in.
enum
{
	SHAPE_ZEROS = 0, // unchanged snapshot fields, the common case
	SHAPE_SEQUENTIAL,
	SHAPE_TEXT, // names, chat, server info
	SHAPE_PATTERN, // repeated structure
	SHAPE_RANDOM, // incompressible, must fall back to an uncompressed packet
	NUM_SHAPES
};

static void FillChunk(unsigned char *pData, int Size, int Shape)
{
	switch(Shape)
	{
	case SHAPE_ZEROS:
		mem_zero(pData, Size);
		break;
	case SHAPE_SEQUENTIAL:
		for(int i = 0; i < Size; i++)
			pData[i] = (unsigned char) i;
		break;
	case SHAPE_TEXT:
	{
		static const char s_pText[] = "The quick brown fox jumps over the lazy dog. /flag /pause nameless tee joined the game.";
		for(int i = 0; i < Size; i++)
			pData[i] = s_pText[i % (sizeof(s_pText) - 1)];
		break;
	}
	case SHAPE_PATTERN:
	{
		static const unsigned char s_aPattern[] = {0x00, 0x00, 0x04, 0x80, 0x01, 0x00, 0x02, 0xff, 0x7f};
		for(int i = 0; i < Size; i++)
			pData[i] = s_aPattern[i % sizeof(s_aPattern)];
		break;
	}
	case SHAPE_RANDOM:
	{
		unsigned State = 0x12345678;
		for(int i = 0; i < Size; i++)
		{
			State ^= State << 13;
			State ^= State >> 17;
			State ^= State << 5;
			pData[i] = (unsigned char) State;
		}
		break;
	}
	}
}

TEST(CHuffman, Roundtrip)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[NET_MAX_PAYLOAD];
	unsigned char aCompressed[NET_MAX_PAYLOAD * 4]; // a bad frequency table can expand
	unsigned char aOutput[NET_MAX_PAYLOAD];

	for(int Shape = 0; Shape < NUM_SHAPES; Shape++)
	{
		for(int i = 0; i < NUM_CHUNK_SIZES; i++)
		{
			const int Size = CHUNK_SIZES[i];
			FillChunk(aInput, Size, Shape);

			const int CompressedSize = Huffman.Compress(aInput, Size, aCompressed, sizeof(aCompressed));
			ASSERT_GT(CompressedSize, 0) << "shape " << Shape << " size " << Size;
			ASSERT_EQ(Huffman.Decompress(aCompressed, CompressedSize, aOutput, sizeof(aOutput)), Size)
				<< "shape " << Shape << " size " << Size;
			ASSERT_EQ(mem_comp(aInput, aOutput, Size), 0) << "shape " << Shape << " size " << Size;
		}
	}
}

TEST(CHuffman, Empty)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[1] = {0};
	unsigned char aCompressed[16];
	unsigned char aOutput[1];

	const int CompressedSize = Huffman.Compress(aInput, 0, aCompressed, sizeof(aCompressed));
	ASSERT_GT(CompressedSize, 0);
	ASSERT_EQ(Huffman.Decompress(aCompressed, CompressedSize, aOutput, sizeof(aOutput)), 0);
}

TEST(CHuffman, CompressBufferTooSmall)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[256];
	unsigned char aCompressed[4]; // too small
	FillChunk(aInput, sizeof(aInput), SHAPE_ZEROS);
	ASSERT_EQ(Huffman.Compress(aInput, sizeof(aInput), aCompressed, sizeof(aCompressed)), -1);
}

TEST(CHuffman, DecompressBufferTooSmall)
{
	CHuffman Huffman;
	Huffman.Init();

	unsigned char aInput[256];
	unsigned char aCompressed[256];
	unsigned char aOutput[16]; // too small
	FillChunk(aInput, sizeof(aInput), SHAPE_ZEROS);

	const int CompressedSize = Huffman.Compress(aInput, sizeof(aInput), aCompressed, sizeof(aCompressed));
	ASSERT_GT(CompressedSize, 0);
	ASSERT_EQ(Huffman.Decompress(aCompressed, CompressedSize, aOutput, sizeof(aOutput)), -1);
}

TEST(CZstdDict, Roundtrip)
{
	CZstdDict Zstd;
	Zstd.Init();

	unsigned char aInput[NET_MAX_PAYLOAD];
	unsigned char aCompressed[NET_MAX_PAYLOAD * 2];
	unsigned char aOutput[NET_MAX_PAYLOAD];

	for(int Shape = 0; Shape < NUM_SHAPES; Shape++)
	{
		for(int i = 0; i < NUM_CHUNK_SIZES; i++)
		{
			const int Size = CHUNK_SIZES[i];
			FillChunk(aInput, Size, Shape);

			const int CompressedSize = Zstd.Compress(aInput, Size, aCompressed, sizeof(aCompressed));
			ASSERT_GT(CompressedSize, 0) << "shape " << Shape << " size " << Size;
			ASSERT_EQ(Zstd.Decompress(aCompressed, CompressedSize, aOutput, sizeof(aOutput)), Size)
				<< "shape " << Shape << " size " << Size;
			ASSERT_EQ(mem_comp(aInput, aOutput, Size), 0) << "shape " << Shape << " size " << Size;
		}
	}
}

TEST(CZstdDict, CompressBufferTooSmall)
{
	CZstdDict Zstd;
	Zstd.Init();

	unsigned char aInput[256];
	unsigned char aCompressed[4]; // too small
	FillChunk(aInput, sizeof(aInput), SHAPE_ZEROS);
	ASSERT_LT(Zstd.Compress(aInput, sizeof(aInput), aCompressed, sizeof(aCompressed)), 0);
}

TEST(CZstdDict, DecompressBufferTooSmall)
{
	CZstdDict Zstd;
	Zstd.Init();

	unsigned char aInput[256];
	unsigned char aCompressed[256];
	unsigned char aOutput[16]; // too small
	FillChunk(aInput, sizeof(aInput), SHAPE_ZEROS);

	const int CompressedSize = Zstd.Compress(aInput, sizeof(aInput), aCompressed, sizeof(aCompressed));
	ASSERT_GT(CompressedSize, 0);
	ASSERT_LT(Zstd.Decompress(aCompressed, CompressedSize, aOutput, sizeof(aOutput)), 0);
}
