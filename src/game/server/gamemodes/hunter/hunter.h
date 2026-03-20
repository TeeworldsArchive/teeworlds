/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_HUNTER_H
#define GAME_SERVER_GAMEMODES_HUNTER_H
#include <game/server/gamecontroller.h>
#include <game/server/gamecontext.h>
#include "class.h"

class CGameControllerHunter : public IGameController
{
public:
	class IClass
	{
	public:
		IClass(class CGameControllerHunter *pController);
		~IClass() {};
		virtual void Tick();
		virtual bool CanCharacterPickup(class CCharacter *pChr) const;
		virtual bool CanCharacterWeaponFullAuto(class CCharacter *pChr, int Weapon);
		virtual bool IsFriendlyFire(int ClientID1, int ClientID2) const;
		virtual bool IsFriendlyTeamFire(int Team1, int Team2) const;
		virtual int OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon);
		virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
		virtual void OnCharacterSpawn(class CCharacter *pChr);
		virtual void OnKill(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	protected:
		CGameControllerHunter *m_pController;
	};
	struct SPlayerData
	{
		enum EPlayerStat
		{
			OFFLINE,
			SPECTATING,
			IN_GAME,
		};
		int m_Stat;
		int m_Team;
		int m_Class;
		int m_HiddenScore;
		void Reset(int Stat = IN_GAME)
		{
			mem_zero(this, sizeof(SPlayerData));
			m_Stat = Stat;
		};
	};

public:
	CGameControllerHunter(class CGameContext *pGameServer);
	~CGameControllerHunter();
	virtual bool CanCharacterPickup(class CCharacter *pChr) const override { return true; }
	virtual bool CanCharacterWeaponFullAuto(class CCharacter *pChr, int Weapon) override;
	virtual void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true) override;
	// virtual bool DoWincheckMatch() override;
	// virtual void DoWincheckRound() {} override;
	// virtual int GetPlayerCheckTeam(class CPlayer *pPlayer) const override;
	virtual bool IsFriendlyFire(int ClientID1, int ClientID2) const override;
	virtual bool IsFriendlyTeamFire(int Team1, int Team2) const override;
	virtual int OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon) override;
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	virtual void OnCharacterSpawn(class CCharacter *pChr) override;
	virtual void OnPlayerConnect(class CPlayer *pPlayer) override;
	virtual void OnPlayerDisconnect(class CPlayer *pPlayer) override;
	// virtual void OnPlayerInfoChange(class CPlayer *pPlayer) override;
	virtual void OnRoundStart() override;
	// virtual void Snap(int SnappingClient) override;
	virtual void Tick() override;

protected:
	IClass *m_apClass[NUM_CLASS_ID - 1];
	SPlayerData m_aPlayerData[MAX_CLIENTS];
	int64 m_HunterRandMask;
};

#define INCLUDE_HUNTER_CLASS_HEADER
#include "class.h"
#undef INCLUDE_HUNTER_CLASS_HEADER

#endif
