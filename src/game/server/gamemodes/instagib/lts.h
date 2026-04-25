/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_INSTAGIB_LTS_H
#define GAME_SERVER_GAMEMODES_INSTAGIB_LTS_H

#include <game/server/gamemodes/vanilla/lts.h>
#include "core.h"

class CGameControllerInstaLTS : public CGameControllerLTS, CInstaCore
{
public:
	CGameControllerInstaLTS(class CGameContext *pGameServer);
	virtual void OnCharacterSpawn(class CCharacter *pChr);
	virtual int OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon);
	virtual bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const;
	virtual bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const;
};

#endif
