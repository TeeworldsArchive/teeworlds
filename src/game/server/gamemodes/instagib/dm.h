/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_INSTAGIB_DM_H
#define GAME_SERVER_GAMEMODES_INSTAGIB_DM_H

#include <game/server/gamemodes/vanilla/dm.h>
#include "core.h"

class CGameControllerInstaDM : public CGameControllerDM, CInstaCore
{
public:
	CGameControllerInstaDM(class CGameContext *pGameServer);
	virtual void OnCharacterSpawn(class CCharacter *pChr);
	virtual int OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon);
	virtual bool NoEntitiesInMap() const { return true; }
	virtual bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const;
	virtual bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const;
};

#endif
