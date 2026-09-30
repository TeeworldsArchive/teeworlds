/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <base/tl/array.h>

#include <engine/demo.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/component.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/layers.h>

#include "camera.h"
#include "map.h"
#include "mapimages.h"
#include "maplayers.h"
#include "menus.h"

CMapLayers::CMapLayers(int Type)
{
	m_Type = Type;
	m_OnlineStartTime = 0;

	for(int i = 0; i < MAX_MENU_MAP_SLOTS; i++)
	{
		m_aMenuMaps[i].m_pMap = 0;
		m_aMenuMaps[i].m_pLayers = 0;
		m_aMenuMaps[i].m_ImageType = CMapImages::MAP_TYPE_MENU + i;
		m_aMenuMaps[i].m_Alpha = 0.0f;
		m_aMenuMaps[i].m_Loaded = false;
	}
	m_CurrentMenuMap = -1;
	m_PrevMenuMap = -1;
	m_MenuMapFadeIn = false;

	m_pEnvEvalLayers = 0;
	m_pEnvEvalPoints = 0;
	m_EnvEvalIsMenuMap = false;
}

void CMapLayers::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE)
		m_OnlineStartTime = Client()->LocalTime(); // reset time for non-scynchronized envelopes

	if(m_Type != TYPE_BACKGROUND || m_CurrentMenuMap < 0)
		return;

	// The menu map is the base layer of both transitions: joining fades the
	// game map in over it, disconnecting restores it and fades the game map
	// out on top of it.
	if(NewState >= IClient::STATE_ONLINE && OldState < IClient::STATE_ONLINE)
	{
		m_MenuMapFadeIn = false;
		m_aMenuMaps[m_CurrentMenuMap].m_Alpha = 1.0f;
	}
	else if(NewState == IClient::STATE_OFFLINE && OldState >= IClient::STATE_ONLINE)
	{
		m_MenuMapFadeIn = false;
		m_aMenuMaps[m_CurrentMenuMap].m_Alpha = 1.0f;
	}
}

bool CMapLayers::LoadMenuMap(int Slot)
{
	if(Slot < 0 || Slot >= MAX_MENU_MAP_SLOTS)
		return false;

	const char *pMenuMap = Config()->m_ClMenuMap;
	if(str_comp(pMenuMap, "auto") == 0)
	{
		switch(time_season())
		{
			case SEASON_SPRING:
				pMenuMap = "heavens";
				break;
			case SEASON_SUMMER:
				pMenuMap = "jungle";
				break;
			case SEASON_AUTUMN:
				pMenuMap = "autumn";
				break;
			case SEASON_WINTER:
				pMenuMap = "winter";
				break;
			case SEASON_NEWYEAR:
				pMenuMap = "newyear";
				break;
		}
	}

	const int HourOfTheDay = time_houroftheday();
	const bool IsDaytime = HourOfTheDay >= 6 && HourOfTheDay < 18;

	IEngineMap *pMap = m_aMenuMaps[Slot].m_pMap;

	char aBuf[128];
	// check for the appropriate day/night map
	str_format(aBuf, sizeof(aBuf), "ui/themes/%s_%s.map", pMenuMap, IsDaytime ? "day" : "night");
	if(!pMap->Load(aBuf, m_pClient->Storage()))
	{
		// fall back on generic map
		str_format(aBuf, sizeof(aBuf), "ui/themes/%s.map", pMenuMap);
		if(!pMap->Load(aBuf, m_pClient->Storage()))
		{
			// fall back on day/night alternative map
			str_format(aBuf, sizeof(aBuf), "ui/themes/%s_%s.map", pMenuMap, IsDaytime ? "night" : "day");
			if(!pMap->Load(aBuf, m_pClient->Storage()))
			{
				str_format(aBuf, sizeof(aBuf), "map '%s' not found", pMenuMap);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);
				return false;
			}
		}
	}

	str_format(aBuf, sizeof(aBuf), "loaded map '%s'", pMenuMap);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);

	m_aMenuMaps[Slot].m_pLayers->Init(Kernel(), pMap);
	m_pClient->m_pMapComponent->Images()->OnMenuMapLoad(pMap, m_aMenuMaps[Slot].m_ImageType);
	LoadEnvPoints(m_aMenuMaps[Slot].m_pLayers, m_aMenuMaps[Slot].m_lEnvPoints);
	m_aMenuMaps[Slot].m_Loaded = true;
	return true;
}

void CMapLayers::UnloadMenuMap(int Slot)
{
	if(Slot < 0 || Slot >= MAX_MENU_MAP_SLOTS)
		return;

	if(m_aMenuMaps[Slot].m_pMap)
		m_aMenuMaps[Slot].m_pMap->Unload();
	m_pClient->m_pMapComponent->Images()->UnloadMap(m_aMenuMaps[Slot].m_ImageType);
	m_aMenuMaps[Slot].m_lEnvPoints.clear();
	m_aMenuMaps[Slot].m_Loaded = false;
	m_aMenuMaps[Slot].m_Alpha = 0.0f;
	// the cached tile data textures may reference the unloaded map data
	m_pClient->m_pMapComponent->ClearTilemapTextures();
}

void CMapLayers::FinalizeMenuMapFade()
{
	if(m_PrevMenuMap >= 0)
	{
		UnloadMenuMap(m_PrevMenuMap);
		m_PrevMenuMap = -1;
	}
}

void CMapLayers::UpdateMenuMapFade()
{
	const float Delta = clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	const float Speed = 1000.0f / MENU_MAP_FADE_TIME; // cross-fade duration
	const bool Online = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;

	// while a game is running the menu map fades out over the game map. The
	// alpha is set back to 1 by OnStateChange when the transition starts.
	if(m_CurrentMenuMap >= 0)
	{
		float &Alpha = m_aMenuMaps[m_CurrentMenuMap].m_Alpha;
		if(Online)
			Alpha = maximum(0.0f, Alpha - Delta * Speed);
		else if(m_MenuMapFadeIn)
		{
			// switching from no map: fade in over the animated background
			Alpha = minimum(1.0f, Alpha + Delta * Speed);
			if(Alpha >= 1.0f)
				m_MenuMapFadeIn = false;
		}
	}

	// the previous menu map fades out during a theme switch
	if(m_PrevMenuMap >= 0)
	{
		m_aMenuMaps[m_PrevMenuMap].m_Alpha = maximum(0.0f, m_aMenuMaps[m_PrevMenuMap].m_Alpha - Delta * Speed);
		if(m_aMenuMaps[m_PrevMenuMap].m_Alpha <= 0.0f)
			FinalizeMenuMapFade();
	}
}

int CMapLayers::GetInitAmount() const
{
	if(m_Type == TYPE_BACKGROUND)
		return 1 + (Config()->m_ClShowMenuMap ? 14 : 0);
	return 0;
}

void CMapLayers::OnInit()
{
	if(m_Type == TYPE_BACKGROUND)
	{
		for(int i = 0; i < MAX_MENU_MAP_SLOTS; i++)
		{
			m_aMenuMaps[i].m_pLayers = new CLayers;
			m_aMenuMaps[i].m_pMap = CreateEngineMap();
		}

		m_pClient->m_pMenus->RenderLoading(1);
		if(Config()->m_ClShowMenuMap)
		{
			if(LoadMenuMap(0))
			{
				m_CurrentMenuMap = 0;
				m_aMenuMaps[0].m_Alpha = 1.0f;
			}
			m_pClient->m_pMenus->RenderLoading(14);
		}
	}

	m_pEggTiles = 0;
}

void CMapLayers::OnMapLoad()
{
	if(Layers())
	{
		LoadEnvPoints(Layers(), m_lEnvPoints);

		// easter time, place eggs
		if(m_pClient->IsEaster())
			PlaceEasterEggs(Layers());
	}
}

void CMapLayers::OnMapUnload()
{
	// the envelope points and the easter eggs point into the map data
	m_lEnvPoints.clear();
	if(m_pEggTiles)
	{
		mem_free(m_pEggTiles);
		m_pEggTiles = 0;
	}
}

void CMapLayers::OnShutdown()
{
	if(m_pEggTiles)
	{
		mem_free(m_pEggTiles);
		m_pEggTiles = 0;
	}
}

void CMapLayers::LoadEnvPoints(const CLayers *pLayers, array<CEnvPoint> &lEnvPoints)
{
	lEnvPoints.clear();

	// get envelope points
	CEnvPoint *pPoints = 0x0;
	{
		int Start, Num;
		pLayers->Map()->GetType(MAPITEMTYPE_ENVPOINTS, &Start, &Num);

		if(!Num)
			return;

		pPoints = (CEnvPoint *) pLayers->Map()->GetItem(Start, 0, 0);
	}

	// get envelopes
	int Start, Num;
	pLayers->Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
	if(!Num)
		return;

	for(int env = 0; env < Num; env++)
	{
		CMapItemEnvelope *pItem = (CMapItemEnvelope *) pLayers->Map()->GetItem(Start + env, 0, 0);

		if(pItem->m_Version >= 3)
		{
			for(int i = 0; i < pItem->m_NumPoints; i++)
				lEnvPoints.add(pPoints[i + pItem->m_StartPoint]);
		}
		else
		{
			// backwards compatibility
			for(int i = 0; i < pItem->m_NumPoints; i++)
			{
				// convert CEnvPoint_v1 -> CEnvPoint
				CEnvPoint_v1 *pEnvPoint_v1 = &((CEnvPoint_v1 *) pPoints)[i + pItem->m_StartPoint];
				CEnvPoint p;

				p.m_Time = pEnvPoint_v1->m_Time;
				p.m_Curvetype = pEnvPoint_v1->m_Curvetype;

				for(int c = 0; c < minimum(pItem->m_Channels, 4); c++)
				{
					p.m_aValues[c] = pEnvPoint_v1->m_aValues[c];
					p.m_aInTangentdx[c] = 0;
					p.m_aInTangentdy[c] = 0;
					p.m_aOutTangentdx[c] = 0;
					p.m_aOutTangentdy[c] = 0;
				}

				lEnvPoints.add(p);
			}
		}
	}
}

void CMapLayers::EnvelopeEval(float TimeOffset, int Env, float *pChannels, void *pUser)
{
	CMapLayers *pThis = (CMapLayers *) pUser;
	pChannels[0] = 0;
	pChannels[1] = 0;
	pChannels[2] = 0;
	pChannels[3] = 0;

	// these are set while a map is rendered, so that the same component can
	// render both the game map and a menu map
	CLayers *pLayers = pThis->m_pEnvEvalLayers;
	array<CEnvPoint> *pEnvPoints = pThis->m_pEnvEvalPoints;
	bool IsMenuMap = pThis->m_EnvEvalIsMenuMap;

	if(!pLayers)
	{
		// called outside of a render pass, e.g. by the map sounds
		pLayers = pThis->Layers();
		pEnvPoints = &pThis->m_lEnvPoints;
		IsMenuMap = false;
	}

	if(!pLayers || !pEnvPoints)
		return;

	CEnvPoint *pPoints = pEnvPoints->base_ptr();
	if(!pPoints)
		return;

	int Start, Num;
	pLayers->Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);

	if(Env >= Num)
		return;

	const CMapItemEnvelope *pItem = (CMapItemEnvelope *) pLayers->Map()->GetItem(Start + Env, 0, 0);
	CEnvPoint *pItemPoints = pPoints + pItem->m_StartPoint;

	static float s_Time = 0.0f;
	float EnvalopTicks = (pItemPoints[pItem->m_NumPoints - 1].m_Time - pItemPoints[0].m_Time) / 1000.0f * pThis->Client()->GameTickSpeed();
	if(!IsMenuMap && (pThis->Client()->State() == IClient::STATE_ONLINE || pThis->Client()->State() == IClient::STATE_DEMOPLAYBACK))
	{
		if(pThis->m_pClient->m_Snap.m_pGameData && !pThis->m_pClient->IsWorldPaused())
		{
			if(pItem->m_Version < 2 || pItem->m_Synchronized)
			{
				float PrevAnimationTick = fmod(pThis->Client()->PrevGameTick() - pThis->m_pClient->m_Snap.m_pGameData->m_GameStartTick, EnvalopTicks);
				float CurAnimationTick = fmod(pThis->Client()->GameTick() - pThis->m_pClient->m_Snap.m_pGameData->m_GameStartTick, EnvalopTicks);
				if(PrevAnimationTick > CurAnimationTick)
					CurAnimationTick += EnvalopTicks;
				s_Time = mix(PrevAnimationTick, CurAnimationTick, pThis->Client()->IntraGameTick()) / pThis->Client()->GameTickSpeed();
			}
			else
				s_Time = pThis->Client()->LocalTime() - pThis->m_OnlineStartTime;
		}
	}
	else
	{
		s_Time = pThis->Client()->LocalTime();
	}
	CRenderTools::RenderEvalEnvelope(pItemPoints, pItem->m_NumPoints, 4, s_Time + TimeOffset, pChannels);
}

void CMapLayers::RenderLayers(CLayers *pLayers, array<CEnvPoint> *pEnvPoints, int ImageType, bool IsMenuMap, const vec2 &Center, float Zoom, float Alpha, bool LeaveScreenMapped)
{
	if(!pLayers || !pLayers->Map())
		return;

	m_pEnvEvalLayers = pLayers;
	m_pEnvEvalPoints = pEnvPoints;
	m_EnvEvalIsMenuMap = IsMenuMap;

	CUIRect Screen;
	Graphics()->GetScreen(&Screen.x, &Screen.y, &Screen.w, &Screen.h);

	if(Alpha < 1.0f)
		Graphics()->SetGlobalAlpha(Alpha);

	bool PassedGameLayer = false;
	bool Stop = false;

	for(int g = 0; g < pLayers->NumGroups() && !Stop; g++)
	{
		CMapItemGroup *pGroup = pLayers->GetGroup(g);

		if(!Config()->m_GfxNoclip && pGroup->m_Version >= 2 && pGroup->m_UseClipping)
		{
			// set clipping
			float Points[4];
			RenderTools()->MapScreenToGroup(Center.x, Center.y, pLayers->GameGroup(), Zoom);
			Graphics()->GetScreen(&Points[0], &Points[1], &Points[2], &Points[3]);
			float x0 = (pGroup->m_ClipX - Points[0]) / (Points[2] - Points[0]);
			float y0 = (pGroup->m_ClipY - Points[1]) / (Points[3] - Points[1]);
			float x1 = ((pGroup->m_ClipX + pGroup->m_ClipW) - Points[0]) / (Points[2] - Points[0]);
			float y1 = ((pGroup->m_ClipY + pGroup->m_ClipH) - Points[1]) / (Points[3] - Points[1]);

			if(x1 < 0.0f || x0 > 1.0f || y1 < 0.0f || y0 > 1.0f)
				continue;

			Graphics()->ClipEnable((int) (x0 * Graphics()->ScreenWidth()), (int) (y0 * Graphics()->ScreenHeight()),
				(int) ((x1 - x0) * Graphics()->ScreenWidth()), (int) ((y1 - y0) * Graphics()->ScreenHeight()));
		}

		RenderTools()->MapScreenToGroup(Center.x, Center.y, pGroup, Zoom);

		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = pLayers->GetLayer(pGroup->m_StartLayer + l);
			bool Render = false;
			bool IsGameLayer = false;

			if(pLayer == (CMapItemLayer *) pLayers->GameLayer())
			{
				IsGameLayer = true;
				PassedGameLayer = true;
			}

			if(m_Type == -1)
				Render = true;
			else if(m_Type == 0)
			{
				// menu maps are rendered completely by the background instance,
				// the game map is split into a background and a foreground part
				if(!IsMenuMap && PassedGameLayer)
				{
					Stop = true;
					break;
				}
				Render = true;
			}
			else
			{
				if(PassedGameLayer && !IsGameLayer)
					Render = true;
			}

			if(!Render)
				continue;

			// skip rendering if detail layers is not wanted
			if(!(pLayer->m_Flags & LAYERFLAG_DETAIL && !Config()->m_GfxHighDetail && !IsGameLayer && !IsMenuMap))
			{
				if(pLayer->m_Type == LAYERTYPE_TILES && Input()->KeyIsPressed(KEY_LCTRL) && Input()->KeyIsPressed(KEY_LSHIFT) && UI()->KeyPress(KEY_KP_0))
				{
					CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *) pLayer;
					CTile *pTiles = (CTile *) pLayers->Map()->GetData(pTMap->m_Data);
					CServerInfo CurrentServerInfo;
					Client()->GetServerInfo(&CurrentServerInfo);
					char aFilename[IO_MAX_PATH_LENGTH];
					str_format(aFilename, sizeof(aFilename), "dumps/tilelayer_dump_%s-%d-%d-%dx%d.txt", CurrentServerInfo.m_aMap, g, l, pTMap->m_Width, pTMap->m_Height);
					IOHANDLE File = Storage()->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
					if(File)
					{
						for(int y = 0; y < pTMap->m_Height; y++)
						{
							for(int x = 0; x < pTMap->m_Width; x++)
								io_write(File, &(pTiles[y * pTMap->m_Width + x].m_Index), sizeof(pTiles[y * pTMap->m_Width + x].m_Index));
							io_write_newline(File);
						}
						io_close(File);
					}
				}

				if(!IsGameLayer)
				{
					if(pLayer->m_Type == LAYERTYPE_TILES)
					{
						CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *) pLayer;
						if(pTMap->m_Image == -1)
							Graphics()->TextureClear();
						else
							Graphics()->TextureSet(m_pClient->m_pMapComponent->Images()->Get(pTMap->m_Image, false, ImageType));

						// evaluate the layer color, the tilemap shader expects it premultiplied
						float r = 1, g = 1, b = 1, a = 1;
						if(pTMap->m_ColorEnv >= 0)
						{
							float aChannels[4];
							EnvelopeEval(pTMap->m_ColorEnvOffset / 1000.0f, pTMap->m_ColorEnv, aChannels, this);
							r = aChannels[0];
							g = aChannels[1];
							b = aChannels[2];
							a = aChannels[3];
						}
						const float ColA = (pTMap->m_Color.a / 255.0f) * a;
						const float Fade = Alpha;
						const vec4 Color = vec4(
							(pTMap->m_Color.r / 255.0f) * r * ColA * Fade,
							(pTMap->m_Color.g / 255.0f) * g * ColA * Fade,
							(pTMap->m_Color.b / 255.0f) * b * ColA * Fade,
							ColA * Fade);
						const bool ColorOpaque = ColA > 254.0f / 255.0f;
						// blending has to stay on while the whole map fades
						const bool OpaquePass = ColorOpaque && Fade >= 1.0f && Config()->m_GfxTileOpaquePass;

						int LayerIndex = -1;
						IGraphics::CTextureHandle TileData;
						// layers without a tileset or without tile data are drawn as solid color by the CPU path
						if(pTMap->m_Image != -1 && pTMap->m_Data >= 0 && Config()->m_GfxTileBuffering && Graphics()->TilemapShaderEnabled())
							TileData = m_pClient->m_pMapComponent->GetTilemapTexture(pLayers, pTMap, &LayerIndex);

						if(TileData.IsValid() && LayerIndex >= 0)
						{
							Graphics()->WrapClamp();
							if(Config()->m_GfxTileDebug)
							{
								Graphics()->BlendNone();
								Graphics()->RenderTilemapTexture(TileData, LayerIndex, pTMap->m_Width, pTMap->m_Height, IGraphics::TILEMAP_PASS_DATA_DEBUG, true, vec4(1, 1, 1, 1));
							}
							else if(OpaquePass)
							{
								Graphics()->BlendNone();
								Graphics()->RenderTilemapTexture(TileData, LayerIndex, pTMap->m_Width, pTMap->m_Height, IGraphics::TILEMAP_PASS_OPAQUE, ColorOpaque, Color);
								Graphics()->BlendNormal();
								Graphics()->RenderTilemapTexture(TileData, LayerIndex, pTMap->m_Width, pTMap->m_Height, IGraphics::TILEMAP_PASS_TRANSPARENT, ColorOpaque, Color);
							}
							else
							{
								Graphics()->BlendNormal();
								Graphics()->RenderTilemapTexture(TileData, LayerIndex, pTMap->m_Width, pTMap->m_Height, IGraphics::TILEMAP_PASS_ALL, ColorOpaque, Color);
							}
							Graphics()->WrapNormal();
						}
						else
						{
							// CPU fallback for backends without the tilemap shader
							CTile *pTiles = (CTile *) pLayers->Map()->GetData(pTMap->m_Data);
							vec4 BaseColor = vec4(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f);
							if(Fade < 1.0f)
								Graphics()->BlendNormal();
							else
								Graphics()->BlendNone();
							RenderTools()->RenderTilemap(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, BaseColor, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_OPAQUE,
								EnvelopeEval, this, pTMap->m_ColorEnv, pTMap->m_ColorEnvOffset);
							Graphics()->BlendNormal();
							RenderTools()->RenderTilemap(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, BaseColor, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_TRANSPARENT,
								EnvelopeEval, this, pTMap->m_ColorEnv, pTMap->m_ColorEnvOffset);
						}
					}
					else if(pLayer->m_Type == LAYERTYPE_QUADS)
					{
						CMapItemLayerQuads *pQLayer = (CMapItemLayerQuads *) pLayer;
						if(pQLayer->m_Image == -1)
							Graphics()->TextureClear();
						else
							Graphics()->TextureSet(m_pClient->m_pMapComponent->Images()->Get(pQLayer->m_Image, true, ImageType));

						CQuad *pQuads = (CQuad *) pLayers->Map()->GetDataSwapped(pQLayer->m_Data);

						// Graphics()->BlendNone();
						// RenderTools()->RenderQuads(pQuads, pQLayer->m_NumQuads, LAYERRENDERFLAG_OPAQUE, EnvelopeEval, this);
						Graphics()->BlendNormal();
						RenderTools()->RenderQuads(pQuads, pQLayer->m_NumQuads, LAYERRENDERFLAG_TRANSPARENT, EnvelopeEval, this);
					}
				}
			}

			// eggs
			if(m_pClient->IsEaster())
			{
				CMapItemLayer *pNextLayer = pLayers->GetLayer(pGroup->m_StartLayer + l + 1);
				if(m_pEggTiles && (l + 1) < pGroup->m_NumLayers && pNextLayer == (CMapItemLayer *) pLayers->GameLayer())
				{
					Graphics()->TextureSet(m_pClient->m_pMapComponent->Images()->GetEasterTexture());
					Graphics()->BlendNormal();
					RenderTools()->RenderTilemap(m_pEggTiles, m_EggLayerWidth, m_EggLayerHeight, 32.0f, vec4(1, 1, 1, 1), LAYERRENDERFLAG_TRANSPARENT, EnvelopeEval, this, -1, 0);
				}
			}
		}
		if(!Config()->m_GfxNoclip)
			Graphics()->ClipDisable();
	}

	if(!Config()->m_GfxNoclip)
		Graphics()->ClipDisable();

	Graphics()->SetGlobalAlpha(1.0f);

	// the game map leaves the screen mapped to the game group, the entity renderers
	// rely on that, so only restore it for everything else
	if(!LeaveScreenMapped)
		Graphics()->MapScreen(Screen.x, Screen.y, Screen.w, Screen.h);

	m_pEnvEvalLayers = 0;
	m_pEnvEvalPoints = 0;
}

void CMapLayers::RenderMenuMap(int Slot)
{
	SMenuMapSlot &Map = m_aMenuMaps[Slot];
	if(!Map.m_Loaded || Map.m_Alpha <= 0.0f)
		return;

	const bool Online = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	const vec2 Center = Online ? *m_pClient->m_pCamera->GetMenuCenter() : *m_pClient->m_pCamera->GetCenter();
	const float Zoom = Online ? m_pClient->m_pCamera->GetMenuZoom() : m_pClient->m_pCamera->GetZoom();
	RenderLayers(Map.m_pLayers, &Map.m_lEnvPoints, Map.m_ImageType, true, Center, Zoom, Map.m_Alpha, false);
}

void CMapLayers::RenderGameMap(float Alpha)
{
	// the camera is already back at the menu position, so the game map has to
	// be rendered with the camera it was last seen with
	RenderLayers(Layers(), &m_lEnvPoints, CMapImages::MAP_TYPE_GAME, false, *m_pClient->m_pCamera->GetGameCenter(), m_pClient->m_pCamera->GetGameZoom(), Alpha, false);
}

bool CMapLayers::GetMenuMapGameBounds(vec2 *pMin, vec2 *pMax) const
{
	if(m_CurrentMenuMap < 0)
		return false;

	const SMenuMapSlot &Map = m_aMenuMaps[m_CurrentMenuMap];
	if(!Map.m_Loaded || !Map.m_pLayers)
		return false;

	const CMapItemLayerTilemap *pGameLayer = Map.m_pLayers->GameLayer();
	if(!pGameLayer)
		return false;

	*pMin = vec2(0.0f, 0.0f);
	*pMax = vec2(pGameLayer->m_Width * 32.0f, pGameLayer->m_Height * 32.0f);
	return true;
}

void CMapLayers::OnRender()
{
	CMapComponent *pMap = m_pClient->m_pMapComponent;

	if(m_Type == TYPE_BACKGROUND)
		UpdateMenuMapFade();

	const bool Online = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;

	if(Online)
	{
		// game map first, it must leave the screen mapped to the game group
		RenderLayers(Layers(), &m_lEnvPoints, CMapImages::MAP_TYPE_GAME, false, *m_pClient->m_pCamera->GetCenter(), m_pClient->m_pCamera->GetZoom(), 1.0f, m_Type == TYPE_BACKGROUND);

		// the menu map is still visible while it fades out over the game map
		if(m_Type == TYPE_BACKGROUND)
		{
			if(m_CurrentMenuMap >= 0)
				RenderMenuMap(m_CurrentMenuMap);
			if(m_PrevMenuMap >= 0)
				RenderMenuMap(m_PrevMenuMap);
		}
		return;
	}

	if(m_Type != TYPE_BACKGROUND)
	{
		// the foreground part of the game map also fades out on top of the menu map
		if(pMap->GameMapVisible())
			RenderGameMap(pMap->GameMapAlpha());
		return;
	}

	// offline: the menu map or the animated background is the base layer and
	// the map of the game we just left fades out on top of it
	if(!MenuMapOpaque())
		pMap->RenderBackground(Client()->LocalTime());
	if(m_CurrentMenuMap >= 0)
		RenderMenuMap(m_CurrentMenuMap);
	if(m_PrevMenuMap >= 0)
		RenderMenuMap(m_PrevMenuMap);
	if(pMap->GameMapVisible())
		RenderGameMap(pMap->GameMapAlpha());
}

void CMapLayers::ConchainBackgroundMap(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CMapLayers *) pUserData)->BackgroundMapUpdate();
}

void CMapLayers::ConchainTileBuffering(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CMapLayers *pSelf = (CMapLayers *) pUserData;
		pSelf->m_pClient->m_pMapComponent->ClearTilemapTextures();
	}
}

void CMapLayers::OnConsoleInit()
{
	Console()->Chain("cl_menu_map", ConchainBackgroundMap, this);
	Console()->Chain("cl_show_menu_map", ConchainBackgroundMap, this);
	Console()->Chain("gfx_tile_buffering", ConchainTileBuffering, this);
}

void CMapLayers::BackgroundMapUpdate()
{
	if(m_Type != TYPE_BACKGROUND || !m_aMenuMaps[0].m_pMap)
		return;

	const bool Online = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(Online)
	{
		// not visible while playing, reload in place
		if(m_CurrentMenuMap < 0)
			m_CurrentMenuMap = 0;
		UnloadMenuMap(m_CurrentMenuMap);
		if(Config()->m_ClShowMenuMap && LoadMenuMap(m_CurrentMenuMap))
			m_aMenuMaps[m_CurrentMenuMap].m_Alpha = 0.0f;
		else
			m_CurrentMenuMap = -1;
		return;
	}

	// cross-fade: load the new map into the free slot and let the old one fade out
	FinalizeMenuMapFade();

	const int OldSlot = m_CurrentMenuMap;
	const int NewSlot = m_CurrentMenuMap == 0 ? 1 : 0;
	UnloadMenuMap(NewSlot);

	if(Config()->m_ClShowMenuMap && LoadMenuMap(NewSlot))
	{
		// fade in over the animated background when there is no old map to fade out
		m_MenuMapFadeIn = OldSlot < 0;
		m_aMenuMaps[NewSlot].m_Alpha = m_MenuMapFadeIn ? 0.0f : 1.0f;
		m_CurrentMenuMap = NewSlot;
	}
	else
	{
		// switching to no map: the animated background becomes the base layer
		// and the old map fades out on top of it
		m_CurrentMenuMap = -1;
	}

	if(OldSlot >= 0 && m_aMenuMaps[OldSlot].m_Loaded)
	{
		m_aMenuMaps[OldSlot].m_Alpha = 1.0f;
		m_PrevMenuMap = OldSlot;
	}
}

static void PlaceEggDoodads(int LayerWidth, int LayerHeight, CTile *aOutTiles, CTile *aGameLayerTiles, int ItemWidth, int ItemHeight, const int *aImageTileID, int ImageTileIDCount, int Freq)
{
	for(int y = 0; y < LayerHeight - ItemHeight; y++)
	{
		for(int x = 0; x < LayerWidth - ItemWidth; x++)
		{
			bool Overlap = false;
			bool ObstructedByWall = false;
			bool HasGround = true;

			for(int iy = 0; iy < ItemHeight; iy++)
			{
				for(int ix = 0; ix < ItemWidth; ix++)
				{
					int Tid = (y + iy) * LayerWidth + (x + ix);
					int DownTid = (y + iy + 1) * LayerWidth + (x + ix);

					if(aOutTiles[Tid].m_Index != 0)
					{
						Overlap = true;
						break;
					}

					if(aGameLayerTiles[Tid].m_Index == 1)
					{
						ObstructedByWall = true;
						break;
					}

					if(iy == ItemHeight - 1 && aGameLayerTiles[DownTid].m_Index != 1)
					{
						HasGround = false;
						break;
					}
				}
			}

			if(!Overlap && !ObstructedByWall && HasGround && random_int() % Freq == 0)
			{
				const int BaskerStartID = aImageTileID[random_int() % ImageTileIDCount];

				for(int iy = 0; iy < ItemHeight; iy++)
				{
					for(int ix = 0; ix < ItemWidth; ix++)
					{
						int Tid = (y + iy) * LayerWidth + (x + ix);
						aOutTiles[Tid].m_Index = BaskerStartID + iy * 16 + ix;
					}
				}
			}
		}
	}
}

void CMapLayers::PlaceEasterEggs(const CLayers *pLayers)
{
	CMapItemLayerTilemap *pGameLayer = pLayers->GameLayer();
	if(m_pEggTiles)
		mem_free(m_pEggTiles);

	m_EggLayerWidth = pGameLayer->m_Width;
	m_EggLayerHeight = pGameLayer->m_Height;
	int DataSize = sizeof(CTile) * m_EggLayerWidth * m_EggLayerHeight;
	m_pEggTiles = (CTile *) mem_alloc(DataSize);
	mem_zero(m_pEggTiles, DataSize);
	CTile *pGameLayerTiles = (CTile *) pLayers->Map()->GetData(pGameLayer->m_Data);

	// first pass: baskets
	static const int s_aBasketIDs[] = {
		38,
		86};

	static const int s_BasketCount = sizeof(s_aBasketIDs) / sizeof(s_aBasketIDs[0]);
	PlaceEggDoodads(m_EggLayerWidth, m_EggLayerHeight, m_pEggTiles, pGameLayerTiles, 3, 2, s_aBasketIDs, s_BasketCount, 250);

	// second pass: double eggs
	static const int s_aDoubleEggIDs[] = {
		9,
		25,
		41,
		57,
		73,
		89};

	static const int s_DoubleEggCount = sizeof(s_aDoubleEggIDs) / sizeof(s_aDoubleEggIDs[0]);
	PlaceEggDoodads(m_EggLayerWidth, m_EggLayerHeight, m_pEggTiles, pGameLayerTiles, 2, 1, s_aDoubleEggIDs, s_DoubleEggCount, 100);

	// third pass: eggs
	static const int s_aEggIDs[] = {
		1, 2, 3, 4, 5,
		17, 18, 19, 20,
		33, 34, 35, 36,
		49, 50, 52,
		65, 66,
		82,
		98};

	static const int s_EggCount = sizeof(s_aEggIDs) / sizeof(s_aEggIDs[0]);
	PlaceEggDoodads(m_EggLayerWidth, m_EggLayerHeight, m_pEggTiles, pGameLayerTiles, 1, 1, s_aEggIDs, s_EggCount, 30);
}
