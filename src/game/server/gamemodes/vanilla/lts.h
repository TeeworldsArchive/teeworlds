/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_SERVER_GAMEMODES_VANILLA_LTS_H
#define GAME_SERVER_GAMEMODES_VANILLA_LTS_H
#include <game/server/gamecontroller.h>

class CGameControllerLTS : public IGameController
{
public:
	CGameControllerLTS(class CGameContext *pGameServer);

	// event
	virtual void OnCharacterSpawn(class CCharacter *pChr) override;
	// game
	virtual void DoWincheckRound() override;
	virtual bool IsPureTuning() const override { return true; }
	virtual bool NoEntitiesInMap() const override { return true; }
};

#endif
