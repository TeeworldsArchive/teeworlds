/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_SERVER_GAMEMODES_VANILLA_DM_H
#define GAME_SERVER_GAMEMODES_VANILLA_DM_H
#include <game/server/gamecontroller.h>

class CGameControllerDM : public IGameController
{
public:
	CGameControllerDM(class CGameContext *pGameServer);
	virtual bool IsPureTuning() const override { return true; }
};

#endif
