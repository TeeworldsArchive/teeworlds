/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <algorithm>
#include <limits.h>

#include <base/tl/algorithm.h>

#include "compression.h"
#include "snapshot.h"
#include "uuid_manager.h"

// CSnapshot

const CSnapshotItem *CSnapshot::GetItem(int Index) const
{
	return (const CSnapshotItem *) (DataStart() + Offsets()[Index]);
}

int CSnapshot::GetItemSize(int Index) const
{
	if(Index == m_NumItems - 1)
		return (m_DataSize - Offsets()[Index]) - sizeof(CSnapshotItem);
	return (Offsets()[Index + 1] - Offsets()[Index]) - sizeof(CSnapshotItem);
}

int CSnapshot::GetItemIndex(int Key) const
{
	plain_range_sorted<int> Keys(SortedKeys(), SortedKeys() + m_NumItems);
	plain_range_sorted<int> r = ::find_binary(Keys, Key);

	if(r.empty())
		return -1;

	int Index = &r.front() - SortedKeys();
	if(GetItem(Index)->Key() != Key)
		return -1; // deleted
	return Index;
}

int CSnapshot::GetItemType(int Index) const
{
	int InternalType = GetItem(Index)->Type();
	if(InternalType < OFFSET_UUID_TYPE)
	{
		return InternalType;
	}
	int TypeItemIndex = GetItemIndex(InternalType); // NETOBJTYPE_EX
	if(TypeItemIndex == -1 || GetItemSize(TypeItemIndex) < (int) sizeof(Uuid))
	{
		return InternalType;
	}
	const CSnapshotItem *pTypeItem = GetItem(TypeItemIndex);
	Uuid ItemUuid;
	for(size_t i = 0; i < sizeof(Uuid) / sizeof(unsigned int); i++)
		uint_to_bytes_be(&ItemUuid.m_aData[i * sizeof(unsigned int)], pTypeItem->Data()[i]);
	return g_UuidManager.LookupUuid(ItemUuid);
}

void CSnapshot::InvalidateItem(int Index)
{
	((CSnapshotItem *) (DataStart() + Offsets()[Index]))->Invalidate();
}

int CSnapshot::Serialize(char *pDstData) const
{
	int *pData = (int *) pDstData;
	pData[0] = m_DataSize;
	pData[1] = m_NumItems;

	mem_copy(pData + 2, Offsets(), sizeof(int) * m_NumItems);
	mem_copy(pData + 2 + m_NumItems, DataStart(), m_DataSize);

	return sizeof(int) * (2 + m_NumItems) + m_DataSize;
}

int CSnapshot::Crc() const
{
	unsigned int Crc = 0;

	for(int i = 0; i < m_NumItems; i++)
	{
		const CSnapshotItem *pItem = GetItem(i);
		int Size = GetItemSize(i);

		for(int b = 0; b < Size / 4; b++)
			Crc += pItem->Data()[b];
	}
	return (int) Crc;
}

void CSnapshot::DebugDump() const
{
	dbg_msg("snapshot", "data_size=%d num_items=%d", m_DataSize, m_NumItems);
	for(int i = 0; i < m_NumItems; i++)
	{
		const CSnapshotItem *pItem = GetItem(i);
		int Size = GetItemSize(i);
		dbg_msg("snapshot", "\ttype=%d id=%d", pItem->Type(), pItem->ID());
		for(int b = 0; b < Size / 4; b++)
			dbg_msg("snapshot", "\t\t%3d %12d\t%08x", b, pItem->Data()[b], pItem->Data()[b]);
	}
}

// CSnapshotDelta

static int DiffItem(const int *pPast, const int *pCurrent, int *pOut, int Size)
{
	int Needed = 0;
	while(Size)
	{
		*pOut = *pCurrent - *pPast;
		Needed |= *pOut;
		pOut++;
		pPast++;
		pCurrent++;
		Size--;
	}

	return Needed;
}

static void UndiffItem(const int *pPast, const int *pDiff, int *pOut, int Size, int *pDataRate)
{
	while(Size)
	{
		*pOut = *pPast + *pDiff;

		if(*pDiff == 0)
			*pDataRate += 1;
		else
		{
			unsigned char aBuf[CVariableInt::MAX_BYTES_PACKED];
			unsigned char *pEnd = CVariableInt::Pack(aBuf, *pDiff, sizeof(aBuf));
			*pDataRate += (int) (pEnd - (unsigned char *) aBuf) * 8;
		}

		pOut++;
		pPast++;
		pDiff++;
		Size--;
	}
}

CSnapshotDelta::CSnapshotDelta()
{
	mem_zero(m_aItemSizes, sizeof(m_aItemSizes));
	mem_zero(m_aSnapshotDataRate, sizeof(m_aSnapshotDataRate));
	mem_zero(m_aSnapshotDataUpdates, sizeof(m_aSnapshotDataUpdates));
	mem_zero(&m_Empty, sizeof(m_Empty));
}

void CSnapshotDelta::SetStaticsize(int ItemType, int Size)
{
	if(ItemType < 0 || ItemType >= MAX_NETOBJSIZES)
		return;
	m_aItemSizes[ItemType] = Size;
}

const CSnapshotDelta::CData *CSnapshotDelta::EmptyDelta() const
{
	return &m_Empty;
}

bool CSnapshotDelta::IncludeItemSize(int Type) const
{
	return Type >= MAX_NETOBJSIZES || !m_aItemSizes[Type];
}

void CSnapshotDelta::EmitNewItem(const CSnapshot *pTo, int Index, int **ppData)
{
	const CSnapshotItem *pCurItem = pTo->GetItem(Index);
	const int ItemSize = pTo->GetItemSize(Index);
	const bool IncludeSize = IncludeItemSize(pCurItem->Type());

	int *pData = *ppData;
	*pData++ = pCurItem->Type();
	*pData++ = pCurItem->ID();
	if(IncludeSize)
		*pData++ = ItemSize / 4;

	mem_copy(pData, pCurItem->Data(), ItemSize);
	pData += ItemSize / 4;
	*ppData = pData;
}

bool CSnapshotDelta::EmitDiffOrSkip(const CSnapshot *pFrom, const CSnapshot *pTo, int FromIndex, int ToIndex, int **ppData)
{
	const CSnapshotItem *pCurItem = pTo->GetItem(ToIndex);
	const CSnapshotItem *pPastItem = pFrom->GetItem(FromIndex);
	const int ItemSize = pTo->GetItemSize(ToIndex);
	const bool IncludeSize = IncludeItemSize(pCurItem->Type());

	int *pData = *ppData;
	int *pItemDataDst = pData + (IncludeSize ? 3 : 2);

	if(DiffItem(pPastItem->Data(), (int *) pCurItem->Data(), pItemDataDst, ItemSize / 4))
	{
		*pData++ = pCurItem->Type();
		*pData++ = pCurItem->ID();
		if(IncludeSize)
			*pData++ = ItemSize / 4;
		pData += ItemSize / 4;
		*ppData = pData;
		return true;
	}
	return false;
}

// The delta wire format is unchanged: all deleted keys first (ascending), then
// all update entries (ascending). Both snapshots are key-sorted, so the delta can
// be produced by a single linear merge instead of hash lookups.
int CSnapshotDelta::CreateDelta(const CSnapshot *pFrom, CSnapshot *pTo, void *pDstData)
{
	CData *pDelta = (CData *) pDstData;
	int *pData = (int *) pDelta->m_aData;

	pDelta->m_NumDeletedItems = 0;
	pDelta->m_NumUpdateItems = 0;
	pDelta->m_NumTempItems = 0;

	const int NumFrom = pFrom->NumItems();
	const int NumTo = pTo->NumItems();

	// pass 1: all deleted keys, ascending
	{
		int i = 0;
		int j = 0;
		while(i < NumFrom && j < NumTo)
		{
			const int FromKey = pFrom->GetItem(i)->Key();
			const int ToKey = pTo->GetItem(j)->Key();
			if(FromKey < ToKey)
			{
				*pData++ = FromKey;
				pDelta->m_NumDeletedItems++;
				i++;
			}
			else if(FromKey > ToKey)
				j++;
			else
			{
				i++;
				j++;
			}
		}
		while(i < NumFrom)
		{
			*pData++ = pFrom->GetItem(i)->Key();
			pDelta->m_NumDeletedItems++;
			i++;
		}
	}

	// pass 2: all update entries, ascending
	{
		int i = 0;
		int j = 0;
		while(i < NumFrom && j < NumTo)
		{
			const int FromKey = pFrom->GetItem(i)->Key();
			const int ToKey = pTo->GetItem(j)->Key();
			if(FromKey < ToKey)
				i++;
			else if(FromKey > ToKey)
			{
				EmitNewItem(pTo, j, &pData);
				pDelta->m_NumUpdateItems++;
				j++;
			}
			else
			{
				if(EmitDiffOrSkip(pFrom, pTo, i, j, &pData))
					pDelta->m_NumUpdateItems++;
				i++;
				j++;
			}
		}
		while(j < NumTo)
		{
			EmitNewItem(pTo, j, &pData);
			pDelta->m_NumUpdateItems++;
			j++;
		}
	}

	if(!pDelta->m_NumDeletedItems && !pDelta->m_NumUpdateItems && !pDelta->m_NumTempItems)
		return 0;

	return (int) ((char *) pData - (char *) pDstData);
}

static int RangeCheck(const void *pEnd, const void *pPtr, int Size)
{
	if((const char *) pPtr + Size > (const char *) pEnd)
		return -1;
	return 0;
}

// Neither the delete list nor the update list is guaranteed to be ascending on
// the wire: real 0.7 captures contain both unsorted delete lists (e.g.
// "4/15 4/17 4/3") and unsorted update lists (e.g. "10/0 3/2 21/0"). The
// original decoder looked every key up individually and was therefore
// order-independent; the linear merge here must sort both lists first to keep
// the same result, otherwise updates create duplicate items and deletes are
// skipped.
static bool KeyIsDeleted(const int *pSortedDeleted, int NumDeleted, int Key)
{
	int Lo = 0;
	int Hi = NumDeleted - 1;
	while(Lo <= Hi)
	{
		const int Mid = (Lo + Hi) / 2;
		if(pSortedDeleted[Mid] == Key)
			return true;
		if(pSortedDeleted[Mid] < Key)
			Lo = Mid + 1;
		else
			Hi = Mid - 1;
	}
	return false;
}

struct CDeltaUpdate
{
	int m_Key;
	int m_Type;
	int m_ID;
	int m_ItemSize;
	const int *m_pData;
};

struct CDeltaUpdateKeyLess
{
	bool operator()(const CDeltaUpdate &a, const CDeltaUpdate &b) const { return a.m_Key < b.m_Key; }
};

int CSnapshotDelta::UnpackDelta(const CSnapshot *pFrom, CSnapshot *pTo, const void *pSrcData, int DataSize)
{
	CSnapshotBuilder Builder;
	const CData *pDelta = (const CData *) pSrcData;
	const int *pData = (const int *) pDelta->m_aData;
	const int *pEnd = (const int *) (((const char *) pSrcData + DataSize));

	if(DataSize < (int) sizeof(int) * 3)
		return -1;
	if(pDelta->m_NumDeletedItems < 0 || pDelta->m_NumUpdateItems < 0 || pDelta->m_NumTempItems < 0)
		return -1;

	const int *pDeleted = pData;
	if(pDelta->m_NumDeletedItems > (int) (pEnd - pData))
		return -2;
	pData += pDelta->m_NumDeletedItems;

	array<int> aDeletedSorted;
	if(pDelta->m_NumDeletedItems > 0)
	{
		aDeletedSorted.set_size(pDelta->m_NumDeletedItems);
		mem_copy(aDeletedSorted.base_ptr(), pDeleted, sizeof(int) * pDelta->m_NumDeletedItems);
		sort(aDeletedSorted.all());
	}
	const int *pDeletedSorted = aDeletedSorted.base_ptr();
	const int NumDeleted = aDeletedSorted.size();

	// Parse every update entry first so they can be processed in key order.
	array<CDeltaUpdate> aUpdates;
	aUpdates.set_size(pDelta->m_NumUpdateItems);
	for(int u = 0; u < pDelta->m_NumUpdateItems; u++)
	{
		if(pData + 2 > pEnd)
			return -3;

		const int Type = *pData++;
		if(Type < 0 || Type > CSnapshot::MAX_TYPE)
			return -4;

		const int ID = *pData++;
		if(ID < 0 || ID > CSnapshot::MAX_ID)
			return -5;

		int ItemSize;
		if(Type < MAX_NETOBJSIZES && m_aItemSizes[Type])
			ItemSize = m_aItemSizes[Type];
		else
		{
			if(pData + 1 > pEnd)
				return -6;
			if(*pData < 0 || *pData > INT_MAX / 4)
				return -7;
			ItemSize = (*pData++) * 4;
		}

		if(RangeCheck(pEnd, pData, ItemSize) || ItemSize < 0)
			return -8;

		CDeltaUpdate &Update = aUpdates[u];
		Update.m_Key = (Type << 16) | (ID & 0xffff);
		Update.m_Type = Type;
		Update.m_ID = ID;
		Update.m_ItemSize = ItemSize;
		Update.m_pData = pData;
		pData += ItemSize / 4;
	}
	if(aUpdates.size() > 0)
		sort(aUpdates.all(), CDeltaUpdateKeyLess());

	Builder.Init();

	const int NumFrom = pFrom->NumItems();
	int FromIndex = 0;

	for(int u = 0; u < aUpdates.size();)
	{
		const int Key = aUpdates[u].m_Key;

		// emit all non-deleted source items that sort before this update
		while(FromIndex < NumFrom)
		{
			const int FromKey = pFrom->GetItem(FromIndex)->Key();
			if(FromKey >= Key)
				break;
			if(!KeyIsDeleted(pDeletedSorted, NumDeleted, FromKey))
			{
				const CSnapshotItem *pFromItem = pFrom->GetItem(FromIndex);
				const int FromSize = pFrom->GetItemSize(FromIndex);
				void *pNewItem = Builder.NewItem(pFromItem->Type(), pFromItem->ID(), FromSize);
				if(!pNewItem)
					return -9;
				mem_copy(pNewItem, pFromItem->Data(), FromSize);
			}
			FromIndex++;
		}

		// the original decoder diffed against the first source item with this
		// key and let every repeated update reuse the same builder item
		const bool HasPrevious = FromIndex < NumFrom && pFrom->GetItem(FromIndex)->Key() == Key;
		const int *pPreviousData = HasPrevious ? pFrom->GetItem(FromIndex)->Data() : 0;

		int *pNewData = 0;
		for(; u < aUpdates.size() && aUpdates[u].m_Key == Key; u++)
		{
			const CDeltaUpdate &Update = aUpdates[u];
			if(!pNewData)
			{
				pNewData = (int *) Builder.NewItem(Update.m_Type, Update.m_ID, Update.m_ItemSize);
				if(!pNewData)
					return -9;
			}

			if(HasPrevious)
			{
				// we got an update so we need to apply the diff
				UndiffItem(pPreviousData, Update.m_pData, pNewData, Update.m_ItemSize / 4, &m_aSnapshotDataRate[Update.m_Type]);
			}
			else // no previous, just copy the pData
			{
				mem_copy(pNewData, Update.m_pData, Update.m_ItemSize);
				m_aSnapshotDataRate[Update.m_Type] += Update.m_ItemSize * 8;
			}
			m_aSnapshotDataUpdates[Update.m_Type]++;
		}
		if(HasPrevious)
			FromIndex++;
	}

	// emit the remaining non-deleted source items
	while(FromIndex < NumFrom)
	{
		const int FromKey = pFrom->GetItem(FromIndex)->Key();
		if(!KeyIsDeleted(pDeletedSorted, NumDeleted, FromKey))
		{
			const CSnapshotItem *pFromItem = pFrom->GetItem(FromIndex);
			const int FromSize = pFrom->GetItemSize(FromIndex);
			void *pNewItem = Builder.NewItem(pFromItem->Type(), pFromItem->ID(), FromSize);
			if(!pNewItem)
				return -9;
			mem_copy(pNewItem, pFromItem->Data(), FromSize);
		}
		FromIndex++;
	}

	// finish up
	return Builder.Finish(pTo);
}

// CSnapshotStorage

CSnapshotStorage::~CSnapshotStorage()
{
	PurgeAll();
}

void CSnapshotStorage::Init()
{
	m_pFirst = 0;
	m_pLast = 0;
}

void CSnapshotStorage::PurgeAll()
{
	CHolder *pHolder = m_pFirst;

	while(pHolder)
	{
		CHolder *pNext = pHolder->m_pNext;
		mem_free(pHolder);
		pHolder = pNext;
	}

	// no more snapshots in storage
	m_pFirst = 0;
	m_pLast = 0;
}

void CSnapshotStorage::PurgeUntil(int Tick)
{
	CHolder *pHolder = m_pFirst;

	while(pHolder)
	{
		CHolder *pNext = pHolder->m_pNext;
		if(pHolder->m_Tick >= Tick)
			return; // no more to remove
		mem_free(pHolder);

		// did we come to the end of the list?
		if(!pNext)
			break;

		m_pFirst = pNext;
		pNext->m_pPrev = 0x0;

		pHolder = pNext;
	}

	// no more snapshots in storage
	m_pFirst = 0;
	m_pLast = 0;
}

void CSnapshotStorage::Add(int Tick, int64 Tagtime, int DataSize, const void *pData, bool CreateAlt)
{
	// allocate memory for holder + snapshot_data
	int TotalSize = sizeof(CHolder) + DataSize;

	if(CreateAlt)
		TotalSize += DataSize;

	CHolder *pHolder = (CHolder *) mem_alloc(TotalSize);

	// set data
	pHolder->m_Tick = Tick;
	pHolder->m_Tagtime = Tagtime;
	pHolder->m_SnapSize = DataSize;
	pHolder->m_pSnap = (CSnapshot *) (pHolder + 1);
	mem_copy(pHolder->m_pSnap, pData, DataSize);

	if(CreateAlt) // create alternative if wanted
	{
		pHolder->m_pAltSnap = (CSnapshot *) (((char *) pHolder->m_pSnap) + DataSize);
		mem_copy(pHolder->m_pAltSnap, pData, DataSize);
	}
	else
		pHolder->m_pAltSnap = 0;

	// link
	pHolder->m_pNext = 0;
	pHolder->m_pPrev = m_pLast;
	if(m_pLast)
		m_pLast->m_pNext = pHolder;
	else
		m_pFirst = pHolder;
	m_pLast = pHolder;
}

int CSnapshotStorage::Get(int Tick, int64 *pTagtime, CSnapshot **ppData, CSnapshot **ppAltData) const
{
	CHolder *pHolder = m_pFirst;

	while(pHolder)
	{
		if(pHolder->m_Tick == Tick)
		{
			if(pTagtime)
				*pTagtime = pHolder->m_Tagtime;
			if(ppData)
				*ppData = pHolder->m_pSnap;
			if(ppAltData)
				*ppAltData = pHolder->m_pAltSnap;
			return pHolder->m_SnapSize;
		}

		pHolder = pHolder->m_pNext;
	}

	return -1;
}

// CSnapshotBuilder
CSnapshotBuilder::CSnapshotBuilder()
{
}

void CSnapshotBuilder::Init()
{
	m_Data.clear_size();
	m_aOffsets.clear_size();
	for(int i = 0; i < m_aExtendedItemTypes.size(); i++)
	{
		AddExtendedItemType(i);
	}
}

void CSnapshotBuilder::Init(const CSnapshot *pSnapshot)
{
	m_Data.clear_size();
	m_aOffsets.clear_size();

	if(pSnapshot->m_DataSize < 0 || pSnapshot->m_NumItems < 0)
	{
		dbg_msg("snapshot", "invalid snapshot");
		return;
	}

	m_Data.set_size(pSnapshot->m_DataSize);
	mem_copy(m_Data.base_ptr(), pSnapshot->DataStart(), pSnapshot->m_DataSize);
	m_aOffsets.set_size(pSnapshot->m_NumItems);
	mem_copy(m_aOffsets.base_ptr(), pSnapshot->Offsets(), sizeof(int) * pSnapshot->m_NumItems);
}

bool CSnapshotBuilder::UnserializeSnap(const char *pSrcData, int SrcSize)
{
	m_Data.clear_size();
	m_aOffsets.clear_size();

	if(SrcSize < (int) sizeof(int) * 2)
		return false;

	const int *pData = (const int *) pSrcData;
	const int DataSize = pData[0];
	const int NumItems = pData[1];
	if(DataSize < 0 || NumItems < 0)
		return false;

	// range check instead of the old hard MAX_SIZE / MAX_ITEMS rejection
	const int64 CompleteSize = (int64) DataSize + (int64) sizeof(int) * (2 + NumItems);
	if(CompleteSize != SrcSize)
		return false;

	// check offsets
	const int *pOffsets = pData + 2;
	int LastOffset = DataSize;
	for(int i = NumItems - 1; i >= 0; i--)
	{
		const int ItemSize = LastOffset - pOffsets[i];
		LastOffset = pOffsets[i];
		if(pOffsets[i] < 0 || ItemSize < (int) sizeof(CSnapshotItem))
			return false;
	}

	m_Data.set_size(DataSize);
	mem_copy(m_Data.base_ptr(), pOffsets + NumItems, DataSize);
	m_aOffsets.set_size(NumItems);
	mem_copy(m_aOffsets.base_ptr(), pOffsets, sizeof(int) * NumItems);
	return true;
}

CSnapshotItem *CSnapshotBuilder::GetItem(int Index) const
{
	return (CSnapshotItem *) (m_Data.base_ptr() + m_aOffsets[Index]);
}

int *CSnapshotBuilder::GetItemData(int Key) const
{
	for(int i = 0; i < m_aOffsets.size(); i++)
	{
		if(GetItem(i)->Key() == Key)
			return GetItem(i)->Data();
	}
	return 0;
}

struct CItemRef
{
	int m_Key;
	int m_Offset;
	int m_Size;
};

struct CItemRefLess
{
	bool operator()(const CItemRef &a, const CItemRef &b) const { return a.m_Key < b.m_Key; }
};

int CSnapshotBuilder::RequiredSize() const
{
	return sizeof(CSnapshot) + sizeof(int) * m_aOffsets.size() * 2 + m_Data.size();
}

int CSnapshotBuilder::Finish(void *pSnapdata)
{
	// flatten and make the snapshot
	CSnapshot *pSnap = (CSnapshot *) pSnapdata;
	const int NumItems = m_aOffsets.size();
	pSnap->m_DataSize = m_Data.size();
	pSnap->m_NumItems = NumItems;

	if(NumItems == 0)
		return sizeof(CSnapshot);

	array<CItemRef> aItems;
	aItems.set_size(NumItems);
	for(int i = 0; i < NumItems; i++)
	{
		aItems[i].m_Key = GetItem(i)->Key();
		aItems[i].m_Offset = m_aOffsets[i];
		aItems[i].m_Size = (i + 1 < NumItems ? m_aOffsets[i + 1] : m_Data.size()) - m_aOffsets[i];
	}

	// stable sort by key, O(n log n) instead of the old O(n^2) bubble sort
	sort(aItems.all(), CItemRefLess());

	// copy sorted items
	int OffsetCur = 0;
	for(int i = 0; i < NumItems; i++)
	{
		pSnap->SortedKeys()[i] = aItems[i].m_Key;
		pSnap->Offsets()[i] = OffsetCur;
		mem_copy(pSnap->DataStart() + OffsetCur, m_Data.base_ptr() + aItems[i].m_Offset, aItems[i].m_Size);
		OffsetCur += aItems[i].m_Size;
	}

	return sizeof(CSnapshot) + sizeof(int) * NumItems * 2 + m_Data.size();
}

static int GetTypeFromIndex(int Index)
{
	return CSnapshot::MAX_TYPE - Index;
}

bool CSnapshotBuilder::AddExtendedItemType(int Index)
{
	dbg_assert(0 <= Index && Index < m_aExtendedItemTypes.size(), "index out of range");
	int *pUuidItem = static_cast<int *>(NewItem(0, GetTypeFromIndex(Index), sizeof(Uuid))); // NETOBJTYPE_EX
	if(pUuidItem == nullptr)
	{
		return false;
	}

	const int TypeId = m_aExtendedItemTypes[Index];
	const Uuid ItemUuid = g_UuidManager.GetUuid(TypeId);
	for(size_t i = 0; i < sizeof(Uuid) / sizeof(unsigned int); i++)
	{
		pUuidItem[i] = bytes_be_to_uint(&ItemUuid.m_aData[i * sizeof(unsigned int)]);
	}
	return true;
}

int CSnapshotBuilder::GetExtendedItemTypeIndex(int TypeID)
{
	for(int i = 0; i < m_aExtendedItemTypes.size(); i++)
	{
		if(m_aExtendedItemTypes[i] == TypeID)
		{
			return i;
		}
	}
	const int Index = m_aExtendedItemTypes.size();
	m_aExtendedItemTypes.add(TypeID);
	if(AddExtendedItemType(Index))
	{
		return Index;
	}
	m_aExtendedItemTypes.remove_index(Index);
	return -1;
}

void *CSnapshotBuilder::NewItem(int Type, int ID, int Size)
{
	if(Size < 0)
		return 0;
	dbg_assert(Size % sizeof(int) == 0, "item size must be 4 byte aligned");

	if(Type >= OFFSET_UUID)
	{
		const int ExtendedItemTypeIndex = GetExtendedItemTypeIndex(Type);
		if(ExtendedItemTypeIndex == -1)
		{
			return nullptr;
		}
		Type = GetTypeFromIndex(ExtendedItemTypeIndex);
	}

	const int DataOffset = m_Data.size();
	m_Data.append(sizeof(CSnapshotItem) + Size);

	CSnapshotItem *pObj = (CSnapshotItem *) (m_Data.base_ptr() + DataOffset);

	mem_zero(pObj, sizeof(CSnapshotItem) + Size);
	pObj->SetKey(Type, ID);
	m_aOffsets.add(DataOffset);

	return pObj->Data();
}
