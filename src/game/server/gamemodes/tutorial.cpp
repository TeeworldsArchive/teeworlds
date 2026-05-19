#include <engine/console.h>
#include <game/server/entities/character.h>
#include <game/server/entities/flag.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include "tutorial.h"

CGameControllerTutorial::CGameControllerTutorial(CGameContext *pGameServer) : IGameController(pGameServer)
{
	m_apFlags[0] = 0;
	m_apFlags[1] = 0;
	m_pGameType = "Tutorial";
	m_GameFlags = GAMEFLAG_FLAGS;
	m_Trigger = TRIGGER_START;
}

void CGameControllerTutorial::Tick()
{
	if(m_Trigger != TRIGGER_CAVE)
		return;

	for(int fi = 0; fi < 2; fi++)
	{
		CFlag *F = m_apFlags[fi];

		if(!F)
			continue;

		//
		if(F->GetCarrier())
		{
			if(m_apFlags[fi ^ 1] && m_apFlags[fi ^ 1]->IsAtStand())
			{
				if(distance(F->GetPos(), m_apFlags[fi ^ 1]->GetPos()) < CFlag::ms_PhysSize + CCharacter::ms_PhysSize)
				{
					CCharacter *pCarrier = F->GetCarrier();
					F->GetCarrier()->GetPlayer()->m_Score += 5;
					float Diff = Server()->Tick() - F->GetGrabTick();
					GameServer()->SendGameMsg(GAMEMSG_CTF_CAPTURE, fi, F->GetCarrier()->GetCID(), Diff, -1);
					for(int i = 0; i < 2; i++)
						m_apFlags[i]->Reset();
					GameServer()->SendChat(-1, CHAT_ALL, pCarrier->GetCID(), Localize("The CTF training is over."));
					GameServer()->SendChat(-1, CHAT_ALL, pCarrier->GetCID(), Localize("Now I have set up some obstacles, and you need to finish it."));
					m_Trigger = TRIGGER_FLAGS;
					pCarrier->TeleTo(m_aaSpawnPoints[TEAM_RED + 1][0], true);
				}
			}
		}
		else
		{
			CCharacter *apCloseCCharacters[MAX_CLIENTS];
			int Num = GameServer()->m_World.FindEntities(F->GetPos(), CFlag::ms_PhysSize, (CEntity **) apCloseCCharacters, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
			for(int i = 0; i < Num; i++)
			{
				if(!apCloseCCharacters[i]->IsAlive() || apCloseCCharacters[i]->GetPlayer()->GetTeam() == TEAM_SPECTATORS || GameServer()->Collision()->IntersectLine(F->GetPos(), apCloseCCharacters[i]->GetPos(), NULL, NULL))
					continue;

				if(apCloseCCharacters[i]->GetPlayer()->GetTeam() == F->GetTeam())
				{
					// return the flag
					if(!F->IsAtStand())
					{
						CCharacter *pChr = apCloseCCharacters[i];
						pChr->GetPlayer()->m_Score += 1;
						GameServer()->SendGameMsg(GAMEMSG_CTF_RETURN, -1);
						F->Reset();
					}
				}
				else
				{
					// take the flag
					if(F->IsAtStand())
						m_aTeamscore[fi ^ 1]++;
					F->Grab(apCloseCCharacters[i]);
					F->GetCarrier()->GetPlayer()->m_Score += 1;
					GameServer()->SendGameMsg(GAMEMSG_CTF_GRAB, fi, -1);
					GameServer()->SendChat(-1, CHAT_ALL, F->GetCarrier()->GetCID(), Localize("Good job! Now you can capture this flag for other flag's team!"));
					break;
				}
			}
		}
	}
}

bool CGameControllerTutorial::OnEntity(int Index, vec2 Pos)
{
	if(IGameController::OnEntity(Index, Pos))
		return true;

	int Team = -1;
	if(Index == ENTITY_FLAGSTAND_RED)
		Team = TEAM_RED;
	if(Index == ENTITY_FLAGSTAND_BLUE)
		Team = TEAM_BLUE;
	if(Team == -1 || m_apFlags[Team])
		return false;
	m_apFlags[Team] = new CFlag(&GameServer()->m_World, Team, Pos);
	return true;
}

bool CGameControllerTutorial::OnExtraTile(int Index, vec2 Pos)
{
	int Flag = -1;
	switch(Index)
	{
		case TILE_TRIGGER_WEAPONS:
		case TILE_TRIGGER_CAVE:
		case TILE_TRIGGER_FINISH:
			Flag = 1 << Index;
			break;
	}
	if(Flag == -1)
		return false;
	GameServer()->Collision()->SetFlagFor(Pos, Flag);
	return true;
}

void CGameControllerTutorial::OnFlagReturn(CFlag *pFlag)
{
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "flag_return");
	GameServer()->SendGameMsg(GAMEMSG_CTF_RETURN, -1);
}

void CGameControllerTutorial::OnPlayerConnect(CPlayer *pPlayer)
{
	pPlayer->TryRespawn();
	GameServer()->SendChat(-1, CHAT_ALL, pPlayer->GetCID(), Localize("Hello! It's tutorial day! Moving yourself to the right of this cave."));
}

void CGameControllerTutorial::OnPlayerDisconnect(CPlayer *pPlayer)
{
	GameServer()->Console()->ExecuteLine("shutdown");
}

void CGameControllerTutorial::HandleCharacterTiles(CCharacter *pChr, vec2 LastPos, vec2 NewPos)
{
	int Flag = GameServer()->Collision()->TestBoxMoveAt(LastPos, NewPos, vec2(CCharacter::ms_PhysSize, CCharacter::ms_PhysSize));
	if(Flag & (1 << TILE_TRIGGER_WEAPONS) && m_Trigger == TRIGGER_START)
	{
		m_Trigger = TRIGGER_WEAPONS;
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("Feel free to use these weapons! And, look the top left of your screen, those are your ammos."));
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("I guess you need to get out of this cave now."));
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("Just hook the blocks above! It's usually triggered by your right mouse button."));
	}
	else if(Flag & (1 << TILE_TRIGGER_CAVE) && m_Trigger == TRIGGER_WEAPONS)
	{
		m_Trigger = TRIGGER_CAVE;
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("Nice job! Now we can see there are two flags out of the cave."));
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("You can pick one of them up."));
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("And then if you get close to the other one, you will capture the flag for that flag's team."));
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("Really easy, isn't it?"));
	}
	else if(Flag & (1 << TILE_TRIGGER_FINISH) && m_Trigger == TRIGGER_FLAGS)
	{
		m_Trigger = TRIGGER_FINISH;
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("You have finished this tutorial!"));
		GameServer()->SendChat(-1, CHAT_ALL, pChr->GetCID(), Localize("Now I suggest joining other servers!"));
	}
}

REGISTER_GAMEMODE("tutorial", CGameControllerTutorial);
