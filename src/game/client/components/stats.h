/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_STATS_H
#define GAME_CLIENT_COMPONENTS_STATS_H

#include <base/system/time.h>
#include <game/client/component.h>

enum
{
	TC_STATS_KILLS = 1,
	TC_STATS_DEATHS = 2,
	TC_STATS_SUICIDES = 4,
	TC_STATS_RATIO = 8,
	TC_STATS_NET = 16,
	TC_STATS_KPM = 32,
	TC_STATS_SPREE = 64,
	TC_STATS_BESTSPREE = 128,
	TC_STATS_FLAGGRABS = 256,
	TC_STATS_WEAPS = 512,
	TC_STATS_FLAGCAPTURES = 1024,
	NUM_TC_STATS = 11
};

class CStats : public CComponent
{
private:
	// stats
	class CPlayerStats
	{
	public:
		CPlayerStats()
		{
			Reset();
		}

		int m_IngameTicks;
		int m_aKillsWith[NUM_WEAPONS];
		int m_aDeathsFrom[NUM_WEAPONS];
		int m_Kills;
		int m_Deaths;
		int m_Suicides;
		int m_BestSpree;
		int m_CurrentSpree;

		int m_FlagGrabs;
		int m_FlagCaptures;
		int m_CarriersKilled;
		int m_KillsCarrying;
		int m_DeathsCarrying;

		void Reset();
	};
	CPlayerStats m_aStats[MAX_CLIENTS];

	bool m_Active;
	bool m_Activate;

	bool m_ScreenshotTaken;
	int64 m_ScreenshotTime;
	static void ConKeyStats(IConsole::IResult *pResult, void *pUserData);
	void AutoStatScreenshot();

public:
	CStats();
	bool IsActive() const;
	virtual void OnReset() override;
	void OnStartGame();
	virtual void OnConsoleInit() override;
	virtual void OnRender() override;
	virtual void OnRelease() override;
	virtual void OnMessage(int MsgType, void *pRawMsg) override;

	void UpdatePlayTime(int Ticks);
	void OnMatchStart();
	void OnFlagGrab(int ClientID);
	void OnFlagCapture(int ClientID);
	void OnPlayerEnter(int ClientID);
	void OnPlayerLeave(int ClientID);

	const CPlayerStats *GetPlayerStats(int ClientID) const { return &m_aStats[ClientID]; }
};

#endif
