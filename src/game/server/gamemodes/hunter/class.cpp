/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/entities/character.h>
#include "hunter.h"
#include "class.h"

CGameControllerHunter::IClass::IClass(class CGameControllerHunter *pController)
{
	m_pController = pController;
}

void CGameControllerHunter::IClass::Tick()
{

}

bool CGameControllerHunter::IClass::CanCharacterPickup(class CCharacter *pChr) const
{
	return true;
}

bool CGameControllerHunter::IClass::CanCharacterWeaponFullAuto(class CCharacter *pChr, int Weapon)
{
	return m_pController->IGameController::CanCharacterWeaponFullAuto(pChr, Weapon);
}

bool CGameControllerHunter::IClass::IsFriendlyFire(int ClientID1, int ClientID2) const
{
	return m_pController->IGameController::IsFriendlyFire(ClientID1, ClientID2);
}

bool CGameControllerHunter::IClass::IsFriendlyTeamFire(int Team1, int Team2) const
{
	return m_pController->IGameController::IsFriendlyTeamFire(Team1, Team2);
}

int CGameControllerHunter::IClass::OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon)
{
	return m_pController->IGameController::OnCharacterFireWeapon(pChr, Direction, Weapon);
}

int CGameControllerHunter::IClass::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	return 0;
}

void CGameControllerHunter::IClass::OnCharacterSpawn(class CCharacter *pChr)
{
	pChr->IncreaseHealth(10);
}

void CGameControllerHunter::IClass::OnKill(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	
}
