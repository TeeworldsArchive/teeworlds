#include <engine/shared/config.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include "riwall.h"

CRIWall::CRIWall(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner) : CEntity(pGameWorld, CGameWorld::ENTTYPE_RIWALL, Pos)
{
	m_Dir = Direction;
	m_Owner = Owner;
	if(Owner < 0 || Owner > MAX_CLIENTS)
		m_Team = -1;
	else
		m_Team = GameServer()->m_pController->GetPlayerCheckTeam(GameServer()->m_apPlayers[Owner]);
	m_StartTick = Server()->Tick();

	GameWorld()->InsertEntity(this);
}

void CRIWall::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CRIWall::Tick()
{
	if(Server()->Tick() >= m_StartTick + Server()->TickSpeed() * Config()->m_RiWallLife)
	{
		GameServer()->CreateSound(GetPos() + m_Dir / 2.0f, SOUND_WEAPON_SPAWN);
		Reset();
		return;
	}
	if(m_Owner >= 0 && m_Owner < MAX_CLIENTS && !GameServer()->m_apPlayers[m_Owner])
		m_Owner = -1;

	if(IsActive())
	{
		for(CCharacter *pChr = static_cast<CCharacter *>(GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER)); pChr; pChr = static_cast<CCharacter *>(pChr->TypeNext()))
		{
			if(GameServer()->m_pController->IsFriendlyTeamFire(GameServer()->m_pController->GetPlayerCheckTeam(pChr->GetPlayer()), m_Team)) return;
			if(CheckHit(pChr)) pChr->Die(m_Owner, WEAPON_HAMMER);
		}
	}
}

void CRIWall::TickPaused()
{
	m_StartTick++;
}

void CRIWall::Snap(int SnappingClient)
{
	vec2 From = IsActive() ? GetPoint2() : GetPos();
	if(NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, From))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = round_to_int(GetPos().x);
	pObj->m_Y = round_to_int(GetPos().y);
	pObj->m_FromX = round_to_int(From.x);
	pObj->m_FromY = round_to_int(From.y);
	pObj->m_StartTick = Server()->Tick();
}

bool CRIWall::CheckHit(CCharacter *pChr)
{
	return distance(pChr->GetPos(), closest_point_on_line(GetPos(), GetPoint2(), pChr->GetPos())) < pChr->GetProximityRadius();
}

bool CRIWall::IsActive()
{
	return Server()->Tick() >= m_StartTick + Server()->TickSpeed() * Config()->m_RiWallDelay;
}

vec2 CRIWall::GetPoint2()
{
	return GetPos() + m_Dir * Config()->m_RiWallLength;
}
