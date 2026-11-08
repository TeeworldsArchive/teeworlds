/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include "steamfriends.h"

#include <base/system/time.h>

#include <engine/kernel.h>

// An animated avatar is a texture per frame, so only the ones that are drawn
// are kept; a long friends list would otherwise hold on to a lot of frames.
static const int MAX_CACHED_AVATARS = 16;

CSteamFriends::CSteamFriends()
{
	m_pSteamFriendList = 0;
}

void CSteamFriends::OnInit()
{
	m_pSteamFriendList = Kernel()->RequestInterface<ISteamFriendList>();
}

void CSteamFriends::OnReset()
{
	ResetAvatars();
}

void CSteamFriends::OnShutdown()
{
	ResetAvatars();
}

bool CSteamFriends::Available() const
{
	return m_pSteamFriendList && m_pSteamFriendList->Available();
}

void CSteamFriends::Refresh()
{
	if(m_pSteamFriendList)
		m_pSteamFriendList->Refresh();

	// Forget avatars of friends that left the list; their textures would only
	// be kept around for nothing.
	for(int i = 0; i < m_lAvatars.size();)
	{
		bool Found = false;
		for(int f = 0; f < NumFriends() && !Found; f++)
		{
			const CSteamFriendInfo *pInfo = GetFriend(f);
			Found = pInfo && pInfo->m_SteamID == m_lAvatars[i]->m_SteamID;
		}
		if(Found)
		{
			i++;
			continue;
		}

		m_lAvatars[i]->m_Animation.Reset(Graphics());
		delete m_lAvatars[i];
		m_lAvatars.remove_index(i);
	}

	// Only the avatars on screen are kept: an animated one is a texture per
	// frame, so a long friends list would otherwise hold on to a lot of them.
	const int64 Now = time_get();
	for(int i = 0; i < m_lAvatars.size(); i++)
	{
		if(m_lAvatars[i]->m_LastUsed != 0 && Now - m_lAvatars[i]->m_LastUsed > 30 * time_freq())
			m_lAvatars[i]->m_LastUsed = 0;
	}
	while(m_lAvatars.size() > MAX_CACHED_AVATARS)
	{
		int Oldest = -1;
		for(int i = 0; i < m_lAvatars.size(); i++)
		{
			if(m_lAvatars[i]->m_LastUsed != 0)
				continue;
			if(Oldest < 0 || m_lAvatars[i]->m_LastUsed < m_lAvatars[Oldest]->m_LastUsed)
				Oldest = i;
		}
		// everything is drawn right now, nothing to give back
		if(Oldest < 0)
			break;

		m_lAvatars[Oldest]->m_Animation.Reset(Graphics());
		delete m_lAvatars[Oldest];
		m_lAvatars.remove_index(Oldest);
	}
}

int CSteamFriends::NumFriends() const
{
	return m_pSteamFriendList ? m_pSteamFriendList->NumFriends() : 0;
}

const CSteamFriendInfo *CSteamFriends::GetFriend(int Index) const
{
	return m_pSteamFriendList ? m_pSteamFriendList->GetFriend(Index) : 0;
}

CSteamFriends::CAvatar *CSteamFriends::GetAvatarEntry(unsigned long long SteamID)
{
	for(int i = 0; i < m_lAvatars.size(); i++)
	{
		if(m_lAvatars[i]->m_SteamID == SteamID)
			return m_lAvatars[i];
	}

	// Avatars own textures, so they are allocated once: growing the array must
	// not copy the animations the menu is drawing right now.
	CAvatar *pAvatar = new CAvatar();
	pAvatar->m_SteamID = SteamID;
	m_lAvatars.add(pAvatar);
	return pAvatar;
}

void CSteamFriends::ResetAvatars()
{
	for(int i = 0; i < m_lAvatars.size(); i++)
	{
		m_lAvatars[i]->m_Animation.Reset(Graphics());
		delete m_lAvatars[i];
	}
	m_lAvatars.clear();
}

CGifAnimation *CSteamFriends::Avatar(const CSteamFriendInfo *pInfo)
{
	if(!pInfo)
		return 0;

	CAvatar *pAvatar = GetAvatarEntry(pInfo->m_SteamID);
	pAvatar->m_LastUsed = time_get();

	// The animation wins over the still avatar Steam keeps for the friend. The
	// engine hands it out only once the GIF has been downloaded.
	int GifSize = 0, GifRevision = 0;
	const unsigned char *pGif = 0;
	if(m_pSteamFriendList && m_pSteamFriendList->GetAnimatedAvatar(pInfo->m_SteamID, &pGif, &GifSize, &GifRevision) &&
		(pAvatar->m_Source != SOURCE_GIF || GifRevision != pAvatar->m_GifRevision))
	{
		pAvatar->m_GifRevision = GifRevision;
		if(pAvatar->m_Animation.LoadGIF(Graphics(), pGif, GifSize))
			pAvatar->m_Source = SOURCE_GIF;
		else
		{
			// a broken download must not hide the still avatar
			pAvatar->m_Animation.Reset(Graphics());
			pAvatar->m_Source = SOURCE_NONE;
		}
	}

	// Drawing the animation of an entry without frames would show whatever
	// texture is bound at the time.
	if(pAvatar->m_Source == SOURCE_GIF && pAvatar->m_Animation.IsValid())
		return &pAvatar->m_Animation;

	int Width = 0, Height = 0, Revision = 0;
	const unsigned char *pData = 0;
	if(!m_pSteamFriendList || !m_pSteamFriendList->GetAvatar(pInfo->m_SteamID, &Width, &Height, &pData, &Revision))
		return pAvatar->m_Animation.IsValid() ? &pAvatar->m_Animation : 0;

	// The still frame is uploaded when it changed, and whenever the animated
	// one was played before; Steam serves both only once it has loaded them.
	if(pAvatar->m_Source != SOURCE_STATIC || Revision != pAvatar->m_StaticRevision)
	{
		pAvatar->m_StaticRevision = Revision;
		if(pAvatar->m_Animation.LoadRGBA(Graphics(), Width, Height, pData))
			pAvatar->m_Source = SOURCE_STATIC;
		else
		{
			pAvatar->m_Animation.Reset(Graphics());
			pAvatar->m_Source = SOURCE_NONE;
		}
	}

	return pAvatar->m_Animation.IsValid() ? &pAvatar->m_Animation : 0;
}