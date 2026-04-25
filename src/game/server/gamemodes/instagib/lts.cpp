/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include "lts.h"

CGameControllerInstaLTS::CGameControllerInstaLTS(CGameContext *pGameServer) : CGameControllerLTS(pGameServer)
{
	InitCore(str_startswith_nocase(Config()->m_SvGametype, "g"));
	m_pGameType = m_GreandeMode ? "gLTS" : "iLTS";
	m_MaxPlayerSlots = MAX_CLIENTS;
}

void CGameControllerInstaLTS::OnCharacterSpawn(CCharacter *pChr)
{
	CGameControllerLTS::OnCharacterSpawn(pChr);
	SetBasicWeapon(pChr);
}

int CGameControllerInstaLTS::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	return CInstaCore::OnCharacterFireWeapon(pChr, Direction, Weapon);
}

bool CGameControllerInstaLTS::IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const
{
	if(Damage < INSTA_MAGIC_MIN_DAMAGE || ClientID1 == ClientID2)
		return true;
	return IGameController::IsFriendlyFire(ClientID1, ClientID2, Damage);
}

bool CGameControllerInstaLTS::IsFriendlyTeamFire(int Team1, int Team2, int Damage) const
{
	if(Damage < INSTA_MAGIC_MIN_DAMAGE || Team1 == Team2)
		return true;
	return IGameController::IsFriendlyFire(Team1, Team2, Damage);
}

REGISTER_GAMEMODE("ilts", CGameControllerInstaLTS);
REGISTER_GAMEMODE_ALIAS(Grenade, "glts", CGameControllerInstaLTS);
