/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_CLIENT_SERVERBROWSER_H
#define ENGINE_CLIENT_SERVERBROWSER_H

#include <base/system/time.h>
#include <engine/serverbrowser.h>
#include "serverbrowser_entry.h"
#include "serverbrowser_fav.h"
#include "serverbrowser_filter.h"

extern void SortClients(CServerInfo *pInfo);

class CServerBrowser : public IServerBrowser
{
public:
	enum
	{
		SET_MASTER_ADD = 1,
		SET_FAV_ADD,
		SET_TOKEN,
		SET_PLAYERINFO,
	};

	CServerBrowser();
	void Init(class CNetClient *pClient, const char *pNetVersion);
	void Set(const NETADDR &Addr, int SetType, int Token, const CServerInfo *pInfo);
	void Update();

	// interface functions
	virtual int GetType() override { return m_ActServerlistType; }
	virtual void SetType(int Type) override;
	virtual void Refresh(int RefreshFlags) override;
	virtual bool IsRefreshing() const override { return m_pFirstReqServer != 0; }
	virtual bool IsRefreshingMasters() const override { return m_pMasterServer->IsRefreshing(); }
	virtual bool WasUpdated(bool Purge) override;
	virtual int LoadingProgression() const override;
	void RequestResort() { m_NeedResort = true; }

	virtual int NumServers() const override { return m_aServerlist[m_ActServerlistType].m_NumServers; }
	virtual int NumPlayers() const override { return m_aServerlist[m_ActServerlistType].m_NumPlayers; }
	virtual int NumClients() const override { return m_aServerlist[m_ActServerlistType].m_NumClients; }
	virtual const CServerInfo *Get(int Index) const override { return &m_aServerlist[m_ActServerlistType].m_ppServerlist[Index]->m_Info; }

	virtual int NumSortedServers(int FilterIndex) const override { return m_ServerBrowserFilter.GetNumSortedServers(FilterIndex); }
	virtual int NumSortedPlayers(int FilterIndex) const override { return m_ServerBrowserFilter.GetNumSortedPlayers(FilterIndex); }
	virtual const CServerInfo *SortedGet(int FilterIndex, int Index) const override { return &m_aServerlist[m_ActServerlistType].m_ppServerlist[m_ServerBrowserFilter.GetIndex(FilterIndex, Index)]->m_Info; }
	virtual const void *GetID(int FilterIndex, int Index) const override { return m_ServerBrowserFilter.GetID(FilterIndex, Index); }

	virtual void AddFavorite(const CServerInfo *pInfo) override;
	virtual void RemoveFavorite(const CServerInfo *pInfo) override;
	virtual void UpdateFavoriteState(CServerInfo *pInfo) override;
	virtual void SetFavoritePassword(const char *pAddress, const char *pPassword) override;
	virtual const char *GetFavoritePassword(const char *pAddress) override;

	virtual int AddFilter(const CServerFilterInfo *pFilterInfo) override { return m_ServerBrowserFilter.AddFilter(pFilterInfo); }
	virtual void SetFilter(int Index, const CServerFilterInfo *pFilterInfo) override { m_ServerBrowserFilter.SetFilter(Index, pFilterInfo); }
	virtual void GetFilter(int Index, CServerFilterInfo *pFilterInfo) override { m_ServerBrowserFilter.GetFilter(Index, pFilterInfo); }
	virtual void RemoveFilter(int Index) override { m_ServerBrowserFilter.RemoveFilter(Index); }

	static void CBFTrackPacket(int TrackID, void *pUser);

	void LoadServerlist();
	void SaveServerlist();

private:
	class CNetClient *m_pNetClient;
	class CConfig *m_pConfig;
	class IConsole *m_pConsole;
	class IStorage *m_pStorage;
	class IMasterServer *m_pMasterServer;

	class CServerBrowserFavorites m_ServerBrowserFavorites;
	class CServerBrowserFilter m_ServerBrowserFilter;

	class CConfig *Config() const { return m_pConfig; }
	class IConsole *Console() const { return m_pConsole; }
	class IStorage *Storage() const { return m_pStorage; }

	// serverlist
	int m_ActServerlistType;
	class CServerlist
	{
	public:
		class CHeap m_ServerlistHeap;

		int m_NumClients;
		int m_NumPlayers;
		int m_NumServers;
		int m_NumServerCapacity;

		CServerEntry *m_aServerlistIp[256]; // ip hash list
		CServerEntry **m_ppServerlist;

		~CServerlist();
		void Clear();
	} m_aServerlist[NUM_TYPES];

	CServerEntry *m_pFirstReqServer; // request list
	CServerEntry *m_pLastReqServer;
	int m_NumRequests;

	bool m_NeedRefresh;
	bool m_InfoUpdated;
	bool m_NeedResort;

	// the token is to keep server refresh separated from each other
	int m_CurrentLanToken;

	int m_RefreshFlags;
	int64 m_BroadcastTime;
	int64 m_MasterRefreshTime;

	CServerEntry *Add(int ServerlistType, const NETADDR &Addr);
	CServerEntry *Find(int ServerlistType, const NETADDR &Addr);
	void QueueRequest(CServerEntry *pEntry);
	void RemoveRequest(CServerEntry *pEntry);
	void RequestImpl(const NETADDR &Addr, CServerEntry *pEntry);
	void SetInfo(int ServerlistType, CServerEntry *pEntry, const CServerInfo &Info);
	void AddInfo(int ServerlistType, CServerEntry *pEntry, const CServerInfo &Info);
};

#endif
