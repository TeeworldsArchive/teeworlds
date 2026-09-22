/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "test.h"

#include <gtest/gtest.h>

#include <engine/shared/datafile.h>
#include <engine/storage.h>

TEST(Datafile, RoundtripItemDataAndSize)
{
	CTestInfo Info;
	char aFilename[64];
	Info.Filename(aFilename, sizeof(aFilename), ".datafile");
	IStorage *pStorage = CreateTestStorage();
	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage, aFilename));

	static const char TEST_DATA[] = "Hello World!";
	int Index = Writer.AddData(sizeof(TEST_DATA), TEST_DATA);
	int Index4 = Writer.AddData(4, TEST_DATA);
	int aItem[2] = {Index, Index4};
	Writer.AddItem(12, 34, sizeof(aItem), aItem);
	EXPECT_TRUE(Writer.Finish());

	CDataFileReader Reader;
	EXPECT_FALSE(Reader.IsOpen());
	ASSERT_TRUE(Reader.Open(pStorage, aFilename, IStorage::TYPE_ALL));
	EXPECT_TRUE(Reader.IsOpen());

	ASSERT_EQ(Reader.GetDataSize(Index), sizeof(TEST_DATA));
	EXPECT_TRUE(mem_comp(Reader.GetData(Index), TEST_DATA, sizeof(TEST_DATA)) == 0);
	ASSERT_EQ(Reader.GetDataSize(Index4), 4);
	EXPECT_TRUE(mem_comp(Reader.GetData(Index), TEST_DATA, 4) == 0);

	static const char REPL_DATA[] = "Replacement";
	char *pReplace4 = (char *) mem_alloc(4);
	mem_copy(pReplace4, REPL_DATA, 4);
	char *pReplace = (char *) mem_alloc(sizeof(REPL_DATA));
	mem_copy(pReplace, REPL_DATA, sizeof(REPL_DATA));
	Reader.ReplaceData(Index, pReplace4, 4);
	Reader.ReplaceData(Index4, pReplace, sizeof(REPL_DATA));

	ASSERT_EQ(Reader.GetDataSize(Index), 4);
	EXPECT_TRUE(mem_comp(Reader.GetData(Index), REPL_DATA, 4) == 0);
	ASSERT_EQ(Reader.GetDataSize(Index4), sizeof(REPL_DATA));
	EXPECT_TRUE(mem_comp(Reader.GetData(Index4), REPL_DATA, sizeof(REPL_DATA)) == 0);

	bool FoundItem = false;
	for(int i = 0; i < Reader.NumItems(); i++)
	{
		int Type;
		int ID;
		void *pItem = Reader.GetItem(i, &Type, &ID);
		ASSERT_EQ(Reader.GetItemSize(i), sizeof(aItem));
		EXPECT_TRUE(mem_comp(pItem, aItem, sizeof(aItem)) == 0);
		EXPECT_EQ(Type, 12);
		EXPECT_EQ(ID, 34);

		FoundItem = true;
	}
	EXPECT_TRUE(FoundItem);

	EXPECT_TRUE(Reader.Close());

	EXPECT_TRUE(pStorage->RemoveFile(aFilename, IStorage::TYPE_SAVE));
}

static void TestDatafileRoundtrip(IStorage *pStorage, int Version)
{
	CTestInfo Info;
	char aFilename[64];
	Info.Filename(aFilename, sizeof(aFilename), ".datafile");

	// a compressible block and an incompressible one
	enum
	{
		REPEATED_SIZE = 16 * 1024,
		RANDOM_SIZE = 4096
	};
	char *pRepeated = (char *) mem_alloc(REPEATED_SIZE);
	for(int i = 0; i < REPEATED_SIZE; i++)
		pRepeated[i] = (char) (i % 7);
	char *pRandom = (char *) mem_alloc(RANDOM_SIZE);
	unsigned Seed = 0x12345678u;
	for(int i = 0; i < RANDOM_SIZE; i++)
	{
		Seed = Seed * 1664525u + 1013904223u;
		pRandom[i] = (char) (Seed >> 24);
	}

	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage, aFilename, Version));
	int RepeatedIndex = Writer.AddData(REPEATED_SIZE, pRepeated);
	int RandomIndex = Writer.AddData(RANDOM_SIZE, pRandom);
	int aItem[2] = {RepeatedIndex, RandomIndex};
	Writer.AddItem(12, 34, sizeof(aItem), aItem);
	EXPECT_TRUE(Writer.Finish());

	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(pStorage, aFilename, IStorage::TYPE_ALL));
	ASSERT_EQ(Reader.NumData(), 2);
	ASSERT_EQ(Reader.GetDataSize(RepeatedIndex), REPEATED_SIZE);
	EXPECT_EQ(mem_comp(Reader.GetData(RepeatedIndex), pRepeated, REPEATED_SIZE), 0);
	ASSERT_EQ(Reader.GetDataSize(RandomIndex), RANDOM_SIZE);
	EXPECT_EQ(mem_comp(Reader.GetData(RandomIndex), pRandom, RANDOM_SIZE), 0);

	ASSERT_EQ(Reader.NumItems(), 1);
	int Type, ID;
	void *pItem = Reader.GetItem(0, &Type, &ID);
	EXPECT_EQ(Type, 12);
	EXPECT_EQ(ID, 34);
	EXPECT_EQ(Reader.GetItemSize(0), (int) sizeof(aItem));
	EXPECT_EQ(mem_comp(pItem, aItem, sizeof(aItem)), 0);
	Reader.Close();

	mem_free(pRepeated);
	mem_free(pRandom);

	EXPECT_TRUE(pStorage->RemoveFile(aFilename, IStorage::TYPE_SAVE));
}

TEST(Datafile, RoundtripV4)
{
	TestDatafileRoundtrip(CreateTestStorage(), 4);
}

TEST(Datafile, RoundtripV5)
{
	TestDatafileRoundtrip(CreateTestStorage(), 5);
}

TEST(Datafile, RejectUnknownVersion)
{
	CTestInfo Info;
	char aFilename[64];
	Info.Filename(aFilename, sizeof(aFilename), ".datafile");
	IStorage *pStorage = CreateTestStorage();

	CDataFileWriter Writer;
	ASSERT_TRUE(Writer.Open(pStorage, aFilename, 5));
	int Index = Writer.AddData(4, "abcd");
	Writer.AddItem(1, 1, 4, &Index);
	ASSERT_TRUE(Writer.Finish());

	// corrupt the version field and write the file back
	IOHANDLE File = pStorage->OpenFile(aFilename, IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_TRUE(File);
	int FileSize = (int) io_length(File);
	char *pFileData = (char *) mem_alloc(FileSize);
	ASSERT_EQ(io_read(File, pFileData, FileSize), FileSize);
	io_close(File);
	ASSERT_GT(FileSize, 8);
	int Version = 6;
	mem_copy(pFileData + 4, &Version, sizeof(Version));
	File = pStorage->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	ASSERT_TRUE(File);
	ASSERT_EQ(io_write(File, pFileData, FileSize), FileSize);
	io_close(File);
	mem_free(pFileData);

	CDataFileReader Reader;
	EXPECT_FALSE(Reader.Open(pStorage, aFilename, IStorage::TYPE_ALL));
	EXPECT_FALSE(Reader.IsOpen());

	EXPECT_TRUE(pStorage->RemoveFile(aFilename, IStorage::TYPE_SAVE));
}
