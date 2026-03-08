#include <base/color.h>
#include <base/tl/inplace_array.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <generated/server_data.h>

#include "reinfected.h"
#include "riwall.h"

class CReinfectedHelper
{
public:
	CGameControllerReinfected *m_pController;

	CTeeInfos m_aTeeInfos[MAX_CLIENTS];
	CTeeInfos m_aInfectedInfos[MAX_CLIENTS];
	bool m_aPlayerInfected[MAX_CLIENTS];
	bool m_aPlayerInTheList[MAX_CLIENTS];
	int m_InfectedNum;
	int m_HumanNum;

	CReinfectedHelper(CGameControllerReinfected *pController)
	{
		m_pController = pController;
		mem_zero(m_aTeeInfos, sizeof(m_aTeeInfos));
		mem_zero(m_aInfectedInfos, sizeof(m_aInfectedInfos));
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			m_aPlayerInfected[i] = false;
			m_aPlayerInTheList[i] = false;
		}
		m_InfectedNum = 0;
		m_HumanNum = 0;
	}

	void Infect(int ClientID)
	{
		if(IsInfected(ClientID))
			return;

		RemovePlayerFromList(ClientID);
		m_aPlayerInfected[ClientID] = true;
		AddPlayerToList(ClientID);

		m_pController->RefreshClientSkin(ClientID, false);
	}

	void Cure(int ClientID)
	{
		if(!IsInfected(ClientID))
			return;

		RemovePlayerFromList(ClientID);
		m_aPlayerInfected[ClientID] = false;
		AddPlayerToList(ClientID);

		m_pController->RefreshClientSkin(ClientID, false);
	}

	void ResetGame()
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!m_pController->GameServer()->m_apPlayers[i])
				continue;
			Cure(i);
		}
	}

	void AddPlayerToList(int ClientID)
	{
		if(m_aPlayerInTheList[ClientID])
			return;
		m_aPlayerInTheList[ClientID] = true;
		if(IsInfected(ClientID))
			m_InfectedNum++;
		else
			m_HumanNum++;
	}

	void RemovePlayerFromList(int ClientID)
	{
		if(!m_aPlayerInTheList[ClientID])
			return;
		m_aPlayerInTheList[ClientID] = false;
		if(IsInfected(ClientID))
			m_InfectedNum--;
		else
			m_HumanNum--;
		m_pController->CheckCancelGame();
	}

	bool IsInfected(CCharacter *pCharacter) const { return IsInfected(pCharacter->GetCID()); }
	bool IsInfected(int ClientID) const { return m_aPlayerInfected[ClientID]; }
};

inline int GetInfectedColor(int UseCustomColors, int PartColor, int Part)
{
	static const int s_InfectedColors[2] = {0x3AFF00, 0x00FF86};

	int InfectedHue = (s_InfectedColors[Part == SKINPART_FEET ? 1 : 0] >> 16) & 0xff;
	int InfectedSat = (s_InfectedColors[Part == SKINPART_FEET ? 1 : 0] >> 8) & 0xff;
	int InfectedLgt = s_InfectedColors[Part == SKINPART_FEET ? 1 : 0] & 0xff;
	int PartSat = (PartColor >> 8) & 0xff;
	int PartLgt = PartColor & 0xff;

	if(!UseCustomColors)
	{
		PartSat = 255;
		PartLgt = 255;
	}

	int MinSat = 160;
	int MaxSat = 255;

	int h = InfectedHue;
	int s = clamp(mix(InfectedSat, PartSat, 0.2), MinSat, MaxSat);
	int l = clamp(mix(InfectedLgt, PartLgt, 0.2), 61, 200);

	int ColorVal = (h << 16) + (s << 8) + l;
	if(Part == SKINPART_MARKING) // keep alpha
		ColorVal += PartColor & 0xff000000;

	return ColorVal;
}

void CGameControllerReinfected::RefreshPlayerSkin(CPlayer *pPlayer, bool Sync)
{
	int ClientID = pPlayer->GetCID();
	if(Sync)
	{
		Reinfected()->m_aInfectedInfos[ClientID] = Reinfected()->m_aTeeInfos[ClientID] = pPlayer->m_TeeInfos;

		for(int p = 0; p < NUM_SKINPARTS; p++)
		{
			int ColorVal = GetInfectedColor(pPlayer->m_TeeInfos.m_aUseCustomColors[p], pPlayer->m_TeeInfos.m_aSkinPartColors[p], p);
			Reinfected()->m_aInfectedInfos[ClientID].m_aSkinPartColors[p] = ColorVal;
		}
	}
	if(Reinfected()->IsInfected(ClientID))
		pPlayer->m_TeeInfos = Reinfected()->m_aInfectedInfos[ClientID];
	else
		pPlayer->m_TeeInfos = Reinfected()->m_aTeeInfos[ClientID];
}

bool CGameControllerReinfected::IsInfectionStarted()
{
	return (Server()->Tick() - m_GameStartTick) > (Server()->TickSpeed() * Config()->m_RiInfectionStartTime) && IsGameRunning();
}

void CGameControllerReinfected::StartRandomInfection()
{
	int InfectedNum = 3;
	if(GetRealPlayerNum() < 4)
		InfectedNum = 1;
	else if(GetRealPlayerNum() < 8)
		InfectedNum = 2;

	InfectedNum -= Reinfected()->m_InfectedNum;
	if(InfectedNum > 0)
	{
		inplace_array<int, MAX_CLIENTS> aPlayers;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				aPlayers.add(i);
		}

		char aBuf[128];
		for(int i = 0; i < InfectedNum; i++)
		{
			int RandomInfected = random_int() % aPlayers.size();
			if(Reinfected()->IsInfected(i))
			{
				i--;
				continue;
			}
			Infect(aPlayers[RandomInfected]);
			str_format(aBuf, sizeof(aBuf), "'%s' has been infected!", Server()->ClientName(aPlayers[RandomInfected]));
			aPlayers.remove_index_fast(RandomInfected);
			GameServer()->SendChat(-1, CHAT_ALL, -1, aBuf);
		}
	}
}

void CGameControllerReinfected::Infect(int InfectedID)
{
	Reinfected()->Infect(InfectedID);
	if(GameServer()->GetPlayerChar(InfectedID))
	{
		GameServer()->GetPlayerChar(InfectedID)->DisableWeapon(-1);
		GameServer()->GetPlayerChar(InfectedID)->EnableWeapon(WEAPON_HAMMER);
		GameServer()->GetPlayerChar(InfectedID)->SetWeapon(WEAPON_HAMMER);
	}
}

void CGameControllerReinfected::Cure(int CureID)
{
	Reinfected()->Cure(CureID);
	if(GameServer()->GetPlayerChar(CureID))
		GameServer()->GetPlayerChar(CureID)->EnableWeapon(-1);
}

void CGameControllerReinfected::AddScoreForInfection(int InfectedID)
{
	if(InfectedID < 0 || !GameServer()->m_apPlayers[InfectedID])
		return;
	GameServer()->m_apPlayers[InfectedID]->m_Score += 3;
}

CGameControllerReinfected::CGameControllerReinfected(CGameContext *pGameServer) : IGameController(pGameServer)
{
	m_pGameType = "Reinfected";
	m_pHelper = new CReinfectedHelper(this);
}

CGameControllerReinfected::~CGameControllerReinfected()
{
	delete m_pHelper;
}

bool CGameControllerReinfected::IsFriendlyFire(int ClientID1, int ClientID2) const
{
	if(ClientID1 == ClientID2)
		return false;

	if(!GameServer()->m_apPlayers[ClientID1] || !GameServer()->m_apPlayers[ClientID2])
		return false;

	if(!Config()->m_SvTeamdamage && Reinfected()->IsInfected(ClientID1) == Reinfected()->IsInfected(ClientID2))
		return true;

	return false;
}

bool CGameControllerReinfected::IsFriendlyTeamFire(int Team1, int Team2) const
{
	return Team1 == Team2;
}

int CGameControllerReinfected::GetPlayerCheckTeam(CPlayer *pPlayer) const
{
	if(!pPlayer)
		return RITEAM_NONE;
	return Reinfected()->IsInfected(pPlayer->GetCID()) ? RITEAM_INFECTED : RITEAM_HUMAN;
}

void CGameControllerReinfected::OnRoundStart()
{
	Reinfected()->ResetGame();
}

void CGameControllerReinfected::OnPlayerConnect(CPlayer *pPlayer)
{
	IGameController::OnPlayerConnect(pPlayer);
	if(IsInfectionStarted())
		Infect(pPlayer->GetCID());
	Reinfected()->AddPlayerToList(pPlayer->GetCID());
	RefreshPlayerSkin(pPlayer, true);
}

void CGameControllerReinfected::OnPlayerDisconnect(CPlayer *pPlayer)
{
	IGameController::OnPlayerDisconnect(pPlayer);
	Reinfected()->RemovePlayerFromList(pPlayer->GetCID());
}

void CGameControllerReinfected::OnPlayerInfoChange(CPlayer *pPlayer)
{
	IGameController::OnPlayerInfoChange(pPlayer);
	RefreshPlayerSkin(pPlayer, true);
}

bool CGameControllerReinfected::CanCharacterPickup(CCharacter *pChr) const
{
	if(!pChr || !pChr->GetPlayer() || Reinfected()->IsInfected(pChr->GetCID()))
		return false;

	return IGameController::CanCharacterPickup(pChr);
}

int CGameControllerReinfected::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	if(IsInfectionStarted() && Weapon != WEAPON_GAME)
		Infect(pVictim->GetCID());

	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;

	if(pKiller == pVictim->GetPlayer())
		pVictim->GetPlayer()->m_Score--; // suicide or world
	else
	{
		if(Reinfected()->IsInfected(pVictim) == Reinfected()->IsInfected(pKiller->GetCID()))
			pKiller->m_Score--; // teamkill
		else
		{
			if(Reinfected()->IsInfected(pKiller->GetCID()))
				AddScoreForInfection(pKiller->GetCID()); // normal kill
			else
				pKiller->m_Score++;
		}
	}
	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() * 3.0f;

	// update spectator modes for dead players in survival
	if(m_GameFlags & GAMEFLAG_SURVIVAL)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_DeadSpecMode)
				GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();
	}

	return 0;
}

int CGameControllerReinfected::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	if(!pChr)
		return IGameController::OnCharacterFireWeapon(pChr, Direction, Weapon);

	if(Weapon == WEAPON_HAMMER)
	{
		int ClientID = pChr->GetCID();
		vec2 ChrPos = pChr->GetPos();
		vec2 ProjStartPos = ChrPos + Direction * pChr->GetProximityRadius() * 0.75f;

		if(!Reinfected()->IsInfected(pChr))
		{
			for(CRIWall *pWall = static_cast<CRIWall *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_RIWALL)); pWall; pWall = static_cast<CRIWall *>(pWall->TypeNext()))
			{
				if(pWall->GetOwner() == ClientID) pWall->Reset();
			}
			new CRIWall(&GameServer()->m_World, ChrPos, Direction, ClientID);
			GameServer()->CreateSound(ChrPos, SOUND_LASER_BOUNCE);
			return IGameController::OnCharacterFireWeapon(pChr, Direction, Weapon);
		}

		// infection hammer
		int ReloadTimer = g_pData->m_Weapons.m_aId[WEAPON_HAMMER].m_Firedelay * Server()->TickSpeed() / 1000;

		GameServer()->CreateSound(ChrPos, SOUND_HAMMER_FIRE);

		CCharacter *apEnts[MAX_CLIENTS];
		int Hits = 0;
		int Num = GameServer()->m_World.FindEntities(ProjStartPos, pChr->GetProximityRadius() * 0.5f, (CEntity **) apEnts,
			MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

		for(int i = 0; i < Num; ++i)
		{
			CCharacter *pTarget = apEnts[i];

			if((pTarget == pChr) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), NULL, NULL))
				continue;

			// set his velocity to fast upward (for now)
			if(length(pTarget->GetPos() - ProjStartPos) > 0.0f)
				GameServer()->CreateHammerHit(pTarget->GetPos() - normalize(pTarget->GetPos() - ProjStartPos) * pChr->GetProximityRadius() * 0.5f);
			else
				GameServer()->CreateHammerHit(ProjStartPos);

			vec2 Dir;
			if(length(pTarget->GetPos() - ChrPos) > 0.0f)
				Dir = normalize(pTarget->GetPos() - ChrPos);
			else
				Dir = vec2(0.f, -1.f);

			if(!Reinfected()->IsInfected(pTarget))
			{
				Infect(pTarget->GetCID());
				AddScoreForInfection(ClientID);
			}
			else
			{
				pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, Dir * -1, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
					ClientID, Weapon);
			}
			Hits++;
		}

		// if we Hit anything, we have to wait for the reload
		if(Hits)
			ReloadTimer = Server()->TickSpeed() / 3;

		return ReloadTimer;
	}

	return IGameController::OnCharacterFireWeapon(pChr, Direction, Weapon);
}

void CGameControllerReinfected::OnCharacterSpawn(CCharacter *pChr)
{
	IGameController::OnCharacterSpawn(pChr);
	if(Reinfected()->IsInfected(pChr))
	{
		pChr->DisableWeapon(-1);
		pChr->EnableWeapon(WEAPON_HAMMER);
		pChr->SetWeapon(WEAPON_HAMMER);
	}
}

void CGameControllerReinfected::Tick()
{
	IGameController::Tick();

	if(GameServer()->m_World.m_ResetRequested || GameServer()->m_World.m_Paused)
		return;

	DoWincheckMatch();
}

bool CGameControllerReinfected::DoWincheckMatch()
{
	if(GetRealPlayerNum() < Config()->m_RiPlayersMin)
		return false; // game should not start

	if(Reinfected()->m_InfectedNum > 0 && Reinfected()->m_HumanNum == 0)
	{
		EndMatch();
		GameServer()->SendChat(-1, CHAT_ALL, -1, "It's zombie time!");
		return true;
	}
	else if(m_GameInfo.m_TimeLimit > 0 && (Server()->Tick() - m_GameStartTick) >= m_GameInfo.m_TimeLimit * Server()->TickSpeed() * 60)
	{
		EndMatch();
		GameServer()->SendChat(-1, CHAT_ALL, -1, "Don't be panic!");
		return true;
	}

	if(IsInfectionStarted())
	{
		StartRandomInfection();
	}

	return false;
}

void CGameControllerReinfected::DoTeamChange(CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	if(Team == TEAM_SPECTATORS)
		Reinfected()->RemovePlayerFromList(pPlayer->GetCID());
	else
	{
		if(IsInfectionStarted())
			Infect(pPlayer->GetCID());
		Reinfected()->AddPlayerToList(pPlayer->GetCID());
	}

	IGameController::DoTeamChange(pPlayer, Team, DoChatMsg);
}

void CGameControllerReinfected::RefreshClientSkin(int ClientID, bool Sync)
{
	if(!GameServer()->m_apPlayers[ClientID])
		return;
	RefreshPlayerSkin(GameServer()->m_apPlayers[ClientID], Sync);
	GameServer()->SendSkinChange(ClientID, -1);
}

void CGameControllerReinfected::CheckCancelGame()
{
	if(!(GetRealPlayerNum() < Config()->m_RiPlayersMin && IsGameRunning()))
		return;
	SetGameState(IGameController::IGS_WARMUP_GAME, TIMER_INFINITE);
	GameServer()->SendChat(-1, CHAT_ALL, -1, "Game has been cancelled");
}

REGISTER_GAMEMODE("reinfected", CGameControllerReinfected);
