/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_SERVER_GAMEMODES_VANILLA_TDM_H
#define GAME_SERVER_GAMEMODES_VANILLA_TDM_H
#include <game/server/gamecontroller.h>

class CGameControllerTDM : public IGameController
{
public:
	CGameControllerTDM(class CGameContext *pGameServer);

	// event
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	virtual bool IsPureTuning() const override { return true; }
};

#endif
