/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MAPLAYERS_H
#define GAME_CLIENT_COMPONENTS_MAPLAYERS_H
#include <base/tl/array.h>
#include <game/client/component.h>
#include <game/mapitems.h>

class CMapLayers : public CComponent
{
	enum
	{
		// menu maps can cross-fade, so one map can fade out while the next fades in
		MAX_MENU_MAP_SLOTS = 2,

		// duration of a menu map cross-fade in ms
		MENU_MAP_FADE_TIME = 250,
	};

	struct SMenuMapSlot
	{
		IEngineMap *m_pMap;
		CLayers *m_pLayers;
		int m_ImageType;
		array<CEnvPoint> m_lEnvPoints;
		float m_Alpha;
		bool m_Loaded;
	};

	SMenuMapSlot m_aMenuMaps[MAX_MENU_MAP_SLOTS];
	int m_CurrentMenuMap; // index of the visible menu map, -1 if none
	int m_PrevMenuMap; // index of the menu map fading out, -1 if none
	bool m_MenuMapFadeIn; // the current menu map fades in over the animated background

	int m_Type;
	float m_OnlineStartTime;

	array<CEnvPoint> m_lEnvPoints;

	// set right before a map is rendered, used by EnvelopeEval
	CLayers *m_pEnvEvalLayers;
	array<CEnvPoint> *m_pEnvEvalPoints;
	bool m_EnvEvalIsMenuMap;

	CTile *m_pEggTiles;
	int m_EggLayerWidth;
	int m_EggLayerHeight;

	void LoadEnvPoints(const CLayers *pLayers, array<CEnvPoint> &lEnvPoints);
	bool LoadMenuMap(int Slot);
	void UnloadMenuMap(int Slot);
	void FinalizeMenuMapFade();
	void UpdateMenuMapFade();

	void RenderLayers(CLayers *pLayers, array<CEnvPoint> *pEnvPoints, int ImageType, bool IsMenuMap, const vec2 &Center, float Zoom, float Alpha, bool LeaveScreenMapped);
	void RenderMenuMap(int Slot);
	void RenderGameMap(float Alpha);

	void PlaceEasterEggs(const CLayers *pLayers);

public:
	enum
	{
		TYPE_BACKGROUND = 0,
		TYPE_FOREGROUND,
	};

	CMapLayers(int Type);
	virtual void OnStateChange(int NewState, int OldState);
	virtual int GetInitAmount() const;
	virtual void OnInit();
	virtual void OnShutdown();
	virtual void OnRender();
	virtual void OnMapLoad();
	virtual void OnMapUnload();

	static void ConchainBackgroundMap(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainTileBuffering(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	virtual void OnConsoleInit();

	void BackgroundMapUpdate();

	// whether a menu map fully covers the screen, i.e. the animated background
	// does not have to be rendered underneath it
	bool MenuMapOpaque() const { return m_CurrentMenuMap >= 0 && m_aMenuMaps[m_CurrentMenuMap].m_Loaded && m_aMenuMaps[m_CurrentMenuMap].m_Alpha >= 1.0f; }

	static void EnvelopeEval(float TimeOffset, int Env, float *pChannels, void *pUser);
};

#endif
