#ifndef GAME_SERVER_GAMEMODES_REINFECTED_H
#define GAME_SERVER_GAMEMODES_REINFECTED_H

#include <game/server/gamecontroller.h>

class CGameControllerReinfected : public IGameController
{
	friend class CReinfectedHelper;
	class CReinfectedHelper *m_pHelper;

	void RefreshPlayerSkin(class CPlayer *pPlayer, bool Sync);
protected:
	const class CReinfectedHelper *Reinfected() const { return m_pHelper; }
	class CReinfectedHelper *Reinfected() { return m_pHelper; }

	virtual bool IsInfectionStarted();
	virtual void StartRandomInfection();
	void AddScoreForInfection(int InfectedID);
public:
	CGameControllerReinfected(class CGameContext *pGameServer);
	virtual ~CGameControllerReinfected();

	virtual bool IsFriendlyFire(int ClientID1, int ClientID2) const;

	virtual void OnPlayerConnect(class CPlayer *pPlayer);
	virtual void OnPlayerDisconnect(class CPlayer *pPlayer);
	virtual void OnPlayerInfoChange(class CPlayer *pPlayer);

	virtual bool CanCharacterPickup(class CCharacter *pChr) const;
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual int OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon);

	virtual void Tick();
	virtual bool DoWincheckMatch();
	virtual void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg);

	void RefreshClientSkin(int ClientID, bool Sync);
};

#endif // GAME_SERVER_GAMEMODES_REINFECTED_H
