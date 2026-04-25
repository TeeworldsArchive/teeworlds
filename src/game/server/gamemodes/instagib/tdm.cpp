/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include "tdm.h"

CGameControllerInstaTDM::CGameControllerInstaTDM(CGameContext *pGameServer) : CGameControllerTDM(pGameServer)
{
	InitCore(str_startswith_nocase(Config()->m_SvGametype, "g"));
	m_pGameType = m_GreandeMode ? "gTDM" : "iTDM";
	m_MaxPlayerSlots = MAX_CLIENTS;
}

void CGameControllerInstaTDM::OnCharacterSpawn(CCharacter *pChr)
{
	IGameController::OnCharacterSpawn(pChr);
	SetBasicWeapon(pChr);
}

int CGameControllerInstaTDM::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	return CInstaCore::OnCharacterFireWeapon(pChr, Direction, Weapon);
}

bool CGameControllerInstaTDM::IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const
{
	if(Damage < INSTA_MAGIC_MIN_DAMAGE || ClientID1 == ClientID2)
		return true;
	return IGameController::IsFriendlyFire(ClientID1, ClientID2, Damage);
}

bool CGameControllerInstaTDM::IsFriendlyTeamFire(int Team1, int Team2, int Damage) const
{
	if(Damage < INSTA_MAGIC_MIN_DAMAGE || Team1 == Team2)
		return true;
	return IGameController::IsFriendlyFire(Team1, Team2, Damage);
}

REGISTER_GAMEMODE("itdm", CGameControllerInstaTDM);
REGISTER_GAMEMODE_ALIAS(Grenade, "gtdm", CGameControllerInstaTDM);
