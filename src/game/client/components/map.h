/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MAP_H
#define GAME_CLIENT_COMPONENTS_MAP_H
#include <game/client/component.h>

#include "mapimages.h"
#include "maplayers.h"
#include "mapsounds.h"

// Owns the client side map resources (images, layers, sounds) and their
// lifetime: the engine keeps the map loaded after a disconnect so it can fade
// out, and this component releases it with IClient::UnloadMap() when done.
class CMapComponent : public CComponent
{
	enum
	{
		GAME_MAP_FADE_TIME = 250, // ms
	};

	CMapImages m_Images;
	CMapLayers m_LayersBackground;
	CMapLayers m_LayersForeground;
	CMapSounds m_Sounds;

	// the game map is kept loaded while it fades out after a disconnect
	bool m_GameMapLoaded;
	bool m_GameMapFading;
	float m_GameMapAlpha;

	void ReleaseGameMap();

public:
	CMapComponent();

	CMapImages *Images() { return &m_Images; }
	CMapLayers *LayersBackground() { return &m_LayersBackground; }
	CMapLayers *LayersForeground() { return &m_LayersForeground; }
	CMapSounds *Sounds() { return &m_Sounds; }

	// whether the map of the game we are in (or just left) has to be rendered
	bool GameMapVisible() const { return m_GameMapLoaded && m_GameMapAlpha > 0.0f; }
	float GameMapAlpha() const { return m_GameMapAlpha; }

	// the animated fallback background, shown when no menu map is loaded
	void RenderBackground(float Time);

	virtual void OnMapLoad();
	virtual void OnMapUnload();
	virtual void OnStateChange(int NewState, int OldState);
	virtual void OnRender();
};

#endif
