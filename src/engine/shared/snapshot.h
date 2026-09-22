/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_SNAPSHOT_H
#define ENGINE_SHARED_SNAPSHOT_H

#include <base/system.h>
#include <base/tl/array.h>

// CSnapshot

class CSnapshotItem
{
	friend class CSnapshotBuilder;
	int m_TypeAndID;

	int *Data() { return (int *) (this + 1); }

public:
	const int *Data() const { return (int *) (this + 1); }
	int Type() const { return m_TypeAndID >> 16; }
	int ID() const { return m_TypeAndID & 0xffff; }
	int Key() const { return m_TypeAndID; }
	void SetKey(int Type, int ID) { m_TypeAndID = (Type << 16) | (ID & 0xffff); }
	void Invalidate() { m_TypeAndID = -1; }
};

class CSnapshot
{
	friend class CSnapshotBuilder;
	int m_DataSize;
	int m_NumItems;

	int *SortedKeys() const { return (int *) (this + 1); }
	int *Offsets() const { return (int *) (SortedKeys() + m_NumItems); }
	char *DataStart() const { return (char *) (Offsets() + m_NumItems); }

public:
	enum
	{
		OFFSET_UUID_TYPE = 0x4000,
		MAX_TYPE = 0x7fff,
		MAX_ID = 0xffff,
		MAX_PARTS = 2048
	};

	void Clear()
	{
		m_DataSize = 0;
		m_NumItems = 0;
	}
	int NumItems() const { return m_NumItems; }
	const CSnapshotItem *GetItem(int Index) const;
	int GetItemSize(int Index) const;
	int GetItemIndex(int Key) const;
	int GetItemType(int Index) const;
	void InvalidateItem(int Index);

	int Serialize(char *pDstData) const;

	int Crc() const;
	void DebugDump() const;
};

// CSnapshotDelta

class CSnapshotDelta
{
public:
	class CData
	{
	public:
		int m_NumDeletedItems;
		int m_NumUpdateItems;
		int m_NumTempItems; // needed?
		int m_aData[1];
	};

private:
	enum
	{
		MAX_NETOBJSIZES = 64
	};
	short m_aItemSizes[MAX_NETOBJSIZES];
	int m_aSnapshotDataRate[CSnapshot::MAX_TYPE + 1];
	int m_aSnapshotDataUpdates[CSnapshot::MAX_TYPE + 1];
	CData m_Empty;

public:
	CSnapshotDelta();
	int GetDataRate(int Index) const { return m_aSnapshotDataRate[Index]; }
	int GetDataUpdates(int Index) const { return m_aSnapshotDataUpdates[Index]; }
	void SetStaticsize(int ItemType, int Size);
	const CData *EmptyDelta() const;
	int CreateDelta(const class CSnapshot *pFrom, class CSnapshot *pTo, void *pDstData);
	int UnpackDelta(const class CSnapshot *pFrom, class CSnapshot *pTo, const void *pSrcData, int DataSize);

private:
	void EmitNewItem(const class CSnapshot *pTo, int Index, int **ppData);
	bool EmitDiffOrSkip(const class CSnapshot *pFrom, const class CSnapshot *pTo, int FromIndex, int ToIndex, int **ppData);
	bool IncludeItemSize(int Type) const;
};

// CSnapshotStorage

class CSnapshotStorage
{
public:
	class CHolder
	{
	public:
		CHolder *m_pPrev;
		CHolder *m_pNext;

		int64 m_Tagtime;
		int m_Tick;

		int m_SnapSize;
		CSnapshot *m_pSnap;
		CSnapshot *m_pAltSnap;
	};

	CHolder *m_pFirst;
	CHolder *m_pLast;

	~CSnapshotStorage();
	void Init();
	void PurgeAll();
	void PurgeUntil(int Tick);
	void Add(int Tick, int64 Tagtime, int DataSize, const void *pData, bool CreateAlt);
	int Get(int Tick, int64 *pTagtime, CSnapshot **ppData, CSnapshot **ppAltData) const;
};

class CSnapshotBuilder
{
	array<unsigned char> m_Data;
	array<int> m_aOffsets;
	array<int> m_aExtendedItemTypes;

	bool AddExtendedItemType(int Index);
	int GetExtendedItemTypeIndex(int TypeID);

public:
	CSnapshotBuilder();

	void Init();
	void Init(const CSnapshot *pSnapshot);
	bool UnserializeSnap(const char *pSrcData, int SrcSize);

	void *NewItem(int Type, int ID, int Size);

	CSnapshotItem *GetItem(int Index) const;
	int *GetItemData(int Key) const;

	int NumItems() const { return m_aOffsets.size(); }
	int DataSize() const { return m_Data.size(); }
	int RequiredSize() const;
	int Finish(void *pSnapdata);
};

#endif // ENGINE_SNAPSHOT_H
