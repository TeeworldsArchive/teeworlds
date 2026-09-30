/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <engine/shared/config.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include "tdm.h"

CGameControllerTDM::CGameControllerTDM(class CGameContext *pGameServer) : IGameController(pGameServer)
{
	/*
	m_pGameType = "TDM";
	*/
	m_pGameType = "TDM*";
	m_GameFlags = GAMEFLAG_TEAMS;
	m_MaxPlayerSlots = 16;
}

// event
int CGameControllerTDM::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	IGameController::OnCharacterDeath(pVictim, pKiller, Weapon);

	if(pKiller && Weapon != WEAPON_GAME)
	{
		// do team scoring
		if(pKiller == pVictim->GetPlayer() || pKiller->GetTeam() == pVictim->GetPlayer()->GetTeam())
			m_aTeamscore[pKiller->GetTeam() & 1]--; // klant arschel
		else
			m_aTeamscore[pKiller->GetTeam() & 1]++; // good shit
	}

	pVictim->GetPlayer()->m_RespawnTick = maximum(pVictim->GetPlayer()->m_RespawnTick, Server()->Tick() + Server()->TickSpeed() * Config()->m_SvRespawnDelayTDM);

	return 0;
}

REGISTER_GAMEMODE("tdm", CGameControllerTDM);
