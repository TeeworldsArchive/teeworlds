#include "steam.h"

#include <base/system.h>

#if defined(CONF_STEAM)
#include <steam/steam_api.h>
#endif

CSteamPresence::CSteamPresence()
{
	m_Initialized = false;
	m_LastMode = MODE_NONE;
	m_aLastGameType[0] = 0;
	m_aLastMap[0] = 0;
	m_aLastGroup[0] = 0;
	m_LastGroupSize = 0;
}

CSteamPresence::~CSteamPresence()
{
#if defined(CONF_STEAM)
	if(m_Initialized)
	{
		SteamFriends()->ClearRichPresence();
		SteamAPI_Shutdown();
	}
#endif
}

void CSteamPresence::Init()
{
#if defined(CONF_STEAM)
	if(SteamAPI_Init())
	{
		m_Initialized = true;
		dbg_msg("steam", "steamworks initialized, rich presence enabled");
	}
	else
	{
		// Not fatal: the game was most likely started without the Steam
		// client, so simply do not advertise anything.
		dbg_msg("steam", "SteamAPI_Init failed, rich presence disabled");
	}
#endif
}

void CSteamPresence::RunCallbacks()
{
#if defined(CONF_STEAM)
	if(m_Initialized)
		SteamAPI_RunCallbacks();
#endif
}

void CSteamPresence::SetPresence(int Mode, const char *pGameType, const char *pMapName, const char *pGroup, int GroupSize)
{
	// SetRichPresence(key, "") removes the key, and Steam then refuses to render
	// a token whose `%key%` substitution cannot be resolved, so never send an
	// empty value for the keys the localization file depends on.
	if(!pGameType || !pGameType[0])
		pGameType = "Unknown";
	if(!pMapName || !pMapName[0])
		pMapName = "Unknown";
	if(!pGroup)
		pGroup = "";
	// A group of one is not a group: Steam would advertise the player as
	// "playing with" nobody, so only report groups with somebody else in them.
	if(Mode != MODE_INGAME || !pGroup[0] || GroupSize < 2)
	{
		pGroup = "";
		GroupSize = 0;
	}

	if(Mode == m_LastMode && str_comp(pGameType, m_aLastGameType) == 0 && str_comp(pMapName, m_aLastMap) == 0 &&
		str_comp(pGroup, m_aLastGroup) == 0 && GroupSize == m_LastGroupSize)
		return;

	m_LastMode = Mode;
	str_copy(m_aLastGameType, pGameType, sizeof(m_aLastGameType));
	str_copy(m_aLastMap, pMapName, sizeof(m_aLastMap));
	str_copy(m_aLastGroup, pGroup, sizeof(m_aLastGroup));
	m_LastGroupSize = GroupSize;

#if defined(CONF_STEAM)
	if(!m_Initialized)
		return;

	ISteamFriends *pFriends = SteamFriends();
	if(Mode == MODE_INGAME)
	{
		pFriends->SetRichPresence("steam_display", "#Status");
		pFriends->SetRichPresence("gamemode", m_aLastGameType);
		pFriends->SetRichPresence("mapname", m_aLastMap);
		if(m_aLastGroup[0])
		{
			char aGroupSize[16];
			str_format(aGroupSize, sizeof(aGroupSize), "%d", m_LastGroupSize);
			pFriends->SetRichPresence("steam_player_group", m_aLastGroup);
			pFriends->SetRichPresence("steam_player_group_size", aGroupSize);
		}
		else
		{
			// Everybody else left (or joined): drop the grouping again.
			pFriends->SetRichPresence("steam_player_group", "");
			pFriends->SetRichPresence("steam_player_group_size", "");
		}
	}
	else if(Mode == MODE_DEMO)
	{
		// Drop the map/mode keys so a later state cannot pick up stale values.
		pFriends->ClearRichPresence();
		pFriends->SetRichPresence("steam_display", "#WatchingDemo");
	}
	else
	{
		pFriends->ClearRichPresence();
	}
#endif
}
