/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#include <base/math.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/layers.h>

#include "camera.h"
#include "map.h"

CMapComponent::CMapComponent() :
	m_LayersBackground(CMapLayers::TYPE_BACKGROUND),
	m_LayersForeground(CMapLayers::TYPE_FOREGROUND)
{
	m_GameMapLoaded = false;
	m_GameMapFading = false;
	m_GameMapAlpha = 0.0f;

	m_pEngineMap = 0;
	m_GameGeneration = 0;
	m_FadeMap.m_pLayers = 0;
	m_FadeMap.m_ImageType = CMapImages::MAP_TYPE_GAME_FADE;
	m_FadeMap.m_Alpha = 0.0f;
	m_FadeMap.m_Offset = vec2(0.0f, 0.0f);
	m_FadeMap.m_HasOffset = false;
	m_FadeMap.m_Loaded = false;
	m_FadeMap.m_StartPending = false;
	m_FadeMap.m_HoldOnly = false;
	m_FadeMap.m_PendingTime = 0.0f;
}

void CMapComponent::ReleaseGameMap()
{
	m_GameMapFading = false;
	m_GameMapAlpha = 0.0f;

	if(m_GameMapLoaded)
	{
		// the engine keeps the map loaded for us, hand it back now
		m_GameMapLoaded = false;
		Client()->UnloadMap();
	}
}

void CMapComponent::ReleaseTilemapTextures(unsigned Generation)
{
	if(Generation == 0)
		return;
	for(int i = m_lTilemapTextures.size() - 1; i >= 0; i--)
	{
		if(m_lTilemapTextures[i].m_Generation != Generation)
			continue;
		if(m_lTilemapTextures[i].m_Texture.IsValid())
			Graphics()->UnloadTexture(&m_lTilemapTextures[i].m_Texture);
		m_lTilemapTextures.remove_index_fast(i);
	}
}

void CMapComponent::BeginGameMapFade(const CLayers &OldLayers, bool HoldOnly)
{
	if(!m_pEngineMap)
		m_pEngineMap = Kernel()->RequestInterface<IEngineMap>();
	if(!m_pEngineMap)
		return;
	if(!m_FadeMap.m_pLayers)
		m_FadeMap.m_pLayers = new CLayers;

	// the old map is already in the fade slot, nothing to reload
	*m_FadeMap.m_pLayers = OldLayers;
	m_FadeMap.m_pLayers->SetMapIndex(IEngineMap::SLOT_FADE);
	if(!m_FadeMap.m_pLayers->Map())
		return;
	// hand the old image table to the fade
	m_Images.MoveMapType(CMapImages::MAP_TYPE_GAME, CMapImages::MAP_TYPE_GAME_FADE);
	CMapLayers::LoadEnvPoints(m_FadeMap.m_pLayers, m_FadeMap.m_lEnvPoints);

	m_FadeMap.m_Alpha = 1.0f;
	m_FadeMap.m_Offset = vec2(0.0f, 0.0f);
	m_FadeMap.m_HasOffset = false;
	m_FadeMap.m_Loaded = true;
	m_FadeMap.m_StartPending = true;
	m_FadeMap.m_HoldOnly = HoldOnly;
	m_FadeMap.m_PendingTime = 0.0f;
}

void CMapComponent::StartGameMapFade()
{
	if(!m_FadeMap.m_Loaded)
		return;
	m_FadeMap.m_StartPending = false;
	if(m_FadeMap.m_HoldOnly)
	{
		// NO_FADE: tiles are ready, cut now
		EndGameMapFade();
	}
}

void CMapComponent::SetFadeMapOffset(const vec2 &Offset)
{
	if(!m_FadeMap.m_Loaded)
		return;
	m_FadeMap.m_Offset = Offset;
	m_FadeMap.m_HasOffset = true;
}

vec2 CMapComponent::FadeMapCenter() const
{
	const vec2 Center = *m_pClient->m_pCamera->GetCenter();
	if(!m_FadeMap.m_HasOffset)
		return Center;
	return Center - m_FadeMap.m_Offset;
}

float CMapComponent::FadeMapZoom() const
{
	return m_pClient->m_pCamera->GetZoom();
}

void CMapComponent::EndGameMapFade()
{
	if(!m_FadeMap.m_pLayers)
	{
		m_FadeMap.m_Alpha = 0.0f;
		m_FadeMap.m_Loaded = false;
		m_FadeMap.m_StartPending = false;
		return;
	}

	if(m_FadeMap.m_Loaded)
	{
		// the cached tile data textures reference the map data being unloaded
		ReleaseTilemapTextures(m_FadeMap.m_pLayers->Generation());
		m_Images.UnloadMap(CMapImages::MAP_TYPE_GAME_FADE);
		if(m_pEngineMap)
			m_pEngineMap->Unload(IEngineMap::SLOT_FADE);
		m_FadeMap.m_Loaded = false;
	}

	m_FadeMap.m_lEnvPoints.clear();
	m_FadeMap.m_Alpha = 0.0f;
	m_FadeMap.m_Offset = vec2(0.0f, 0.0f);
	m_FadeMap.m_HasOffset = false;
	m_FadeMap.m_StartPending = false;
	m_FadeMap.m_HoldOnly = false;
}

void CMapComponent::OnMapLoad()
{
	m_GameGeneration = Layers() ? Layers()->Generation() : 0;
	m_GameMapLoaded = true;
	m_GameMapFading = false;
	m_GameMapAlpha = 0.0f;
}

void CMapComponent::OnMapUnload()
{
	m_GameMapLoaded = false;
	m_GameMapFading = false;
	m_GameMapAlpha = 0.0f;
	// release only the game map, not the fade
	ReleaseTilemapTextures(m_GameGeneration);
	m_GameGeneration = 0;
}

void CMapComponent::PrepareGameMap()
{
	if(!Layers() || !Layers()->Map())
		return;
	if(!Config()->m_GfxTileBuffering || !Graphics()->TilemapShaderEnabled())
		return;

	// build the new map's tile arrays before it is shown
	for(int g = 0; g < Layers()->NumGroups(); g++)
	{
		CMapItemGroup *pGroup = Layers()->GetGroup(g);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = Layers()->GetLayer(pGroup->m_StartLayer + l);
			if(pLayer->m_Type != LAYERTYPE_TILES)
				continue;
			CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *) pLayer;
			if(pTMap->m_Image < 0 || pTMap->m_Data < 0)
				continue;
			int LayerIndex;
			GetTilemapTexture(Layers(), pTMap, &LayerIndex);
		}
	}
}

IGraphics::CTextureHandle CMapComponent::GetTilemapTexture(const CLayers *pLayers, const CMapItemLayerTilemap *pLayer, int *pLayerIndex)
{
	*pLayerIndex = -1;
	// a layer without tile data can never be in the array, so bail out before building
	if(!pLayers || !pLayers->Map() || !pLayer || pLayer->m_Data < 0)
		return IGraphics::CTextureHandle();

	// look for the array that already contains this layer
	const unsigned Generation = pLayers->Generation();
	for(int i = 0; i < m_lTilemapTextures.size(); i++)
	{
		STilemapTexture &Tex = m_lTilemapTextures[i];
		if(Tex.m_Generation != Generation || Tex.m_Width != pLayer->m_Width || Tex.m_Height != pLayer->m_Height)
			continue;
		for(int l = 0; l < Tex.m_lLayers.size(); l++)
		{
			if(Tex.m_lLayers[l] == pLayer)
			{
				*pLayerIndex = l;
				return Tex.m_Texture;
			}
		}
	}

	// one array for all drawable tile layers of this size
	STilemapTexture Tex;
	Tex.m_Generation = Generation;
	Tex.m_Width = pLayer->m_Width;
	Tex.m_Height = pLayer->m_Height;

	for(int g = 0; g < pLayers->NumGroups(); g++)
	{
		CMapItemGroup *pGroup = pLayers->GetGroup(g);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pCandidate = pLayers->GetLayer(pGroup->m_StartLayer + l);
			if(pCandidate->m_Type != LAYERTYPE_TILES)
				continue;
			CMapItemLayerTilemap *pTilemap = (CMapItemLayerTilemap *) pCandidate;
			// solid-color layers never sample this texture
			if(pTilemap->m_Width != Tex.m_Width || pTilemap->m_Height != Tex.m_Height || pTilemap->m_Data < 0 || pTilemap->m_Image < 0)
				continue;
			Tex.m_lLayers.add(pTilemap);
		}
	}

	const int NumLayers = Tex.m_lLayers.size();
	if(NumLayers <= 0)
		return IGraphics::CTextureHandle();

	// CTile's first two bytes are the index and flags, uploaded as R8G8 (SDL_GPU
	// has no RGB8). TEXLOAD_NORESAMPLE keeps them from being averaged.
	const size_t LayerSize = (size_t) Tex.m_Width * Tex.m_Height * 2;
	unsigned char *pData = (unsigned char *) mem_alloc(LayerSize * NumLayers);
	if(!pData)
		return IGraphics::CTextureHandle();

	for(int l = 0; l < NumLayers; l++)
	{
		const CTile *pTiles = (const CTile *) pLayers->GetData(Tex.m_lLayers[l]->m_Data);
		unsigned char *pOut = pData + l * LayerSize;
		const int NumTiles = Tex.m_Width * Tex.m_Height;
		for(int t = 0; t < NumTiles; t++)
		{
			pOut[t * 2 + 0] = pTiles ? pTiles[t].m_Index : 0;
			pOut[t * 2 + 1] = pTiles ? pTiles[t].m_Flags : 0;
		}
	}

	Tex.m_Texture = Graphics()->LoadTextureRaw(Tex.m_Width, Tex.m_Height, NumLayers, CImageInfo::FORMAT_RG, pData, CImageInfo::FORMAT_RG, IGraphics::TEXLOAD_NOMIPMAPS | IGraphics::TEXLOAD_NORESAMPLE);
	mem_free(pData);

	if(!Tex.m_Texture.IsValid())
		return IGraphics::CTextureHandle();

	// the requested layer must be in the array, otherwise drop the texture instead of caching it
	int LayerIndex = -1;
	for(int l = 0; l < Tex.m_lLayers.size(); l++)
	{
		if(Tex.m_lLayers[l] == pLayer)
		{
			LayerIndex = l;
			break;
		}
	}
	if(LayerIndex < 0)
	{
		Graphics()->UnloadTexture(&Tex.m_Texture);
		return IGraphics::CTextureHandle();
	}

	m_lTilemapTextures.add(Tex);
	*pLayerIndex = LayerIndex;
	return Tex.m_Texture;
}

void CMapComponent::ClearTilemapTextures()
{
	for(int i = 0; i < m_lTilemapTextures.size(); i++)
	{
		if(m_lTilemapTextures[i].m_Texture.IsValid())
			Graphics()->UnloadTexture(&m_lTilemapTextures[i].m_Texture);
	}
	m_lTilemapTextures.clear();
}

void CMapComponent::OnStateChange(int NewState, int OldState)
{
	if(OldState >= IClient::STATE_ONLINE && NewState < IClient::STATE_ONLINE)
	{
		// the old map no longer needs to fade
		EndGameMapFade();

		// keep the map loaded while it fades out over the menu map
		if(m_GameMapLoaded)
		{
			m_GameMapFading = true;
			m_GameMapAlpha = 1.0f;
		}
	}
	else if(NewState >= IClient::STATE_ONLINE)
	{
		m_GameMapFading = false;
		m_GameMapAlpha = 1.0f;
	}
	else if(m_GameMapFading)
	{
		// connecting again, the map is about to be replaced
		ReleaseGameMap();
	}
}

void CMapComponent::OnRender()
{
	if(m_FadeMap.m_Loaded)
	{
		const float Delta = clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
		if(m_FadeMap.m_StartPending)
		{
			// never hold the old map for more than a tick or two
			m_FadeMap.m_PendingTime += Delta;
			if(m_FadeMap.m_PendingTime > 1.0f)
			{
				if(m_FadeMap.m_HoldOnly)
					EndGameMapFade();
				else
					m_FadeMap.m_StartPending = false;
			}
		}
		else
		{
			m_FadeMap.m_Alpha = maximum(0.0f, m_FadeMap.m_Alpha - Delta * 1000.0f / GAME_MAP_FADE_TIME);
			if(m_FadeMap.m_Alpha <= 0.0f)
				EndGameMapFade();
		}
	}

	if(!m_GameMapFading)
		return;

	const float Delta = clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	m_GameMapAlpha = maximum(0.0f, m_GameMapAlpha - Delta * 1000.0f / GAME_MAP_FADE_TIME);
	if(m_GameMapAlpha <= 0.0f)
		ReleaseGameMap();
}

void CMapComponent::OnShutdown()
{
	EndGameMapFade();
}

void CMapComponent::RenderBackground(float Time)
{
	const float ScreenHeight = 300.0f * Graphics()->ScreenUIScale();
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0, 0, ScreenWidth, ScreenHeight);

	// render the tiles
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	const float Size = 15.0f;
	const float OffsetTime = fmod(Time * 0.15f, 2.0f);
	for(int y = -2; y < (int) (ScreenWidth / Size); y++)
		for(int x = -2; x < (int) (ScreenHeight / Size); x++)
		{
			Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.045f);
			IGraphics::CQuadItem QuadItem((x - OffsetTime) * Size * 2 + (y & 1) * Size, (y + OffsetTime) * Size, Size, Size);
			Graphics()->SingleQuadDrawTL(&QuadItem);
		}
	Graphics()->QuadsEnd();

	// render border fade
	static IGraphics::CTextureHandle s_TextureBlob = Graphics()->LoadTexture("ui/blob.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
	Graphics()->TextureSet(s_TextureBlob);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0, 0, 0, 0.5f);
	IGraphics::CQuadItem QuadItem = IGraphics::CQuadItem(-100, -100, ScreenWidth + 200, ScreenHeight + 200);
	Graphics()->SingleQuadDrawTL(&QuadItem);
	Graphics()->QuadsEnd();

	UI()->MapScreen();
}
