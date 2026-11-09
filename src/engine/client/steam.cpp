/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#include "steam.h"

#include <base/math.h>
#include <base/system/debug.h>
#include <base/system/mem.h>
#include <base/system/string.h>
#include <base/system/thread.h>
#include <base/system/time.h>

#if defined(CONF_STEAM)
#include <engine/engine.h>
#include <engine/shared/http_request.h>

#include <steam/steam_api.h>

static bool IsGif(const unsigned char *pData, int Size)
{
	return pData && Size >= 10 && pData[0] == 'G' && pData[1] == 'I' && pData[2] == 'F' && pData[3] == '8';
}

/*
	Downloads the animated avatars of the friends.

	Animated avatars are Steam Community items and Steam exposes them in two
	pieces: the equipped item whose properties carry the URL of the GIF, and
	the GIF itself on the Steam CDN. Neither is covered by the avatar image
	handles, which only ever contain a still frame.

	The equipped items are looked up through the Steamworks API, whose call
	results arrive in RunCallbacks() and are therefore only usable from the
	main thread. The GIF itself is then downloaded through CHttpRequest, which
	runs in a job thread, so neither step stalls the client.

	Lookups and downloads run a few at a time. One at a time would make a long
	friends list fill in slowly, as every friend would wait for the lookup and
	the download of all the friends before them. Both are still started on
	demand, so only friends whose avatar is actually drawn cost anything.
*/
class CSteamAvatarFetcher
{
public:
	CSteamAvatarFetcher();
	~CSteamAvatarFetcher();

	void Init(IEngine *pEngine);
	void Update();

	bool GetAvatar(unsigned long long SteamID, const unsigned char **pData, int *pSize, int *pRevision);

private:
	enum
	{
		STATE_QUEUED = 0, // equipped items not requested yet
		STATE_REQUESTED, // Steam is looking up the equipped items
		STATE_DOWNLOADING, // the GIF url is known and the download runs
		STATE_READY, // m_lData holds the GIF
		STATE_NONE, // no animated avatar, or the download failed
	};

	class CEntry
	{
	public:
		unsigned long long m_SteamID;
		int m_State;
		int m_Revision;
		// when the friend was last drawn, and when the equipped items were
		// last looked up; Steam does not report a changed avatar while the
		// game runs, so the lookups are repeated now and then
		int64 m_LastUsed;
		int64 m_LastCheck;
		char m_aUrl[256];
		array<unsigned char> m_lData;

		CEntry() :
			m_SteamID(0), m_State(STATE_QUEUED), m_Revision(0), m_LastUsed(0), m_LastCheck(0) { m_aUrl[0] = 0; }
	};

	// how many friends are looked up at the same time; the call results are
	// cheap, so this only trades Steam round trips against a little memory
	enum
	{
		MAX_PARALLEL_LOOKUPS = 4,
		// An upper bound for the downloads; Update() uses fewer when the job
		// pool is small, as a download occupies a job thread for its whole
		// duration.
		MAX_PARALLEL_DOWNLOADS = 4,
	};

	class CDownload
	{
	public:
		unsigned long long m_SteamID;
		CHttpRequest *m_pRequest;

		CDownload() :
			m_SteamID(0), m_pRequest(0) {}
	};

	// A call result cannot be told apart from the outside once it is handed
	// to Steam, so every lookup that is waiting for one gets a slot of its
	// own instead of a single pending steam id.
	class CLookup
	{
	public:
		CSteamAvatarFetcher *m_pFetcher;
		unsigned long long m_SteamID;
		int64 m_Started;
		CCallResult<CLookup, EquippedProfileItems_t> m_Result;

		CLookup() :
			m_pFetcher(0), m_SteamID(0), m_Started(0) {}

		void OnResult(EquippedProfileItems_t *pResult, bool IOError);
	};

	IEngine *m_pEngine;
	array<CEntry> m_lEntries;
	array<CDownload> m_lDownloads;
	CLookup m_aLookups[MAX_PARALLEL_LOOKUPS];

	CCallback<CSteamAvatarFetcher, EquippedProfileItemsChanged_t> m_ItemsChangedCallback;

	CEntry *FindEntry(unsigned long long SteamID);
	CEntry *AddEntry(unsigned long long SteamID);
	bool IsDownloading(unsigned long long SteamID) const;
	int MaxDownloads() const;
	void CollectDownload(CDownload *pDownload);
	void StartDownloads();
	void StartLookups(int64 Now, bool SteamRunning);
	void OnLookupResult(CLookup *pLookup, EquippedProfileItems_t *pResult, bool IOError);
	void OnEquippedProfileItemsChanged(EquippedProfileItemsChanged_t *pResult);
};

CSteamAvatarFetcher::CSteamAvatarFetcher() :
	m_ItemsChangedCallback(this, &CSteamAvatarFetcher::OnEquippedProfileItemsChanged)
{
	m_pEngine = 0;
	for(int i = 0; i < MAX_PARALLEL_LOOKUPS; i++)
		m_aLookups[i].m_pFetcher = this;
}

CSteamAvatarFetcher::~CSteamAvatarFetcher()
{
	// Deleting a request while its job is still running would free the buffer
	// curl writes into. Abort the transfers and wait for the jobs to return; a
	// request that does not come back in time is leaked rather than freeing
	// live data, because the client is shutting down anyway.
	for(int i = 0; i < m_lDownloads.size(); i++)
	{
		if(m_lDownloads[i].m_pRequest->Status() != CJob::STATE_DONE)
			m_lDownloads[i].m_pRequest->Cancel();
	}

	bool AllDone = false;
	const int64 Start = time_get();
	while(!AllDone && time_get() - Start < time_freq())
	{
		AllDone = true;
		for(int i = 0; i < m_lDownloads.size() && AllDone; i++)
			AllDone = m_lDownloads[i].m_pRequest->Status() == CJob::STATE_DONE;
		if(!AllDone)
			thread_sleep(1);
	}

	for(int i = 0; i < m_lDownloads.size(); i++)
	{
		if(m_lDownloads[i].m_pRequest->Status() == CJob::STATE_DONE)
			delete m_lDownloads[i].m_pRequest;
	}
	m_lDownloads.clear();
}

void CSteamAvatarFetcher::Init(IEngine *pEngine)
{
	m_pEngine = pEngine;
}

CSteamAvatarFetcher::CEntry *CSteamAvatarFetcher::FindEntry(unsigned long long SteamID)
{
	for(int i = 0; i < m_lEntries.size(); i++)
	{
		if(m_lEntries[i].m_SteamID == SteamID)
			return &m_lEntries[i];
	}
	return 0;
}

CSteamAvatarFetcher::CEntry *CSteamAvatarFetcher::AddEntry(unsigned long long SteamID)
{
	CEntry *pEntry = &m_lEntries.emplace();
	pEntry->m_SteamID = SteamID;
	return pEntry;
}

bool CSteamAvatarFetcher::GetAvatar(unsigned long long SteamID, const unsigned char **pData, int *pSize, int *pRevision)
{
	if(!m_pEngine)
		return false;

	CEntry *pEntry = FindEntry(SteamID);
	if(!pEntry)
		pEntry = AddEntry(SteamID);

	pEntry->m_LastUsed = time_get();
	if(pEntry->m_State != STATE_READY)
		return false;

	*pData = pEntry->m_lData.base_ptr();
	*pSize = pEntry->m_lData.size();
	*pRevision = pEntry->m_Revision;
	return true;
}

void CSteamAvatarFetcher::Update()
{
	if(!m_pEngine)
		return;

	// collect the finished downloads before anything new is started
	for(int i = 0; i < m_lDownloads.size();)
	{
		if(m_lDownloads[i].m_pRequest->Status() != CJob::STATE_DONE)
		{
			i++;
			continue;
		}
		CollectDownload(&m_lDownloads[i]);
		m_lDownloads.remove_index(i);
	}

	const int64 Now = time_get();

	// Steam normally answers within a frame or two, but a lost call result
	// must not block the rest of the list forever.
	for(int i = 0; i < MAX_PARALLEL_LOOKUPS; i++)
	{
		CLookup *pLookup = &m_aLookups[i];
		if(!pLookup->m_SteamID || Now - pLookup->m_Started <= 30 * time_freq())
			continue;

		CEntry *pEntry = FindEntry(pLookup->m_SteamID);
		if(pEntry && pEntry->m_State == STATE_REQUESTED)
		{
			pEntry->m_State = STATE_NONE;
			pEntry->m_LastCheck = Now;
		}
		pLookup->m_Result.Cancel();
		pLookup->m_SteamID = 0;
	}

	const bool SteamRunning = SteamAPI_IsSteamRunning();

	// The frames of an avatar are only kept while the friend is on screen, and
	// the equipped items are looked up again every ten minutes: Steam does not
	// report an avatar being equipped or removed while the game is running.
	for(int i = 0; i < m_lEntries.size();)
	{
		CEntry *pEntry = &m_lEntries[i];
		if(pEntry->m_LastUsed == 0)
		{
			i++;
			continue;
		}

		const int64 Unused = Now - pEntry->m_LastUsed;
		if(Unused > 60 * 60 * time_freq())
		{
			// A lookup or download that is still on its way for this friend
			// simply finds no entry once it is done.
			m_lEntries.remove_index(i);
			continue;
		}

		if(pEntry->m_State == STATE_READY && Unused > 5 * 60 * time_freq())
		{
			// off screen for a while: give the memory back, it is fetched
			// again the next time the friend is drawn
			pEntry->m_lData.clear();
			pEntry->m_aUrl[0] = 0;
			pEntry->m_Revision++;
			pEntry->m_State = STATE_QUEUED;
		}
		else if((pEntry->m_State == STATE_READY || pEntry->m_State == STATE_NONE) && pEntry->m_LastCheck != 0 &&
			Now - pEntry->m_LastCheck > 10 * 60 * time_freq() && Unused <= 5 * 60 * time_freq())
		{
			pEntry->m_lData.clear();
			pEntry->m_aUrl[0] = 0;
			pEntry->m_Revision++;
			pEntry->m_State = STATE_QUEUED;
		}
		i++;
	}

	StartDownloads();
	StartLookups(Now, SteamRunning);
}

int CSteamAvatarFetcher::MaxDownloads() const
{
	// A download holds on to a job thread until the transfer is over, so leave
	// the pool room for the other jobs unless it has threads to spare.
	const int NumThreads = m_pEngine->NumJobThreads();
	return minimum(maximum(NumThreads - 1, 1), (int) MAX_PARALLEL_DOWNLOADS);
}

bool CSteamAvatarFetcher::IsDownloading(unsigned long long SteamID) const
{
	for(int i = 0; i < m_lDownloads.size(); i++)
	{
		if(m_lDownloads[i].m_SteamID == SteamID)
			return true;
	}
	return false;
}

void CSteamAvatarFetcher::StartDownloads()
{
	const int MaxDownloads = this->MaxDownloads();
	for(int i = 0; i < m_lEntries.size() && m_lDownloads.size() < MaxDownloads; i++)
	{
		CEntry *pEntry = &m_lEntries[i];
		if(pEntry->m_State != STATE_DOWNLOADING || IsDownloading(pEntry->m_SteamID))
			continue;

		CDownload *pDownload = &m_lDownloads.emplace();
		pDownload->m_SteamID = pEntry->m_SteamID;
		pDownload->m_pRequest = new CHttpRequest("GET", pEntry->m_aUrl, 20L);
		pDownload->m_pRequest->StartRun(m_pEngine);
	}
}

void CSteamAvatarFetcher::StartLookups(int64 Now, bool SteamRunning)
{
	for(int i = 0; i < m_lEntries.size(); i++)
	{
		CEntry *pEntry = &m_lEntries[i];
		if(pEntry->m_State != STATE_QUEUED)
			continue;

		if(!SteamRunning)
		{
			// Without the Steam client the lookups would only fail; the list
			// is not shown then anyway.
			pEntry->m_State = STATE_NONE;
			continue;
		}

		CLookup *pLookup = 0;
		for(int l = 0; l < MAX_PARALLEL_LOOKUPS; l++)
		{
			if(!m_aLookups[l].m_SteamID)
			{
				pLookup = &m_aLookups[l];
				break;
			}
		}
		if(!pLookup)
			return;

		const SteamAPICall_t Call = SteamFriends()->RequestEquippedProfileItems(CSteamID(pEntry->m_SteamID));
		if(Call == k_uAPICallInvalid)
		{
			pEntry->m_State = STATE_NONE;
			pEntry->m_LastCheck = Now;
			continue;
		}
		pEntry->m_State = STATE_REQUESTED;
		pLookup->m_SteamID = pEntry->m_SteamID;
		pLookup->m_Started = Now;
		pLookup->m_Result.Set(Call, pLookup, &CLookup::OnResult);
	}
}

void CSteamAvatarFetcher::CollectDownload(CDownload *pDownload)
{
	CHttpRequest *pRequest = pDownload->m_pRequest;
	CEntry *pEntry = FindEntry(pDownload->m_SteamID);
	if(pEntry)
	{
		const int Size = pRequest->ReceivedDataSize();
		// Only a GIF can be played. Steam answers with a still image for some
		// items; GetAvatar() keeps covering those.
		if(pRequest->Result() == 0 && pRequest->ResponseCode() == 200 && IsGif(pRequest->ReceivedData(), Size))
		{
			// the data is taken over instead of copied; the request is done and
			// several downloads can finish in the same frame
			pRequest->MoveReceivedData(&pEntry->m_lData);
			pEntry->m_Revision++;
			pEntry->m_State = STATE_READY;
		}
		else
		{
			dbg_msg("steam", "no animated avatar for %llu (result %d, http %d, %d bytes)", pEntry->m_SteamID,
				pRequest->Result(), pRequest->ResponseCode(), Size);
			pEntry->m_State = STATE_NONE;
		}
		pEntry->m_LastCheck = time_get();
	}
	delete pRequest;
}

void CSteamAvatarFetcher::CLookup::OnResult(EquippedProfileItems_t *pResult, bool IOError)
{
	if(m_pFetcher)
		m_pFetcher->OnLookupResult(this, pResult, IOError);
}

void CSteamAvatarFetcher::OnLookupResult(CLookup *pLookup, EquippedProfileItems_t *pResult, bool IOError)
{
	const unsigned long long SteamID = pLookup->m_SteamID;
	pLookup->m_SteamID = 0;

	CEntry *pEntry = SteamID ? FindEntry(SteamID) : 0;
	if(!pEntry)
		return;

	pEntry->m_LastCheck = time_get();

	if(IOError || pResult->m_eResult != k_EResultOK || !pResult->m_bHasAnimatedAvatar)
	{
		pEntry->m_State = STATE_NONE;
		return;
	}

	// For an animated avatar ImageSmall is the animation itself, see the
	// ECommunityProfileItemProperty documentation of the Steamworks SDK.
	const char *pUrl = SteamFriends()->GetProfileItemPropertyString(CSteamID(SteamID),
		k_ECommunityProfileItemType_AnimatedAvatar, k_ECommunityProfileItemProperty_ImageSmall);
	if(!pUrl || !pUrl[0] || str_length(pUrl) >= (int) sizeof(pEntry->m_aUrl))
	{
		pEntry->m_State = STATE_NONE;
		return;
	}

	str_copy(pEntry->m_aUrl, pUrl, sizeof(pEntry->m_aUrl));
	pEntry->m_State = STATE_DOWNLOADING;
}

void CSteamAvatarFetcher::OnEquippedProfileItemsChanged(EquippedProfileItemsChanged_t *pResult)
{
	CEntry *pEntry = FindEntry(pResult->m_steamID.ConvertToUint64());
	if(!pEntry)
		return;

	// The friend changed or removed their animated avatar. A lookup that is
	// already on its way reads the new state anyway.
	if(pEntry->m_State == STATE_REQUESTED || pEntry->m_State == STATE_DOWNLOADING)
		return;

	// The revision makes the menus drop the frames they are still playing.
	pEntry->m_lData.clear();
	pEntry->m_aUrl[0] = 0;
	pEntry->m_Revision++;
	pEntry->m_LastCheck = 0;
	pEntry->m_State = STATE_QUEUED;
}

#endif

CSteamPresence::CSteamPresence()
{
	m_Initialized = false;
	m_LastMode = MODE_NONE;
	m_aLastGameType[0] = 0;
	m_aLastMap[0] = 0;
	m_aLastGroup[0] = 0;
	m_LastGroupSize = 0;
	m_LastFriendsRefresh = 0;
	m_pAvatarFetcher = 0;
}

CSteamPresence::~CSteamPresence()
{
#if defined(CONF_STEAM)
	delete m_pAvatarFetcher;
#endif
	m_pAvatarFetcher = 0;

#if defined(CONF_STEAM)
	if(m_Initialized)
	{
		SteamFriends()->ClearRichPresence();
		SteamAPI_Shutdown();
		m_Initialized = false;
	}
#endif
	m_lFriends = sorted_array<CSteamFriendInfo>();
	m_lStaticAvatars.clear();
}

void CSteamPresence::Init([[maybe_unused]] IEngine *pEngine)
{
#if defined(CONF_STEAM)
	if(SteamAPI_Init())
	{
		m_Initialized = true;
		m_pAvatarFetcher = new CSteamAvatarFetcher();
		m_pAvatarFetcher->Init(pEngine);
		dbg_msg("steam", "steamworks initialized (steam client running: %s)", SteamAPI_IsSteamRunning() ? "yes" : "no");
	}
	else
	{
		// Not fatal: the game was most likely started without the Steam
		// client, so simply do not advertise anything.
		dbg_msg("steam", "SteamAPI_Init failed, rich presence and friends list disabled");
	}
#endif
}

void CSteamPresence::RunCallbacks()
{
#if defined(CONF_STEAM)
	if(m_Initialized)
	{
		SteamAPI_RunCallbacks();
		if(m_pAvatarFetcher)
			m_pAvatarFetcher->Update();
	}
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

bool CSteamPresence::Available() const
{
#if defined(CONF_STEAM)
	// SteamAPI_Init also succeeds when the Steam client is not running, using
	// the cached account data; the friends list would be stale then.
	return m_Initialized && SteamAPI_IsSteamRunning();
#else
	return false;
#endif
}

void CSteamPresence::Refresh()
{
#if defined(CONF_STEAM)
	if(!Available())
		return;

	// The friends list does not change fast enough to be rebuilt on every
	// frame, and the menu can be drawn twice per frame while it transitions.
	const int64 Now = time_get();
	if(m_LastFriendsRefresh != 0 && Now - m_LastFriendsRefresh < time_freq())
		return;
	m_LastFriendsRefresh = Now;

	// The list is rebuilt sorted; friends that are drawn keep their entry, so
	// pointers into the array stay valid.
	sorted_array<CSteamFriendInfo> lFriends;

	const AppId_t AppID = SteamUtils()->GetAppID();
	const int Count = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
	for(int i = 0; i < Count; i++)
	{
		const CSteamID FriendID = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);

		CSteamFriendInfo Info;
		mem_zero(&Info, sizeof(Info));
		Info.m_SteamID = FriendID.ConvertToUint64();
		str_copy(Info.m_aName, SteamFriends()->GetFriendPersonaName(FriendID), sizeof(Info.m_aName));
		Info.m_State = SteamFriends()->GetFriendPersonaState(FriendID);

		FriendGameInfo_t GameInfo;
		mem_zero(&GameInfo, sizeof(GameInfo));
		if(SteamFriends()->GetFriendGamePlayed(FriendID, &GameInfo))
		{
			if(GameInfo.m_gameID.AppID() == AppID)
			{
				Info.m_PlayingThisGame = true;
				str_copy(Info.m_aGameType, SteamFriends()->GetFriendRichPresence(FriendID, "gamemode"), sizeof(Info.m_aGameType));
				str_copy(Info.m_aMap, SteamFriends()->GetFriendRichPresence(FriendID, "mapname"), sizeof(Info.m_aMap));
				str_copy(Info.m_aServerAddress, SteamFriends()->GetFriendRichPresence(FriendID, "steam_player_group"), sizeof(Info.m_aServerAddress));
			}
			else
			{
				Info.m_PlayingOtherGame = true;
			}
		}

		// Friends without a name (deleted accounts, Steam hiccups) are not
		// useful in the list.
		if(Info.m_aName[0])
			lFriends.add(Info);
	}

	m_lFriends = lFriends;

	// Forget avatars of friends that are gone.
	for(int i = 0; i < m_lStaticAvatars.size();)
	{
		bool Found = false;
		for(int f = 0; f < m_lFriends.size() && !Found; f++)
			Found = m_lFriends[f].m_SteamID == m_lStaticAvatars[i].m_SteamID;
		if(Found)
			i++;
		else
			m_lStaticAvatars.remove_index(i);
	}
#endif
}

int CSteamPresence::NumFriends() const
{
	return m_lFriends.size();
}

const CSteamFriendInfo *CSteamPresence::GetFriend(int Index) const
{
	if(Index < 0 || Index >= m_lFriends.size())
		return 0;
	return &m_lFriends[Index];
}

CSteamPresence::CStaticAvatar *CSteamPresence::GetStaticAvatar(unsigned long long SteamID)
{
	for(int i = 0; i < m_lStaticAvatars.size(); i++)
	{
		if(m_lStaticAvatars[i].m_SteamID == SteamID)
			return &m_lStaticAvatars[i];
	}

	CStaticAvatar *pAvatar = &m_lStaticAvatars.emplace();
	pAvatar->m_SteamID = SteamID;
	return pAvatar;
}

bool CSteamPresence::GetAvatar([[maybe_unused]] unsigned long long SteamID, [[maybe_unused]] int *pWidth, [[maybe_unused]] int *pHeight, [[maybe_unused]] const unsigned char **pData, [[maybe_unused]] int *pRevision)
{
#if defined(CONF_STEAM)
	if(!m_Initialized || SteamID == 0)
		return false;

	// A friend whose avatar Steam has not loaded yet has no handle; the entry
	// is still created so the handle is checked again next frame.
	CStaticAvatar *pAvatar = GetStaticAvatar(SteamID);

	const CSteamID FriendID(SteamID);
	int Handle = SteamFriends()->GetMediumFriendAvatar(FriendID);
	if(Handle == -1)
		Handle = SteamFriends()->GetSmallFriendAvatar(FriendID);

	uint32 Width = 0, Height = 0;
	if(Handle == -1 || !SteamUtils()->GetImageSize(Handle, &Width, &Height) || Width == 0 || Height == 0 || Width > 256 || Height > 256)
		return false;

	if(pAvatar->m_Handle != Handle || pAvatar->m_Width != (int) Width || pAvatar->m_Height != (int) Height)
	{
		const int DataSize = (int) Width * (int) Height * 4;
		pAvatar->m_lData.set_size(DataSize);
		if(!SteamUtils()->GetImageRGBA(Handle, pAvatar->m_lData.base_ptr(), DataSize))
		{
			pAvatar->m_lData.clear();
			pAvatar->m_State = CStaticAvatar::STATE_NONE;
			return false;
		}
		pAvatar->m_Handle = Handle;
		pAvatar->m_Width = (int) Width;
		pAvatar->m_Height = (int) Height;
		pAvatar->m_State = CStaticAvatar::STATE_READY;
		pAvatar->m_Revision++;
	}

	if(pAvatar->m_State != CStaticAvatar::STATE_READY || !pAvatar->m_lData.size())
		return false;

	*pWidth = pAvatar->m_Width;
	*pHeight = pAvatar->m_Height;
	*pData = pAvatar->m_lData.base_ptr();
	*pRevision = pAvatar->m_Revision;
	return true;
#else
	return false;
#endif
}

bool CSteamPresence::GetAnimatedAvatar([[maybe_unused]] unsigned long long SteamID, [[maybe_unused]] const unsigned char **pData, [[maybe_unused]] int *pSize, [[maybe_unused]] int *pRevision)
{
#if defined(CONF_STEAM)
	if(!m_pAvatarFetcher || SteamID == 0)
		return false;
	return m_pAvatarFetcher->GetAvatar(SteamID, pData, pSize, pRevision);
#else
	return false;
#endif
}