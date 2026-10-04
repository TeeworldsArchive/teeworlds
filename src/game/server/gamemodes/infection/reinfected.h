/* (c) Teeworlds Archive Project Contributors. See license.txt. */
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
	virtual bool HasEnoughPlayers() const override;

public:
	CGameControllerReinfected(class CGameContext *pGameServer);
	virtual ~CGameControllerReinfected();

	virtual bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const override;
	virtual bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const override;
	virtual int GetPlayerCheckTeam(class CPlayer *pPlayer) const override;

	virtual void OnRoundStart() override;

	virtual void OnPlayerConnect(class CPlayer *pPlayer) override;
	virtual void OnPlayerDisconnect(class CPlayer *pPlayer) override;
	virtual void OnPlayerInfoChange(class CPlayer *pPlayer) override;

	virtual bool CanCharacterPickup(class CCharacter *pChr) const override;
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	virtual int OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon) override;
	virtual void OnCharacterSpawn(class CCharacter *pChr) override;

	virtual bool DoWincheckMatch() override;
	virtual void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg) override;

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
