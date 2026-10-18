/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_LAYERS_H
#define GAME_LAYERS_H

#include <engine/map.h>
#include <game/mapitems.h>

class CLayers
{
	int m_GroupsNum;
	int m_GroupsStart;
	int m_LayersNum;
	int m_LayersStart;
	CMapItemGroup *m_pGameGroup;
	CMapItemLayerTilemap *m_pGameLayer;
	class IMap *m_pMap;
	int m_MapIndex;
	unsigned m_MapGeneration;
	bool m_TilemapSkipComputed;

	void InitGameLayer();
	void InitTilemapSkip();

public:
	CLayers();
	void Init(class IKernel *pKernel, class IMap *pMap = 0, int MapIndex = 0);
	int NumGroups() const { return m_GroupsNum; }
	int NumLayers() const { return m_LayersNum; }
	class IMap *Map() const { return m_pMap; }
	int MapIndex() const { return m_MapIndex; }
	// id of the loaded map this object points into
	unsigned Generation() const { return m_MapGeneration; }
	// point at another engine map slot
	void SetMapIndex(int MapIndex);
	// CPU tile rendering needs m_Skip, compute it on demand
	void EnsureTilemapSkip();

	// the map slots in the engine map are indexed, these forward the index
	int GetDataSize(int Index) const { return m_pMap->GetDataSize(m_MapIndex, Index); }
	void *GetData(int Index) const { return m_pMap->GetData(m_MapIndex, Index); }
	void *GetDataSwapped(int Index) const { return m_pMap->GetDataSwapped(m_MapIndex, Index); }
	void UnloadData(int Index) const { m_pMap->UnloadData(m_MapIndex, Index); }
	void *GetItem(int Index, int *pType, int *pID) const { return m_pMap->GetItem(m_MapIndex, Index, pType, pID); }
	void GetType(int Type, int *pStart, int *pNum) const { return m_pMap->GetType(m_MapIndex, Type, pStart, pNum); }
	void *FindItem(int Type, int ID) const { return m_pMap->FindItem(m_MapIndex, Type, ID); }
	int NumItems() const { return m_pMap->NumItems(m_MapIndex); }
	CMapItemGroup *GameGroup() const { return m_pGameGroup; }
	CMapItemLayerTilemap *GameLayer() const { return m_pGameLayer; }
	CMapItemGroup *GetGroup(int Index) const;
	CMapItemLayer *GetLayer(int Index) const;
};

#endif
