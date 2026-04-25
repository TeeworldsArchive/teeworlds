#include <game/server/entities/character.h>
#include <game/server/entities/laser.h>
#include <game/server/entities/projectile.h>
#include <game/server/gamecontext.h>
#include <generated/server_data.h>
#include "core.h"

void CInstaCore::InitCore(bool GrenadeMode)
{
	m_GreandeMode = GrenadeMode;
}

void CInstaCore::SetBasicWeapon(CCharacter *pChr)
{
	pChr->DisableWeapon(-1);
	pChr->EnableWeapon(m_GreandeMode ? WEAPON_GRENADE : WEAPON_LASER);

	pChr->RemoveWeapon(m_GreandeMode ? WEAPON_GRENADE : WEAPON_LASER);
	pChr->GiveWeapon(m_GreandeMode ? WEAPON_GRENADE : WEAPON_LASER, -1);
	pChr->SetWeapon(m_GreandeMode ? WEAPON_GRENADE : WEAPON_LASER);
}

int CInstaCore::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	if(!pChr)
		return 0;

	int ClientID = pChr->GetCID();
	vec2 ChrPos = pChr->GetPos();
	vec2 ProjStartPos = ChrPos + Direction * pChr->GetProximityRadius() * 0.75f;

	int ReloadTimer = 0;
	switch(Weapon)
	{
		case WEAPON_GRENADE:
		{
			new CProjectile(&pChr->GameServer()->m_World, WEAPON_GRENADE,
				ClientID,
				ProjStartPos,
				Direction,
				(int) (pChr->Server()->TickSpeed() * pChr->GameServer()->Tuning()->m_GrenadeLifetime),
				INSTA_MAGIC_MAX_DAMAGE, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			pChr->GameServer()->CreateSound(ChrPos, SOUND_GRENADE_FIRE);
		}
		break;

		case WEAPON_LASER:
		{
			new CLaser(&pChr->GameServer()->m_World, ChrPos, Direction, pChr->GameServer()->Tuning()->m_LaserReach, ClientID, INSTA_MAGIC_MAX_DAMAGE);
			pChr->GameServer()->CreateSound(ChrPos, SOUND_LASER_FIRE);
		}
		break;
	}
	if(!ReloadTimer)
		ReloadTimer = g_pData->m_Weapons.m_aId[Weapon].m_Firedelay * pChr->Server()->TickSpeed() / 1000;

	return ReloadTimer;
}
