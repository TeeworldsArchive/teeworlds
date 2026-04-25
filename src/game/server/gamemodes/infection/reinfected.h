#ifndef GAME_SERVER_GAMEMODES_INFECTION_REINFECTED_H
#define GAME_SERVER_GAMEMODES_INFECTION_REINFECTED_H

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
	virtual void Infect(int InfectedID);
	virtual void Cure(int CureID);
	virtual void AddScoreForInfection(int InfectedID);
	virtual bool HasEnoughPlayers() const;

public:
	CGameControllerReinfected(class CGameContext *pGameServer);
	virtual ~CGameControllerReinfected();

	virtual bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const;
	virtual bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const;
	virtual int GetPlayerCheckTeam(class CPlayer *pPlayer) const;

	virtual void OnRoundStart();

	virtual void OnPlayerConnect(class CPlayer *pPlayer);
	virtual void OnPlayerDisconnect(class CPlayer *pPlayer);
	virtual void OnPlayerInfoChange(class CPlayer *pPlayer);

	virtual bool CanCharacterPickup(class CCharacter *pChr) const;
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual int OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon);
	virtual void OnCharacterSpawn(class CCharacter *pChr);

	virtual bool DoWincheckMatch();
	virtual void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg);

	void RefreshClientSkin(int ClientID, bool Sync);

	enum
	{
		RITEAM_NONE = -1,
		RITEAM_HUMAN = 0,
		RITEAM_INFECTED,
		NUM_RITEAMS,
	};
};

#endif // GAME_SERVER_GAMEMODES_REINFECTED_H
