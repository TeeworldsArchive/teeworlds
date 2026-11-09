/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_STEAMFRIENDS_H
#define GAME_CLIENT_COMPONENTS_STEAMFRIENDS_H

#include <base/tl/array.h>

#include <engine/graphics.h>
#include <engine/steamfriends.h>

#include <game/client/component.h>
#include <game/client/gif_animation.h>

/*
	Steam friends and their avatars for the start menu.

	The friends themselves come from the engine (ISteamFriendList), which is a
	stub without a Steam build. An animated avatar is a Steam Community item
	that Steam only serves as a GIF, so the engine downloads that file and the
	frames are decoded in the background and uploaded here. The still avatar
	Steam keeps for every friend is shown until the download finishes and for
	friends without an animated one.
*/
class CSteamFriends : public CComponent
{
public:
	CSteamFriends();
	~CSteamFriends();

	bool Available() const;
	void Refresh();
	int NumFriends() const;
	const CSteamFriendInfo *GetFriend(int Index) const;

	// Avatar of a friend, animated if Steam has an animated one for them.
	// May return 0 while Steam has not loaded it yet.
	CGifAnimation *Avatar(const CSteamFriendInfo *pInfo);

	virtual void OnInit() override;
	virtual void OnReset() override;
	virtual void OnShutdown() override;

private:
	enum
	{
		SOURCE_NONE = 0,
		SOURCE_GIF,
		SOURCE_STATIC,
	};

	enum
	{
		// Starting to load an avatar is quick on its own, but a whole page of
		// new friends arrives at once and the sum shows up as a hitch. Only a
		// short slice of every frame is spent on starting loads; the rows that
		// do not make it keep their placeholder for a frame or two longer.
		AVATAR_LOAD_BUDGET_MS = 1,
	};

	class CAvatar
	{
	public:
		unsigned long long m_SteamID;
		// what m_Animation currently holds and which revision of that source;
		// an image is only uploaded again when Steam changed it
		int m_Source;
		int m_GifRevision;
		int m_StaticRevision;
		// every frame is a texture of its own, so the frames of avatars that
		// are no longer drawn are given back
		int64 m_LastUsed;
		CGifAnimation m_Animation;

		CAvatar(CGifDecoder *pDecoder) :
			m_SteamID(0), m_Source(SOURCE_NONE), m_GifRevision(-1), m_StaticRevision(-1), m_LastUsed(0), m_Animation(pDecoder) {}
	};

	ISteamFriendList *m_pSteamFriendList;
	// decodes the frames of the animated avatars away from the main thread;
	// created when the first avatar is drawn
	CGifDecoder *m_pDecoder;
	// pointers because avatars own textures and must not be copied or move
	array<CAvatar *> m_lAvatars;
	// when the current frame started spending on avatar loading
	int64 m_LoadBudgetStart;

	CAvatar *GetAvatarEntry(unsigned long long SteamID);
	CGifDecoder *Decoder();
	bool LoadBudgetLeft(int64 Now) const;
	void ResetAvatars();
};

#endif