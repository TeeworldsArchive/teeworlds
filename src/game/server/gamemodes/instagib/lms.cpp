/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include "lms.h"

CGameControllerInstaLMS::CGameControllerInstaLMS(CGameContext *pGameServer) : CGameControllerLMS(pGameServer)
{
	InitCore(str_startswith_nocase(Config()->m_SvGametype, "g"));
	m_pGameType = m_GreandeMode ? "gLMS" : "iLMS";
	m_MaxPlayerSlots = MAX_CLIENTS;
}

void CGameControllerInstaLMS::OnCharacterSpawn(CCharacter *pChr)
{
	CGameControllerLMS::OnCharacterSpawn(pChr);
	SetBasicWeapon(pChr);
}

int CGameControllerInstaLMS::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	return CInstaCore::OnCharacterFireWeapon(pChr, Direction, Weapon);
}

bool CGameControllerInstaLMS::IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const
{
	if(Damage < INSTA_MAGIC_MIN_DAMAGE || ClientID1 == ClientID2)
		return true;
	return IGameController::IsFriendlyFire(ClientID1, ClientID2, Damage);
}

bool CGameControllerInstaLMS::IsFriendlyTeamFire(int Team1, int Team2, int Damage) const
{
	if(Damage < INSTA_MAGIC_MIN_DAMAGE || Team1 == Team2)
		return true;
	return IGameController::IsFriendlyFire(Team1, Team2, Damage);
}

REGISTER_GAMEMODE("ilms", CGameControllerInstaLMS);
REGISTER_GAMEMODE_ALIAS(Grenade, "glms", CGameControllerInstaLMS);
