/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hunter.h"
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <engine/shared/config.h>

CGameControllerHunter::CGameControllerHunter(class CGameContext *pGameServer) : IGameController(pGameServer)
{
	m_pGameType = "hunter";

	m_GameFlags = GAMEFLAG_SURVIVAL;
	m_MaxPlayerSlots = 64;
	mem_zero(m_aPlayerData, sizeof(m_aPlayerData));

#define REGISTER_HUNTER_CLASS(CLASS, CLASS_ENUM) \
	m_apClass[CLASS_ENUM - 1] = new CLASS(this);
	#include "class.h"
#undef REGISTER_HUNTER_CLASS
	//Config()->m_SvTournamentMode = 1;
}

REGISTER_GAMEMODE("hunter", CGameControllerHunter);

CGameControllerHunter::~CGameControllerHunter()
{
#define REGISTER_HUNTER_CLASS(CLASS, CLASS_ENUM) \
	delete (CLASS *)m_apClass[CLASS_ENUM - 1];
#undef REGISTER_HUNTER_CLASS
}

bool CGameControllerHunter::CanCharacterWeaponFullAuto(class CCharacter *pChr, int Weapon)
{
	return false;
}

void CGameControllerHunter::DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	int CID = pPlayer->GetCID();
	if(Team != pPlayer->GetTeam() && Team == TEAM_SPECTATORS)
		m_aPlayerData[CID].m_Stat = SPlayerData::SPECTATING;
	IGameController::DoTeamChange(pPlayer, Team, DoChatMsg);
}

bool CGameControllerHunter::IsFriendlyFire(int ClientID1, int ClientID2) const
{
	return false;
}

bool CGameControllerHunter::IsFriendlyTeamFire(int Team1, int Team2) const
{
	return false;
}

int CGameControllerHunter::OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon)
{
	return 0;
}

int CGameControllerHunter::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_DeadSpecMode)
			GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();

	int VictimCID = pVictim->GetPlayer()->GetCID();
	int KillerCID = pKiller->GetCID();
	int Ret = 0;
	if(m_aPlayerData[KillerCID].m_Class)
		m_apClass[m_aPlayerData[KillerCID].m_Class - 1]->OnKill(pVictim, pKiller, Weapon);
	if(m_aPlayerData[VictimCID].m_Class)
		Ret = m_apClass[m_aPlayerData[VictimCID].m_Class - 1]->OnCharacterDeath(pVictim, pKiller, Weapon);
	return Ret;
}

void CGameControllerHunter::OnCharacterSpawn(class CCharacter *pChr)
{
	int CID = pChr->GetPlayer()->GetCID();
	if(m_aPlayerData[CID].m_Class)
		m_apClass[m_aPlayerData[CID].m_Class - 1]->OnCharacterSpawn(pChr);
	if(m_aPlayerData[CID].m_Class == ID_CIVIC)
		GameServer()->SendBroadcast(CID, "tt");
}

void CGameControllerHunter::OnPlayerConnect(class CPlayer *pPlayer)
{
	int ClientID = pPlayer->GetCID();
	pPlayer->Respawn();

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), pPlayer->GetTeam());
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	SendGameInfo(ClientID);

	m_aPlayerData[ClientID].m_Stat = SPlayerData::SPECTATING;

	if(!IsGameRunning())
		SetGameState(IGS_WARMUP_GAME, 0);
}

void CGameControllerHunter::OnPlayerDisconnect(class CPlayer *pPlayer)
{
	int CID = pPlayer->GetCID();
	m_aPlayerData[CID].Reset(SPlayerData::OFFLINE);
}

void CGameControllerHunter::OnRoundStart()
{
	int64 PlayerMask = 0;
	int PlayerCount = 0;
	int RandPlayerCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aPlayerData[i].m_Stat == SPlayerData::SPECTATING && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			m_aPlayerData[i].Reset();

		if(m_aPlayerData[i].m_Stat != SPlayerData::IN_GAME)
			continue;

		PlayerMask |= 1 << i;
		PlayerCount++;
		if(m_HunterRandMask & CmaskOne(i))
			RandPlayerCount++;
	}

	if(PlayerCount < 2)
	{
		SetGameState(IGS_WARMUP_GAME, TIMER_INFINITE);
		return;
	}
	else if(!IsGameRunning())
		SetGameState(IGS_WARMUP_GAME, 0);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aPlayerData[i].m_Stat != SPlayerData::IN_GAME)
			continue;
		
		m_aPlayerData[i].m_Class = ID_CIVIC;
		m_aPlayerData[i].m_Team = TEAM_RED;
	}

	int HunterNum = clamp((PlayerCount + 1) / 4, 1, (int)MAX_CLIENTS / 4);
	if(HunterNum > RandPlayerCount)
		m_HunterRandMask = PlayerMask & ~(m_HunterRandMask);
	else
		m_HunterRandMask &= PlayerMask;
	for(int i = 0; i < HunterNum; i++)
	{
		int RandPlayer = rand() % RandPlayerCount;
		
		int j;
		for(j = 0; j < MAX_CLIENTS; j++)
		{
			if(!(m_HunterRandMask & CmaskOne(j)) || RandPlayer--)
				continue;
			
			m_aPlayerData[j].m_Class = ID_HUNTER;
			m_aPlayerData[j].m_Team = TEAM_BLUE;
			break;
		}
		dbg_assert(j != MAX_CLIENTS, "error on hunter select");
	}
}

void CGameControllerHunter::Tick()
{
	IGameController::Tick();
}