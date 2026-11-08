/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#ifndef ENGINE_STEAMFRIENDS_H
#define ENGINE_STEAMFRIENDS_H

#include <base/system/string.h>

#include "kernel.h"

/*
	A read-only view of the Steam friends list.

	The interface is registered in every build. Without CONF_STEAM, or when
	Steamworks could not be initialized, Available() returns false and the list
	stays empty, so the menus can query it unconditionally.
*/
class CSteamFriendInfo
{
public:
	enum
	{
		STATE_OFFLINE = 0,
		STATE_ONLINE,
		STATE_BUSY,
		STATE_AWAY,
		STATE_SNOOZE,
		STATE_LOOKING_TO_TRADE,
		STATE_LOOKING_TO_PLAY,
	};

	unsigned long long m_SteamID;
	char m_aName[128];
	// values above, matching EPersonaState of the Steamworks SDK
	int m_State;

	// set when Steam reports that the friend is running this game
	bool m_PlayingThisGame;
	// set when the friend is playing some other Steam game
	bool m_PlayingOtherGame;
	char m_aGameType[32];
	char m_aMap[64];
	// address of the server the friend is playing on, empty if Steam does not
	// know it (our rich presence only publishes a group when somebody else is
	// on the same server)
	char m_aServerAddress[64];

	bool IsOnline() const { return m_State != STATE_OFFLINE; }

	bool operator<(const CSteamFriendInfo &Other) const
	{
		// friends playing this game come first, then the online ones, and
		// everybody is sorted by name within those groups
		if(m_PlayingThisGame != Other.m_PlayingThisGame)
			return m_PlayingThisGame;
		if(IsOnline() != Other.IsOnline())
			return IsOnline();
		return str_comp_nocase(m_aName, Other.m_aName) < 0;
	}

	bool operator==(const CSteamFriendInfo &Other) const { return m_SteamID == Other.m_SteamID; }
};

class ISteamFriendList : public IInterface
{
	MACRO_INTERFACE("steamfriends", 0)
public:
	// False without a Steam build or when Steam is not running.
	virtual bool Available() const = 0;

	// Rebuilds the list, at most a few times per second.
	virtual void Refresh() = 0;
	virtual int NumFriends() const = 0;
	virtual const CSteamFriendInfo *GetFriend(int Index) const = 0;

	// Raw RGBA (width * height * 4) of the friend's Steam avatar. Revision
	// changes whenever the image changed, so callers can cache the uploaded
	// texture. Returns false while Steam has not loaded the avatar yet.
	virtual bool GetAvatar(unsigned long long SteamID, int *pWidth, int *pHeight, const unsigned char **pData, int *pRevision) = 0;

	// Raw bytes of the friend's animated avatar, a GIF as served by Steam.
	// Animated avatars are Steam Community items: Steam only keeps a still
	// frame in the avatar image handles, so the animation has to be fetched
	// from the URL of the equipped item. The engine does that in the
	// background; this returns false until the download is done and for
	// friends without an animated avatar. Revision changes whenever a
	// different image was downloaded, so callers can cache the decoded frames.
	virtual bool GetAnimatedAvatar(unsigned long long SteamID, const unsigned char **pData, int *pSize, int *pRevision) = 0;
};

#endif
