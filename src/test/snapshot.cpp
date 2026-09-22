/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "test.h"

#include <gtest/gtest.h>

#include <base/system.h>
#include <engine/shared/snapshot.h>

static unsigned NextRand(unsigned &Seed)
{
	Seed = Seed * 1664525u + 1013904223u;
	return Seed;
}

// Builds a snapshot with ascending, unique keys and variable item sizes.
static CSnapshot *BuildSnapshot(int NumItems, unsigned Seed)
{
	CSnapshotBuilder Builder;
	Builder.Init();

	int Key = 0;
	for(int i = 0; i < NumItems; i++)
	{
		Key += 1 + (int) (NextRand(Seed) % 5);
		const int Type = (Key >> 16) & 0x3fff;
		const int ID = Key & 0xffff;
		// size depends on the key so that the same key always has the same size
		const int SizeWords = Key % 3 + 1;
		int aData[4];
		for(int k = 0; k < SizeWords; k++)
			aData[k] = (int) (NextRand(Seed) & 0xffff) - 0x8000;

		void *p = Builder.NewItem(Type, ID, SizeWords * 4);
		if(!p)
			return 0;
		mem_copy(p, aData, SizeWords * 4);
	}

	CSnapshot *pSnap = (CSnapshot *) mem_alloc(Builder.RequiredSize());
	Builder.Finish(pSnap);
	return pSnap;
}

static void ExpectSnapshotsEqual(const CSnapshot *pExpected, const CSnapshot *pActual)
{
	ASSERT_EQ(pExpected->NumItems(), pActual->NumItems());
	for(int i = 0; i < pExpected->NumItems(); i++)
	{
		ASSERT_EQ(pExpected->GetItem(i)->Key(), pActual->GetItem(i)->Key());
		ASSERT_EQ(pExpected->GetItemSize(i), pActual->GetItemSize(i));
		ASSERT_EQ(mem_comp(pExpected->GetItem(i)->Data(), pActual->GetItem(i)->Data(), pExpected->GetItemSize(i)), 0);
	}
}

static void TestDeltaRoundtrip(int NumFrom, int NumTo)
{
	SCOPED_TRACE(::testing::Message() << "from=" << NumFrom << " to=" << NumTo);
	CSnapshot *pFrom = BuildSnapshot(NumFrom, 0x1111u);
	CSnapshot *pTo = BuildSnapshot(NumTo, 0x9999u);
	ASSERT_TRUE(pFrom);
	ASSERT_TRUE(pTo);

	const int BufferSize = 4096 + (NumFrom + NumTo) * 64;
	char *pDeltaData = (char *) mem_alloc(BufferSize);

	CSnapshotDelta Delta;
	const int DeltaSize = Delta.CreateDelta(pFrom, pTo, pDeltaData);
	ASSERT_GE(DeltaSize, 0);

	if(DeltaSize > 0)
	{
		CSnapshot *pResult = (CSnapshot *) mem_alloc(BufferSize);
		const int Result = Delta.UnpackDelta(pFrom, pResult, pDeltaData, DeltaSize);
		ASSERT_GE(Result, 0);

		char *pSerializedTo = (char *) mem_alloc(BufferSize);
		char *pSerializedResult = (char *) mem_alloc(BufferSize);
		const int ToSize = pTo->Serialize(pSerializedTo);
		const int ResultSize = pResult->Serialize(pSerializedResult);
		EXPECT_EQ(ResultSize, ToSize);
		EXPECT_EQ(mem_comp(pSerializedResult, pSerializedTo, ToSize), 0);
		ExpectSnapshotsEqual(pTo, pResult);

		mem_free(pSerializedTo);
		mem_free(pSerializedResult);
		mem_free(pResult);
	}

	mem_free(pDeltaData);
	mem_free(pFrom);
	mem_free(pTo);
}

TEST(Snapshot, BuilderExceedsOldLimits)
{
	// > 1024 items and > 64 KiB of item data, both of which the old fixed builder rejected
	const int NumItems = 5000;
	CSnapshotBuilder Builder;
	Builder.Init();

	for(int i = 0; i < NumItems; i++)
	{
		const int aData[4] = {i, i * 2, i * 3, i * 4};
		void *p = Builder.NewItem(i % 60, i, sizeof(aData));
		ASSERT_TRUE(p);
		mem_copy(p, aData, sizeof(aData));
	}

	EXPECT_EQ(Builder.NumItems(), NumItems);
	EXPECT_GT(Builder.DataSize(), 64 * 1024);

	CSnapshot *pSnap = (CSnapshot *) mem_alloc(Builder.RequiredSize());
	const int Size = Builder.Finish(pSnap);
	EXPECT_GT(Size, 64 * 1024);
	EXPECT_EQ(pSnap->NumItems(), NumItems);

	// keys must be sorted and every item must be findable
	for(int i = 1; i < pSnap->NumItems(); i++)
		EXPECT_LT(pSnap->GetItem(i - 1)->Key(), pSnap->GetItem(i)->Key());
	for(int i = 0; i < NumItems; i++)
	{
		const int Key = ((i % 60) << 16) | i;
		EXPECT_GE(pSnap->GetItemIndex(Key), 0);
		const int *pData = pSnap->GetItem(pSnap->GetItemIndex(Key))->Data();
		EXPECT_EQ(pData[0], i);
		EXPECT_EQ(pData[3], i * 4);
	}

	mem_free(pSnap);
}

TEST(Snapshot, BuilderReuseKeepsCapacity)
{
	CSnapshotBuilder Builder;
	Builder.Init();
	void *p = Builder.NewItem(1, 2, 16);
	ASSERT_TRUE(p);
	Builder.Init();
	EXPECT_EQ(Builder.NumItems(), 0);
	EXPECT_EQ(Builder.DataSize(), 0);
	p = Builder.NewItem(3, 4, 16);
	ASSERT_TRUE(p);
	EXPECT_EQ(Builder.NumItems(), 1);
}

TEST(Snapshot, DeltaRoundtripSmall)
{
	TestDeltaRoundtrip(0, 0);
	TestDeltaRoundtrip(0, 1);
	TestDeltaRoundtrip(1, 0);
	TestDeltaRoundtrip(1, 1);
	TestDeltaRoundtrip(2, 2);
	TestDeltaRoundtrip(1024, 1025);
	TestDeltaRoundtrip(1025, 1024);
}

TEST(Snapshot, DeltaRoundtripLarge)
{
	// > 16K items used to overflow the delta hash buckets and silently drop items
	TestDeltaRoundtrip(4096, 4096);
	TestDeltaRoundtrip(16384, 16384);
	TestDeltaRoundtrip(65535, 65535);
}

TEST(Snapshot, DeltaIdenticalSnapshots)
{
	CSnapshot *pFrom = BuildSnapshot(64, 0x1234u);
	CSnapshot *pTo = BuildSnapshot(64, 0x1234u);
	ASSERT_TRUE(pFrom);
	ASSERT_TRUE(pTo);

	char aDelta[4096];
	CSnapshotDelta Delta;
	EXPECT_EQ(Delta.CreateDelta(pFrom, pTo, aDelta), 0);

	mem_free(pFrom);
	mem_free(pTo);
}

TEST(Snapshot, DeltaSingleValueChange)
{
	CSnapshot *pFrom = BuildSnapshot(16, 0x5555u);
	CSnapshot *pTo = BuildSnapshot(16, 0x5555u);
	ASSERT_TRUE(pFrom);
	ASSERT_TRUE(pTo);

	// change one int in the first item
	const int Index = pFrom->GetItemIndex(pTo->GetItem(0)->Key());
	ASSERT_GE(Index, 0);
	const int *pToData = pTo->GetItem(0)->Data();
	((int *) pToData)[0] += 1;

	char aDelta[4096];
	CSnapshotDelta Delta;
	const int DeltaSize = Delta.CreateDelta(pFrom, pTo, aDelta);
	ASSERT_GT(DeltaSize, 0);

	CSnapshot *pResult = (CSnapshot *) mem_alloc(4096);
	ASSERT_GE(Delta.UnpackDelta(pFrom, pResult, aDelta, DeltaSize), 0);
	ExpectSnapshotsEqual(pTo, pResult);

	mem_free(pResult);
	mem_free(pFrom);
	mem_free(pTo);
}

TEST(Snapshot, DeltaRejectsCorruptData)
{
	CSnapshot *pFrom = BuildSnapshot(0, 0x1u);
	CSnapshot *pTo = BuildSnapshot(1, 0x2u);
	ASSERT_TRUE(pFrom);
	ASSERT_TRUE(pTo);

	char aDelta[4096];
	CSnapshotDelta Delta;
	const int DeltaSize = Delta.CreateDelta(pFrom, pTo, aDelta);
	ASSERT_GT(DeltaSize, 0);

	CSnapshot *pResult = (CSnapshot *) mem_alloc(4096);

	// truncated
	EXPECT_LT(Delta.UnpackDelta(pFrom, pResult, aDelta, 4), 0);
	EXPECT_LT(Delta.UnpackDelta(pFrom, pResult, aDelta, 8), 0);

	// negative deleted count
	char aCopy[4096];
	mem_copy(aCopy, aDelta, DeltaSize);
	const int Negative = -1;
	mem_copy(aCopy, &Negative, 4);
	EXPECT_EQ(Delta.UnpackDelta(pFrom, pResult, aCopy, DeltaSize), -1);

	// out of range type
	mem_copy(aCopy, aDelta, DeltaSize);
	const int BadType = 0x8000;
	mem_copy(aCopy + 12, &BadType, 4);
	EXPECT_EQ(Delta.UnpackDelta(pFrom, pResult, aCopy, DeltaSize), -4);

	// out of range id
	mem_copy(aCopy, aDelta, DeltaSize);
	const int BadID = 0x10000;
	mem_copy(aCopy + 16, &BadID, 4);
	EXPECT_EQ(Delta.UnpackDelta(pFrom, pResult, aCopy, DeltaSize), -5);

	mem_free(pResult);
	mem_free(pFrom);
	mem_free(pTo);
}

TEST(Snapshot, DeltaApplyUnsortedLists)
{
	// The delta lists are not guaranteed to be ascending on the wire: real 0.7
	// captures contain unsorted delete lists (e.g. "4/15 4/17 4/3") and unsorted
	// update lists (e.g. "10/0 3/2 21/0"). Decoding must be order-independent.
	CSnapshotBuilder Builder;
	Builder.Init();
	for(int ID = 0; ID < 5; ID++)
	{
		const int aData[1] = {ID};
		void *p = Builder.NewItem(1, ID, sizeof(aData));
		ASSERT_TRUE(p);
		mem_copy(p, aData, sizeof(aData));
	}
	CSnapshot *pFrom = (CSnapshot *) mem_alloc(Builder.RequiredSize());
	Builder.Finish(pFrom);
	ASSERT_EQ(pFrom->NumItems(), 5);

	// deletes: 1/3 before 1/1 (unsorted)
	// updates: 1/4, 2/0 (new item), 1/0 (unsorted)
	int aDelta[64];
	int *p = aDelta;
	*p++ = 2; // num deleted
	*p++ = 3; // num updates
	*p++ = 0; // num temp
	*p++ = (1 << 16) | 3;
	*p++ = (1 << 16) | 1;
	*p++ = 1; // update 1/4: diff +40
	*p++ = 4;
	*p++ = 1;
	*p++ = 40;
	*p++ = 2; // add 2/0: absolute 7
	*p++ = 0;
	*p++ = 1;
	*p++ = 7;
	*p++ = 1; // update 1/0: diff +10
	*p++ = 0;
	*p++ = 1;
	*p++ = 10;
	const int DeltaSize = (int) ((char *) p - (char *) aDelta);

	CSnapshotDelta Delta;
	CSnapshot *pResult = (CSnapshot *) mem_alloc(64 * 1024);
	ASSERT_GE(Delta.UnpackDelta(pFrom, pResult, aDelta, DeltaSize), 0);

	// 1/1 and 1/3 deleted, 1/2 unchanged, 1/0 and 1/4 diffed, 2/0 added
	const int aExpectedKeys[] = {(1 << 16) | 0, (1 << 16) | 2, (1 << 16) | 4, (2 << 16) | 0};
	const int aExpectedValues[] = {10, 2, 44, 7};
	ASSERT_EQ(pResult->NumItems(), 4);
	for(int i = 0; i < 4; i++)
	{
		ASSERT_EQ(pResult->GetItem(i)->Key(), aExpectedKeys[i]) << "item " << i;
		ASSERT_EQ(pResult->GetItem(i)->Data()[0], aExpectedValues[i]) << "item " << i;
	}

	mem_free(pResult);
	mem_free(pFrom);
}

TEST(Snapshot, SerializeUnserialize)
{
	CSnapshot *pSnap = BuildSnapshot(512, 0xabcdefu);
	ASSERT_TRUE(pSnap);

	char aSerialized[64 * 1024];
	const int Size = pSnap->Serialize(aSerialized);
	ASSERT_GT(Size, 0);

	CSnapshotBuilder Builder;
	ASSERT_TRUE(Builder.UnserializeSnap(aSerialized, Size));
	EXPECT_EQ(Builder.NumItems(), pSnap->NumItems());

	CSnapshot *pRebuilt = (CSnapshot *) mem_alloc(Builder.RequiredSize());
	Builder.Finish(pRebuilt);
	ExpectSnapshotsEqual(pSnap, pRebuilt);

	mem_free(pRebuilt);
	mem_free(pSnap);
}

TEST(Snapshot, UnserializeRejectsInvalidData)
{
	CSnapshotBuilder Builder;

	// offset out of range: DataSize=8, NumItems=1, offset=100
	int aBadOffset[4] = {8, 1, 100, 0};
	EXPECT_FALSE(Builder.UnserializeSnap((char *) aBadOffset, sizeof(aBadOffset)));

	// CompleteSize does not match SrcSize
	int aBadSize[6] = {8, 1, 0, 0, 0, 0};
	EXPECT_FALSE(Builder.UnserializeSnap((char *) aBadSize, 24));

	// negative counts
	int aNegative[2] = {-1, 0};
	EXPECT_FALSE(Builder.UnserializeSnap((char *) aNegative, sizeof(aNegative)));
}

// Reference implementation of the original delta writer: deletes in from-order
// first, then updates in to-order, always carrying the item size. Used to prove
// that the new merge-based writer keeps the wire format byte-identical.
static bool ReferenceKeyInSnapshot(const CSnapshot *pSnap, int Key)
{
	for(int i = 0; i < pSnap->NumItems(); i++)
		if(pSnap->GetItem(i)->Key() == Key)
			return true;
	return false;
}

static int ReferenceFindItem(const CSnapshot *pSnap, int Key)
{
	for(int i = 0; i < pSnap->NumItems(); i++)
		if(pSnap->GetItem(i)->Key() == Key)
			return i;
	return -1;
}

static int ReferenceCreateDelta(const CSnapshot *pFrom, const CSnapshot *pTo, char *pDst)
{
	int *pData = (int *) pDst;
	int *pHeader = pData;
	pData += 3;
	int NumDeleted = 0;
	int NumUpdate = 0;

	for(int i = 0; i < pFrom->NumItems(); i++)
	{
		if(!ReferenceKeyInSnapshot(pTo, pFrom->GetItem(i)->Key()))
		{
			*pData++ = pFrom->GetItem(i)->Key();
			NumDeleted++;
		}
	}

	for(int j = 0; j < pTo->NumItems(); j++)
	{
		const CSnapshotItem *pCur = pTo->GetItem(j);
		const int Size = pTo->GetItemSize(j);
		const int FromIndex = ReferenceFindItem(pFrom, pCur->Key());
		if(FromIndex != -1)
		{
			int *pDiff = pData + 3;
			int Needed = 0;
			const int *pPast = pFrom->GetItem(FromIndex)->Data();
			const int *pNow = pCur->Data();
			for(int k = 0; k < Size / 4; k++)
			{
				pDiff[k] = pNow[k] - pPast[k];
				Needed |= pDiff[k];
			}
			if(Needed)
			{
				*pData++ = pCur->Type();
				*pData++ = pCur->ID();
				*pData++ = Size / 4;
				pData += Size / 4;
				NumUpdate++;
			}
		}
		else
		{
			*pData++ = pCur->Type();
			*pData++ = pCur->ID();
			*pData++ = Size / 4;
			mem_copy(pData, pCur->Data(), Size);
			pData += Size / 4;
			NumUpdate++;
		}
	}

	if(!NumDeleted && !NumUpdate)
		return 0;

	pHeader[0] = NumDeleted;
	pHeader[1] = NumUpdate;
	pHeader[2] = 0;
	return (int) ((char *) pData - pDst);
}

TEST(Snapshot, DeltaMatchesReferenceWireFormat)
{
	const int aItemCounts[] = {0, 1, 2, 7, 64, 1024, 1025, 4096};
	for(size_t a = 0; a < sizeof(aItemCounts) / sizeof(aItemCounts[0]); a++)
	{
		for(size_t b = 0; b < sizeof(aItemCounts) / sizeof(aItemCounts[0]); b++)
		{
			SCOPED_TRACE(::testing::Message() << "from=" << aItemCounts[a] << " to=" << aItemCounts[b]);
			CSnapshot *pFrom = BuildSnapshot(aItemCounts[a], 0x2222u);
			CSnapshot *pTo = BuildSnapshot(aItemCounts[b], 0x7777u);
			ASSERT_TRUE(pFrom);
			ASSERT_TRUE(pTo);

			const int BufferSize = 4096 + (aItemCounts[a] + aItemCounts[b]) * 64;
			char *pNew = (char *) mem_alloc(BufferSize);
			char *pRef = (char *) mem_alloc(BufferSize);

			CSnapshotDelta Delta;
			const int NewSize = Delta.CreateDelta(pFrom, pTo, pNew);
			const int RefSize = ReferenceCreateDelta(pFrom, pTo, pRef);
			EXPECT_EQ(NewSize, RefSize);
			if(NewSize == RefSize && NewSize > 0)
			{
				EXPECT_EQ(mem_comp(pNew, pRef, NewSize), 0);
			}

			mem_free(pNew);
			mem_free(pRef);
			mem_free(pFrom);
			mem_free(pTo);
		}
	}
}
