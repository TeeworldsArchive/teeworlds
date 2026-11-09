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
	m_pDecoder = 0;
	m_LoadBudgetStart = time_get();
}

CSteamFriends::~CSteamFriends()
{
	// OnShutdown() normally stops this thread already; it must not outlive the
	// process if the client did not get there.
	delete m_pDecoder;
	m_pDecoder = 0;
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
	delete m_pDecoder;
	m_pDecoder = 0;
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

	// The menu calls this once per frame before it draws the list, so it is the
	// point where a new avatar budget starts.
	m_LoadBudgetStart = time_get();
}

int CSteamFriends::NumFriends() const
{
	return m_pSteamFriendList ? m_pSteamFriendList->NumFriends() : 0;
}

const CSteamFriendInfo *CSteamFriends::GetFriend(int Index) const
{
	return m_pSteamFriendList ? m_pSteamFriendList->GetFriend(Index) : 0;
}

bool CSteamFriends::LoadBudgetLeft(int64 Now) const
{
	return Now - m_LoadBudgetStart < time_freq() * AVATAR_LOAD_BUDGET_MS / 1000;
}

CGifDecoder *CSteamFriends::Decoder()
{
	// A decoder is a thread of its own, so it is only started once the menu
	// actually shows an avatar.
	if(!m_pDecoder)
		m_pDecoder = new CGifDecoder();
	return m_pDecoder;
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
	CAvatar *pAvatar = new CAvatar(Decoder());
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
	const int64 Now = time_get();
	pAvatar->m_LastUsed = Now;
	const bool BudgetLeft = LoadBudgetLeft(Now);

	// The animation wins over the still avatar Steam keeps for the friend. The
	// engine hands it out only once the GIF has been downloaded, and decoding
	// it is started here; the first frame is ready right away, the rest is
	// composed in the background. The lookup itself is cheap, so it runs even
	// when the frame's load budget is used up, which keeps the download alive.
	int GifSize = 0, GifRevision = 0;
	const unsigned char *pGif = 0;
	const bool HaveGif = m_pSteamFriendList && m_pSteamFriendList->GetAnimatedAvatar(pInfo->m_SteamID, &pGif, &GifSize, &GifRevision);
	if(BudgetLeft && HaveGif && GifRevision != pAvatar->m_GifRevision)
	{
		// The revision is remembered even when the file cannot be played, so a
		// broken download is not opened again on every frame.
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
	// Asking Steam for the still avatar can decode the image on this thread, so
	// this load is bound to the frame's budget as well; the row keeps whatever
	// it has for a frame or two longer.
	if(!BudgetLeft || !m_pSteamFriendList || !m_pSteamFriendList->GetAvatar(pInfo->m_SteamID, &Width, &Height, &pData, &Revision))
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