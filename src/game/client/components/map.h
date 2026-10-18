/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#ifndef GAME_CLIENT_COMPONENTS_MAP_H
#define GAME_CLIENT_COMPONENTS_MAP_H
#include <game/client/component.h>

#include "mapimages.h"
#include "maplayers.h"
#include "mapsounds.h"

class IEngineMap;

// Owns the client side map resources (images, layers, sounds) and their
// lifetime: the engine keeps the map loaded after a disconnect so it can fade
// out, and this component releases it with IClient::UnloadMap() when done.
class CMapComponent : public CComponent
{
	enum
	{
		GAME_MAP_FADE_TIME = 250, // ms
	};

	// the map we just left, drawn with the view offset between the two maps
	struct SFadeMap
	{
		IEngineMap *m_pMap;
		class CLayers *m_pLayers;
		array<CEnvPoint> m_lEnvPoints;
		int m_ImageType;
		float m_Alpha;
		vec2 m_Offset;
		bool m_HasOffset;
		bool m_Loaded;
		// set until the first new-world snapshot
		bool m_StartPending;
		// drop it at that snapshot instead of fading
		bool m_HoldOnly;
		float m_PendingTime;
	};
	SFadeMap m_FadeMap;

	CMapImages m_Images;
	CMapLayers m_LayersBackground;
	CMapLayers m_LayersForeground;
	CMapSounds m_Sounds;

	// the game map is kept loaded while it fades out after a disconnect
	bool m_GameMapLoaded;
	bool m_GameMapFading;
	float m_GameMapAlpha;

	// tile data: one 2D array per (width, height), one layer per tile layer
	struct STilemapTexture
	{
		const CLayers *m_pLayers;
		int m_Width;
		int m_Height;
		IGraphics::CTextureHandle m_Texture;
		array<const CMapItemLayerTilemap *> m_lLayers;
	};
	array<STilemapTexture> m_lTilemapTextures;

	void ReleaseGameMap();
	void ReleaseTilemapTextures(const CLayers *pLayers);

public:
	CMapComponent();

	// keep the old map visible; HoldOnly drops it at the new-world snapshot
	void BeginGameMapFade(const char *pMapPath, bool HoldOnly);
	// the first new-world snapshot is in, start the fade
	void StartGameMapFade();
	// remember how far the camera view jumped on the switch
	void SetFadeMapOffset(const vec2 &Offset);
	void EndGameMapFade();

	bool FadeMapVisible() const { return m_FadeMap.m_Loaded && m_FadeMap.m_Alpha > 0.0f; }
	CLayers *FadeMapLayers() { return m_FadeMap.m_pLayers; }
	array<CEnvPoint> *FadeMapEnvPoints() { return &m_FadeMap.m_lEnvPoints; }
	int FadeMapImageType() const { return m_FadeMap.m_ImageType; }
	float FadeMapAlpha() const { return m_FadeMap.m_Alpha; }
	// live view, shifted by the offset once it is known
	vec2 FadeMapCenter() const;
	float FadeMapZoom() const;


	// returns the tile data texture for the layer and writes its array layer index
	IGraphics::CTextureHandle GetTilemapTexture(const CLayers *pLayers, const CMapItemLayerTilemap *pLayer, int *pLayerIndex);
	void ClearTilemapTextures();

	CMapImages *Images() { return &m_Images; }
	CMapLayers *LayersBackground() { return &m_LayersBackground; }
	CMapLayers *LayersForeground() { return &m_LayersForeground; }
	CMapSounds *Sounds() { return &m_Sounds; }

	// whether the map of the game we are in (or just left) has to be rendered
	bool GameMapVisible() const { return m_GameMapLoaded && m_GameMapAlpha > 0.0f; }
	float GameMapAlpha() const { return m_GameMapAlpha; }

	// the animated fallback background, shown when no menu map is loaded
	void RenderBackground(float Time);

	virtual void OnMapLoad() override;
	virtual void OnMapUnload() override;
	virtual void OnStateChange(int NewState, int OldState) override;
	virtual void OnRender() override;
	virtual void OnShutdown() override;
};

#endif
