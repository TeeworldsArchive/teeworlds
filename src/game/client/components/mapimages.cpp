/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/storage.h>
#include <game/client/component.h>
#include <game/mapitems.h>

#include "mapimages.h"

CMapImages::CMapImages()
{
	for(int i = 0; i < NUM_MAP_TYPES; i++)
		m_Info[i].m_Count = 0;

	m_CacheBytes = 0;
	m_CacheClock = 0;
	m_EasterIsLoaded = false;
}

bool CMapImages::IsCachedImage(const IGraphics::CTextureHandle &Handle) const
{
	if(!Handle.IsValid())
		return false;
	for(int i = 0; i < m_lImageCache.size(); i++)
	{
		const SCachedImage &Entry = m_lImageCache[i];
		if((Entry.m_Quads.IsValid() && Entry.m_Quads.Id() == Handle.Id()) ||
			(Entry.m_Tilemap.IsValid() && Entry.m_Tilemap.Id() == Handle.Id()))
			return true;
	}
	return false;
}

bool CMapImages::IsCacheEntryUsed(const SCachedImage &Entry) const
{
	for(int t = 0; t < NUM_MAP_TYPES; t++)
	{
		for(int i = 0; i < m_Info[t].m_Count; i++)
		{
			const IGraphics::CTextureHandle &Quads = m_Info[t].m_aTextures[i].m_Quads;
			const IGraphics::CTextureHandle &Tilemap = m_Info[t].m_aTextures[i].m_Tilemap;
			if(Entry.m_Quads.IsValid() && Quads.IsValid() && Quads.Id() == Entry.m_Quads.Id())
				return true;
			if(Entry.m_Tilemap.IsValid() && Tilemap.IsValid() && Tilemap.Id() == Entry.m_Tilemap.Id())
				return true;
		}
	}
	return false;
}

bool CMapImages::EvictOneImage()
{
	int Oldest = -1;
	for(int i = 0; i < m_lImageCache.size(); i++)
	{
		if(IsCacheEntryUsed(m_lImageCache[i]))
			continue;
		if(Oldest < 0 || m_lImageCache[i].m_LastUse < m_lImageCache[Oldest].m_LastUse)
			Oldest = i;
	}
	if(Oldest < 0)
		return false;

	SCachedImage &Entry = m_lImageCache[Oldest];
	if(Entry.m_Quads.IsValid())
	{
		Graphics()->UnloadTexture(&Entry.m_Quads);
		m_CacheBytes -= Entry.m_PixelBytes;
	}
	if(Entry.m_Tilemap.IsValid())
	{
		Graphics()->UnloadTexture(&Entry.m_Tilemap);
		m_CacheBytes -= Entry.m_PixelBytes;
	}
	m_lImageCache.remove_index_fast(Oldest);
	return true;
}

IGraphics::CTextureHandle CMapImages::CacheImage(const char *pPath, bool IsQuads, int Width, int Height)
{
	for(int i = 0; i < m_lImageCache.size(); i++)
	{
		SCachedImage &Entry = m_lImageCache[i];
		if(str_comp(Entry.m_aPath, pPath) != 0)
			continue;

		IGraphics::CTextureHandle &Handle = IsQuads ? Entry.m_Quads : Entry.m_Tilemap;
		if(!Handle.IsValid())
		{
			Handle = Graphics()->LoadTexture(pPath, IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO,
				IsQuads ? 0 : (IGraphics::TEXLOAD_TILEMAP | IGraphics::TEXLOAD_NOMIPMAPS));
			const int64 Bytes = (int64) Width * Height * 4;
			if(Handle.IsValid())
			{
				// one decoded image per variant, the size is what eviction frees
				Entry.m_PixelBytes = Bytes;
				m_CacheBytes += Bytes;
			}
			while(m_CacheBytes > (int64) IMAGE_CACHE_BUDGET && EvictOneImage())
			{
			}
		}
		Entry.m_LastUse = ++m_CacheClock;
		return Handle;
	}

	SCachedImage Entry;
	Entry.m_Quads = IGraphics::CTextureHandle();
	Entry.m_Tilemap = IGraphics::CTextureHandle();
	Entry.m_PixelBytes = 0;
	Entry.m_LastUse = ++m_CacheClock;
	str_copy(Entry.m_aPath, pPath, sizeof(Entry.m_aPath));
	if(IsQuads)
		Entry.m_Quads = Graphics()->LoadTexture(pPath, IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
	else
		Entry.m_Tilemap = Graphics()->LoadTexture(pPath, IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, IGraphics::TEXLOAD_TILEMAP | IGraphics::TEXLOAD_NOMIPMAPS);
	const int64 Bytes = (int64) Width * Height * 4;
	Entry.m_PixelBytes = Bytes;
	m_lImageCache.add(Entry);
	if(Entry.m_Quads.IsValid() || Entry.m_Tilemap.IsValid())
		m_CacheBytes += Bytes;
	while(m_CacheBytes > (int64) IMAGE_CACHE_BUDGET && EvictOneImage())
	{
	}

	// the fresh entry only goes if every other entry is in use
	for(int i = 0; i < m_lImageCache.size(); i++)
	{
		if(str_comp(m_lImageCache[i].m_aPath, pPath) == 0)
			return IsQuads ? m_lImageCache[i].m_Quads : m_lImageCache[i].m_Tilemap;
	}
	return IGraphics::CTextureHandle();
}

void CMapImages::LoadMapImages(CLayers *pLayers, int MapType)
{
	if(MapType < 0 || MapType >= NUM_MAP_TYPES)
		return;

	// unload all textures
	UnloadMap(MapType);

	int Start;
	pLayers->GetType(MAPITEMTYPE_IMAGE, &Start, &m_Info[MapType].m_Count);
	m_Info[MapType].m_Count = clamp(m_Info[MapType].m_Count, 0, int(MAX_TEXTURES));

	// load new textures
	for(int i = 0; i < m_Info[MapType].m_Count; i++)
	{
		bool FoundQuadLayer = false;
		bool FoundTileLayer = false;
		for(int k = 0; k < pLayers->NumLayers(); k++)
		{
			const CMapItemLayer *const pLayer = pLayers->GetLayer(k);
			if(!FoundQuadLayer && pLayer->m_Type == LAYERTYPE_QUADS && ((const CMapItemLayerQuads *) pLayer)->m_Image == i)
				FoundQuadLayer = true;
			if(!FoundTileLayer && pLayer->m_Type == LAYERTYPE_TILES && ((const CMapItemLayerTilemap *) pLayer)->m_Image == i)
				FoundTileLayer = true;
		}

		CMapItemImage *pImg = (CMapItemImage *) pLayers->GetItem(Start + i, 0, 0);
		if(pImg->m_External || (pImg->m_Version > 1 && pImg->m_MustBe1 != 1))
		{
			char Buf[IO_MAX_PATH_LENGTH];
			char *pName = (char *) pLayers->GetData(pImg->m_ImageName);
			str_format(Buf, sizeof(Buf), "mapres/%s.png", pName);
			// shared by path: the fade and the next map reuse it
			if(FoundQuadLayer)
				m_Info[MapType].m_aTextures[i].m_Quads = CacheImage(Buf, true, pImg->m_Width, pImg->m_Height);
			if(FoundTileLayer)
				m_Info[MapType].m_aTextures[i].m_Tilemap = CacheImage(Buf, false, pImg->m_Width, pImg->m_Height);
		}
		else
		{
			void *pData = pLayers->GetData(pImg->m_ImageData);
			if(FoundQuadLayer)
				m_Info[MapType].m_aTextures[i].m_Quads = Graphics()->LoadTextureRaw(pImg->m_Width, pImg->m_Height, 1, CImageInfo::FORMAT_RGBA, pData, CImageInfo::FORMAT_RGBA, 0);
			if(FoundTileLayer)
				m_Info[MapType].m_aTextures[i].m_Tilemap = Graphics()->LoadTextureRaw(pImg->m_Width, pImg->m_Height, 1, CImageInfo::FORMAT_RGBA, pData, CImageInfo::FORMAT_RGBA, IGraphics::TEXLOAD_TILEMAP | IGraphics::TEXLOAD_NOMIPMAPS);
			pLayers->UnloadData(pImg->m_ImageData);
		}
	}

	// easter time, preload easter tileset
	if(m_pClient->IsEaster())
		GetEasterTexture();
}

void CMapImages::OnMapLoad()
{
	LoadMapImages(Layers(), MAP_TYPE_GAME);
}

void CMapImages::OnMapUnload()
{
	UnloadMap(MAP_TYPE_GAME);
}

void CMapImages::MoveMapType(int From, int To)
{
	if(From < 0 || From >= NUM_MAP_TYPES || To < 0 || To >= NUM_MAP_TYPES || From == To)
		return;

	UnloadMap(To);
	m_Info[To] = m_Info[From];
	for(int i = 0; i < m_Info[From].m_Count; i++)
	{
		m_Info[From].m_aTextures[i].m_Quads = IGraphics::CTextureHandle();
		m_Info[From].m_aTextures[i].m_Tilemap = IGraphics::CTextureHandle();
	}
	m_Info[From].m_Count = 0;
}

void CMapImages::OnMenuMapLoad(IMap *pMap, int MapIndex, int MapType)
{
	CLayers MenuLayers;
	MenuLayers.Init(Kernel(), pMap, MapIndex);
	LoadMapImages(&MenuLayers, MapType);
}

void CMapImages::UnloadMap(int MapType)
{
	if(MapType < 0 || MapType >= NUM_MAP_TYPES)
		return;

	for(int i = 0; i < m_Info[MapType].m_Count; i++)
	{
		IGraphics::CTextureHandle &Quads = m_Info[MapType].m_aTextures[i].m_Quads;
		IGraphics::CTextureHandle &Tilemap = m_Info[MapType].m_aTextures[i].m_Tilemap;
		// cached tilesets live on, embedded ones are freed here
		if(Quads.IsValid() && !IsCachedImage(Quads))
			Graphics()->UnloadTexture(&Quads);
		if(Tilemap.IsValid() && !IsCachedImage(Tilemap))
			Graphics()->UnloadTexture(&Tilemap);
		Quads = IGraphics::CTextureHandle();
		Tilemap = IGraphics::CTextureHandle();
	}
	m_Info[MapType].m_Count = 0;
}

IGraphics::CTextureHandle CMapImages::GetEasterTexture()
{
	if(!m_EasterIsLoaded)
	{
		m_EasterTexture = Graphics()->LoadTexture("mapres/easter.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, IGraphics::TEXLOAD_TILEMAP);
		if(!m_EasterTexture.IsValid())
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "mapimages", "Failed to load easter.png");
		m_EasterIsLoaded = true;
	}
	return m_EasterTexture;
}

IGraphics::CTextureHandle CMapImages::Get(int Index, bool IsQuads, int MapType) const
{
	if(MapType < 0)
		MapType = (Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK) ? MAP_TYPE_GAME : MAP_TYPE_MENU;
	MapType = clamp(MapType, 0, (int) NUM_MAP_TYPES - 1);
	return IsQuads ? m_Info[MapType].m_aTextures[clamp(Index, 0, m_Info[MapType].m_Count)].m_Quads : m_Info[MapType].m_aTextures[clamp(Index, 0, m_Info[MapType].m_Count)].m_Tilemap;
}

int CMapImages::Num() const
{
	if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return m_Info[MAP_TYPE_GAME].m_Count;
	return m_Info[MAP_TYPE_MENU].m_Count;
}
