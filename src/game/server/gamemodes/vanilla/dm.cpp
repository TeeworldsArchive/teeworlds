/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include "dm.h"

CGameControllerDM::CGameControllerDM(CGameContext *pGameServer) : IGameController(pGameServer)
{
	/*
	m_pGameType = "DM";
	*/
	m_pGameType = "DM*";
	m_MaxPlayerSlots = 16;
}

REGISTER_GAMEMODE("dm", CGameControllerDM);
