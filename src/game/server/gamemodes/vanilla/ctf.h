/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_SERVER_GAMEMODES_VANILLA_CTF_H
#define GAME_SERVER_GAMEMODES_VANILLA_CTF_H
#include <game/server/entity.h>
#include <game/server/gamecontroller.h>

class CGameControllerCTF : public IGameController
{
	// balancing
	virtual bool CanBeMovedOnBalance(int ClientID) const override;

	// game
	class CFlag *m_apFlags[2];

	virtual bool DoWincheckMatch() override;

public:
	CGameControllerCTF(class CGameContext *pGameServer);

	// event
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	virtual void OnFlagReturn(class CFlag *pFlag) override;
	virtual bool OnEntity(int Index, vec2 Pos) override;

	// general
	virtual void Snap(int SnappingClient) override;
	virtual void Tick() override;
	virtual bool IsPureTuning() const override { return true; }
};

#endif
