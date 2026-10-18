/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <base/system/debug.h>
#include <base/system/fs.h>
#include <base/system/mem.h>
#include <engine/map.h>
#include <engine/storage.h>
#include <game/mapitems.h>
#include "datafile.h"

// one object, one indexed slot per map loaded at the same time
class CEngineMap : public IEngineMap
{
	CDataFileReader m_aMaps[MAX_MAPS];

	static bool ValidIndex(int MapIndex) { return MapIndex >= 0 && MapIndex < MAX_MAPS; }

	bool LoadSlot(int MapIndex, const char *pMapName, IStorage *pStorage)
	{
		CDataFileReader &Data = m_aMaps[MapIndex];
		if(!pStorage)
			pStorage = Kernel()->RequestInterface<IStorage>();
		if(!pStorage)
			return false;
		if(!Data.Open(pStorage, pMapName, IStorage::TYPE_ALL))
			return false;
		// check version
		CMapItemVersion *pItem = (CMapItemVersion *) Data.FindItem(MAPITEMTYPE_VERSION, 0);
		if(!pItem || pItem->m_Version != CMapItemVersion::CURRENT_VERSION)
			return false;

		// replace compressed tile layers with uncompressed ones
		int GroupsStart, GroupsNum, LayersStart, LayersNum;
		Data.GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
		Data.GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
		for(int g = 0; g < GroupsNum; g++)
		{
			CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(Data.GetItem(GroupsStart + g, 0, 0));
			for(int l = 0; l < pGroup->m_NumLayers; l++)
			{
				CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(Data.GetItem(LayersStart + pGroup->m_StartLayer + l, 0, 0));

				if(pLayer->m_Type == LAYERTYPE_TILES)
				{
					CMapItemLayerTilemap *pTilemap = reinterpret_cast<CMapItemLayerTilemap *>(pLayer);

					if(pTilemap->m_Version > 3)
					{
						const int TilemapCount = pTilemap->m_Width * pTilemap->m_Height;
						const int TilemapSize = TilemapCount * sizeof(CTile);

						if((TilemapCount / pTilemap->m_Width != pTilemap->m_Height) || (TilemapSize / (int) sizeof(CTile) != TilemapCount))
						{
							dbg_msg("engine", "map layer too big (%d * %d * %u causes an integer overflow)", pTilemap->m_Width, pTilemap->m_Height, unsigned(sizeof(CTile)));
							return false;
						}
						CTile *pTiles = static_cast<CTile *>(mem_alloc(TilemapSize));
						if(!pTiles)
							return false;

						// extract original tile data
						int i = 0;
						CTile *pSavedTiles = static_cast<CTile *>(Data.GetData(pTilemap->m_Data));
						while(i < TilemapCount)
						{
							for(unsigned Counter = 0; Counter <= pSavedTiles->m_Skip && i < TilemapCount; Counter++)
							{
								pTiles[i] = *pSavedTiles;
								pTiles[i++].m_Skip = 0;
							}

							pSavedTiles++;
						}

						Data.ReplaceData(pTilemap->m_Data, reinterpret_cast<char *>(pTiles), TilemapSize);
					}
				}
			}
		}

		return true;
	}

public:
	virtual int GetDataSize(int MapIndex, int Index) override { return ValidIndex(MapIndex) ? m_aMaps[MapIndex].GetDataSize(Index) : 0; }
	virtual void *GetData(int MapIndex, int Index) override { return ValidIndex(MapIndex) ? m_aMaps[MapIndex].GetData(Index) : 0; }
	virtual void *GetDataSwapped(int MapIndex, int Index) override { return ValidIndex(MapIndex) ? m_aMaps[MapIndex].GetDataSwapped(Index) : 0; }
	virtual void UnloadData(int MapIndex, int Index) override
	{
		if(ValidIndex(MapIndex))
			m_aMaps[MapIndex].UnloadData(Index);
	}

	virtual void *GetItem(int MapIndex, int Index, int *pType, int *pID) override { return ValidIndex(MapIndex) ? m_aMaps[MapIndex].GetItem(Index, pType, pID) : 0; }
	virtual void GetType(int MapIndex, int Type, int *pStart, int *pNum) override
	{
		if(ValidIndex(MapIndex))
			m_aMaps[MapIndex].GetType(Type, pStart, pNum);
	}

	virtual void *FindItem(int MapIndex, int Type, int ID) override { return ValidIndex(MapIndex) ? m_aMaps[MapIndex].FindItem(Type, ID) : 0; }
	virtual int NumItems(int MapIndex) override { return ValidIndex(MapIndex) ? m_aMaps[MapIndex].NumItems() : 0; }

	virtual bool Load(const char *pMapName, IStorage *pStorage, int MapIndex) override
	{
		return ValidIndex(MapIndex) && LoadSlot(MapIndex, pMapName, pStorage);
	}

	virtual bool IsLoaded(int MapIndex) override
	{
		return ValidIndex(MapIndex) && m_aMaps[MapIndex].IsOpen();
	}

	virtual void Unload(int MapIndex) override
	{
		if(ValidIndex(MapIndex))
			m_aMaps[MapIndex].Close();
	}

	virtual SHA256_DIGEST Sha256(int MapIndex) override
	{
		return ValidIndex(MapIndex) ? m_aMaps[MapIndex].Sha256() : SHA256_ZEROED;
	}

	virtual unsigned Crc(int MapIndex) override
	{
		return ValidIndex(MapIndex) ? m_aMaps[MapIndex].Crc() : 0;
	}
};

extern IEngineMap *CreateEngineMap() { return new CEngineMap; }
