/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include "ctf.h"

CGameControllerInstaCTF::CGameControllerInstaCTF(CGameContext *pGameServer) : CGameControllerCTF(pGameServer)
{
	InitCore(str_startswith_nocase(Config()->m_SvGametype, "g"));
	m_pGameType = m_GreandeMode ? "gCTF" : "iCTF";
	m_MaxPlayerSlots = MAX_CLIENTS;
}

void CGameControllerInstaCTF::OnCharacterSpawn(CCharacter *pChr)
{
	IGameController::OnCharacterSpawn(pChr);
	SetBasicWeapon(pChr);
}

int CGameControllerInstaCTF::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	return CInstaCore::OnCharacterFireWeapon(pChr, Direction, Weapon);
}

bool CGameControllerInstaCTF::IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const
{
	if(Damage < INSTA_MAGIC_MIN_DAMAGE || ClientID1 == ClientID2)
		return true;
	return IGameController::IsFriendlyFire(ClientID1, ClientID2, Damage);
}

bool CGameControllerInstaCTF::IsFriendlyTeamFire(int Team1, int Team2, int Damage) const
{
	if(Damage < INSTA_MAGIC_MIN_DAMAGE || Team1 == Team2)
		return true;
	return IGameController::IsFriendlyFire(Team1, Team2, Damage);
}

REGISTER_GAMEMODE("ictf", CGameControllerInstaCTF);
REGISTER_GAMEMODE_ALIAS(Grenade, "gctf", CGameControllerInstaCTF);
