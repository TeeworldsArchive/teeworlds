#ifndef ENGINE_CLIENT_STEAM_H
#define ENGINE_CLIENT_STEAM_H

/*
	Steam rich presence.

	While the player is connected to a server the Steam friends list shows what
	they are playing; during demo playback that they are watching a replay. In
	every other state (menus, loading screen, server browser, editor, ...) no
	rich presence is set at all.

*/
class CSteamPresence
{
public:
	enum
	{
		MODE_NONE = 0, // no presence at all
		MODE_INGAME, // "playing <map> in <gamemode>"
		MODE_DEMO, // "watching a replay"
	};

	CSteamPresence();
	~CSteamPresence();

	// Initializes the Steamworks API. Failure (no Steam client, game started
	// outside of Steam, missing library) only disables rich presence.
	void Init();

	// Pumps the Steamworks callbacks. Has to be called once per frame from the
	// same thread that called Init().
	void RunCallbacks();

	// Pushes the desired presence to Steam, skipping unchanged states. pGroup
	// identifies the Steam player group (the server that is being played on)
	// and GroupSize is the number of people on it; the group is dropped when it
	// would only contain the player itself.
	void SetPresence(int Mode, const char *pGameType, const char *pMapName, const char *pGroup, int GroupSize);

private:
	bool m_Initialized;
	int m_LastMode;
	char m_aLastGameType[32];
	char m_aLastMap[64];
	char m_aLastGroup[64];
	int m_LastGroupSize;
};

#endif
