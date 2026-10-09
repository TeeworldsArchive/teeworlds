/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_HUD_H
#define GAME_CLIENT_COMPONENTS_HUD_H
#include <base/system/time.h>
#include <game/client/component.h>

class CHud : public CComponent
{
	float m_Width, m_Height;
	float m_AverageFPS;

	int64 m_WarmupHideTick;
	bool IsLargeWarmupTimerShown();

	int m_CheckpointDiff;
	int64 m_CheckpointTime;

	void RenderCursor();

	void RenderFps();
	void RenderConnectionWarning();
	void RenderTeambalanceWarning();
	void RenderVoting();
	void RenderNinjaBar(float x, float y, float Progress);
	void RenderTwoLayerIcon(int EmptySpriteID, int FullSpriteID, float x, float y, float Size, float Progress);
	void RenderHealthAndAmmo(const CNetObj_Character *pCharacter);
	void RenderGameTimer();
	void RenderPauseTimer();
	void RenderStartCountdown();
	void RenderNetworkIssueNotification();
	void RenderDeadNotification();
	void RenderScoreHud();
	void RenderSpectatorHud();
	void RenderSpectatorNotification();
	void RenderReadyUpNotification();
	void RenderWarmupTimer();
	void RenderRaceTime(const CNetObj_TeeInfo *pRaceInfo);
	void RenderCheckpoint();
	void RenderLocalTime(float x);

public:
	CHud();

	virtual void OnReset() override;
	virtual void OnMessage(int MsgType, void *pRawMsg) override;
	virtual void OnRender() override;
};

#endif
