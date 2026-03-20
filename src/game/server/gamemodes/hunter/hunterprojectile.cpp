/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <game/server/entities/character.h>
#include <generated/server_data.h>
#include "hunterprojectile.h"

CHunterProjectile::CHunterProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
	int Damage, bool Explosive, float Force, int SoundImpact, int Weapon) : CProjectile(pGameWorld, Type, Owner, Pos, Dir, Span, Damage, Explosive, Force, SoundImpact, Weapon)
{
	m_StartTick = Server()->Tick();
	m_Owner = Owner;
}

void CHunterProjectile::Tick()
{
	CProjectile::Tick();
	if(!IsMarkedForDestroy()) // !(TargetChr || Collide || m_LifeSpan < 0 || GameLayerClipped(CurPos))
		return;

	vec2 CurPos = GetPos((Server()->Tick() - m_StartTick) / (float) Server()->TickSpeed());
	GameServer()->Collision()->IntersectLine(GetPos((Server()->Tick() - m_StartTick - 1) / (float) Server()->TickSpeed()), CurPos, &CurPos, 0);

	const float PerAngle = pi * 2 / RAND_MAX;
	for(int i = 0; i < 16; i++)
	{
		new CProjectile(GameWorld(),
					WEAPON_SHOTGUN,
					m_Owner,
					CurPos,
					direction(PerAngle * rand()),
					(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_ShotgunLifetime),
					g_pData->m_Weapons.m_Shotgun.m_pBase->m_Damage, false, 0, -1, WEAPON_SHOTGUN);
	}
	for(int i = 0; i < 5; i++)
	{
		CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *) GameServer()->m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
		if(!pEvent)
			return;

		vec2 Pos = CurPos + direction(PerAngle * rand() + i * (pi * 2 / 5)) * (3.8f * 32.f);
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
	}
}