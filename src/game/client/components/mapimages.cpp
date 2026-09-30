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

	m_EasterIsLoaded = false;
}

void CMapImages::LoadMapImages(IMap *pMap, class CLayers *pLayers, int MapType)
{
	if(MapType < 0 || MapType >= NUM_MAP_TYPES)
		return;

	// unload all textures
	UnloadMap(MapType);

	int Start;
	pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &m_Info[MapType].m_Count);
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

		CMapItemImage *pImg = (CMapItemImage *) pMap->GetItem(Start + i, 0, 0);
		if(pImg->m_External || (pImg->m_Version > 1 && pImg->m_MustBe1 != 1))
		{
			char Buf[IO_MAX_PATH_LENGTH];
			char *pName = (char *) pMap->GetData(pImg->m_ImageName);
			str_format(Buf, sizeof(Buf), "mapres/%s.png", pName);
			if(FoundQuadLayer)
				m_Info[MapType].m_aTextures[i].m_Quads = Graphics()->LoadTexture(Buf, IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
			if(FoundTileLayer)
				m_Info[MapType].m_aTextures[i].m_Tilemap = Graphics()->LoadTexture(Buf, IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, IGraphics::TEXLOAD_TILEMAP | IGraphics::TEXLOAD_NOMIPMAPS);
		}
		else
		{
			void *pData = pMap->GetData(pImg->m_ImageData);
			if(FoundQuadLayer)
				m_Info[MapType].m_aTextures[i].m_Quads = Graphics()->LoadTextureRaw(pImg->m_Width, pImg->m_Height, 1, CImageInfo::FORMAT_RGBA, pData, CImageInfo::FORMAT_RGBA, 0);
			if(FoundTileLayer)
				m_Info[MapType].m_aTextures[i].m_Tilemap = Graphics()->LoadTextureRaw(pImg->m_Width, pImg->m_Height, 1, CImageInfo::FORMAT_RGBA, pData, CImageInfo::FORMAT_RGBA, IGraphics::TEXLOAD_TILEMAP | IGraphics::TEXLOAD_NOMIPMAPS);
			pMap->UnloadData(pImg->m_ImageData);
		}
	}

	// easter time, preload easter tileset
	if(m_pClient->IsEaster())
		GetEasterTexture();
}

void CMapImages::OnMapLoad()
{
	LoadMapImages(Kernel()->RequestInterface<IMap>(), Layers(), MAP_TYPE_GAME);
}

void CMapImages::OnMapUnload()
{
	UnloadMap(MAP_TYPE_GAME);
}

void CMapImages::OnMenuMapLoad(IMap *pMap, int MapType)
{
	CLayers MenuLayers;
	MenuLayers.Init(Kernel(), pMap);
	LoadMapImages(pMap, &MenuLayers, MapType);
}

void CMapImages::UnloadMap(int MapType)
{
	if(MapType < 0 || MapType >= NUM_MAP_TYPES)
		return;

	for(int i = 0; i < m_Info[MapType].m_Count; i++)
	{
		Graphics()->UnloadTexture(&(m_Info[MapType].m_aTextures[i].m_Quads));
		Graphics()->UnloadTexture(&(m_Info[MapType].m_aTextures[i].m_Tilemap));
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
