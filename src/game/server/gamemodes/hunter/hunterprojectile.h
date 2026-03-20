/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_HUNTER_PROJECTILE_H
#define GAME_SERVER_GAMEMODES_HUNTER_PROJECTILE_H

#include <game/server/entities/projectile.h>

class CHunterProjectile : public CProjectile
{
public:
	CHunterProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
		int Damage, bool Explosive, float Force, int SoundImpact, int Weapon);

	virtual void Tick() override;
	int m_StartTick;
	int m_Owner;
};

#endif // GAME_SERVER_GAMEMODES_HUNTER_PROJECTILE_H
