/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include "map.h"

CMapComponent::CMapComponent() :
	m_LayersBackground(CMapLayers::TYPE_BACKGROUND),
	m_LayersForeground(CMapLayers::TYPE_FOREGROUND)
{
	m_GameMapLoaded = false;
	m_GameMapFading = false;
	m_GameMapAlpha = 0.0f;
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

void CMapComponent::OnMapLoad()
{
	m_GameMapLoaded = true;
	m_GameMapFading = false;
	m_GameMapAlpha = 0.0f;
}

void CMapComponent::OnMapUnload()
{
	m_GameMapLoaded = false;
	m_GameMapFading = false;
	m_GameMapAlpha = 0.0f;
}

void CMapComponent::OnStateChange(int NewState, int OldState)
{
	if(OldState >= IClient::STATE_ONLINE && NewState < IClient::STATE_ONLINE)
	{
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
	if(!m_GameMapFading)
		return;

	const float Delta = clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	m_GameMapAlpha = maximum(0.0f, m_GameMapAlpha - Delta * 1000.0f / GAME_MAP_FADE_TIME);
	if(m_GameMapAlpha <= 0.0f)
		ReleaseGameMap();
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
