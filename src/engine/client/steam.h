/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#ifndef ENGINE_CLIENT_STEAM_H
#define ENGINE_CLIENT_STEAM_H

#include <base/system/time.h>
#include <base/tl/array.h>
#include <base/tl/sorted_array.h>

#include <engine/steamfriends.h>

class IEngine;
class CSteamAvatarFetcher;

/*
	Steam integration.

	Rich presence: while the player is connected to a server the Steam friends
	list shows what they are playing; during demo playback that they are
	watching a replay. In every other state (menus, loading screen, server
	browser, ...) no rich presence is set at all.

	Friends: the same object exposes the Steam friends list and their avatars
	through ISteamFriendList, so the menus can show where the friends are
	playing without talking to the Steamworks API directly.
*/
class CSteamPresence : public ISteamFriendList
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
	// outside of Steam, missing library) only disables rich presence and the
	// friends list. The engine is only used to run the avatar downloads in the
	// background.
	void Init(IEngine *pEngine);

	// Pumps the Steamworks callbacks. Has to be called once per frame from the
	// same thread that called Init().
	void RunCallbacks();

	// Pushes the desired presence to Steam, skipping unchanged states. pGroup
	// identifies the Steam player group (the server that is being played on)
	// and GroupSize is the number of people on it; the group is dropped when it
	// would only contain the player itself.
	void SetPresence(int Mode, const char *pGameType, const char *pMapName, const char *pGroup, int GroupSize);

	// ISteamFriendList
	bool Available() const override;
	void Refresh() override;
	int NumFriends() const override;
	const CSteamFriendInfo *GetFriend(int Index) const override;
	bool GetAvatar(unsigned long long SteamID, int *pWidth, int *pHeight, const unsigned char **pData, int *pRevision) override;
	bool GetAnimatedAvatar(unsigned long long SteamID, const unsigned char **pData, int *pSize, int *pRevision) override;

private:
	// Steam hands out avatar images as opaque handles; the RGBA data behind
	// them is copied once and kept until the handle changes.
	class CStaticAvatar
	{
	public:
		enum
		{
			STATE_NONE = 0,
			STATE_READY,
		};

		unsigned long long m_SteamID;
		int m_State;
		int m_Handle;
		int m_Width;
		int m_Height;
		int m_Revision;
		array<unsigned char> m_lData;

		CStaticAvatar() :
			m_SteamID(0), m_State(STATE_NONE), m_Handle(-1), m_Width(0), m_Height(0), m_Revision(0) {}
	};

	bool m_Initialized;
	int m_LastMode;
	char m_aLastGameType[32];
	char m_aLastMap[64];
	char m_aLastGroup[64];
	int m_LastGroupSize;

	sorted_array<CSteamFriendInfo> m_lFriends;
	array<CStaticAvatar> m_lStaticAvatars;
	int64 m_LastFriendsRefresh;

	// Owns the Steam Community item lookups and the GIF downloads; only
	// allocated once Steamworks is initialized.
	CSteamAvatarFetcher *m_pAvatarFetcher;

	CStaticAvatar *GetStaticAvatar(unsigned long long SteamID);
};

#endif
