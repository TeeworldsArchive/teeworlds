#include <engine/shared/config.h>
#include <game/server/gamemodes/vanilla/ctf.h>
#include <game/server/gamemodes/vanilla/dm.h>
#include <game/server/gamemodes/vanilla/lms.h>
#include <game/server/gamemodes/vanilla/lts.h>
#include <game/server/gamemodes/vanilla/tdm.h>

#include "core.h"

#define INSTAGIB_MODE(Mode, Lowercase) \
	class CGameControllerInsta##Mode : public CGameController##Mode, CInstaCore \
	{ \
	public: \
		CGameControllerInsta##Mode(CGameContext *pGameServer) : CGameController##Mode(pGameServer) \
		{ \
			InitCore(str_startswith_nocase(Config()->m_SvGametype, "g")); \
			m_pGameType = m_GreandeMode ? "g" #Mode : "i" #Mode; \
			m_MaxPlayerSlots = MAX_CLIENTS; \
		} \
\
		virtual void OnCharacterSpawn(CCharacter *pChr) \
		{ \
			IGameController::OnCharacterSpawn(pChr); \
			SetBasicWeapon(pChr); \
		} \
\
		virtual int OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon) \
		{ \
			return CInstaCore::OnCharacterFireWeapon(pChr, Direction, Weapon); \
		} \
\
		virtual bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const \
		{ \
			if(Damage < INSTA_MAGIC_MIN_DAMAGE || ClientID1 == ClientID2) \
				return true; \
			return IGameController::IsFriendlyFire(ClientID1, ClientID2, Damage); \
		} \
\
		virtual bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const \
		{ \
			if(Damage < INSTA_MAGIC_MIN_DAMAGE || Team1 == Team2) \
				return true; \
			return IGameController::IsFriendlyFire(Team1, Team2, Damage); \
		} \
	}; \
\
	REGISTER_GAMEMODE("i" #Lowercase, CGameControllerInsta##Mode); \
	REGISTER_GAMEMODE_ALIAS(Grenade, "g" #Lowercase, CGameControllerInsta##Mode)

INSTAGIB_MODE(CTF, ctf);
INSTAGIB_MODE(DM, dm);
INSTAGIB_MODE(LMS, lms);
INSTAGIB_MODE(LTS, lts);
INSTAGIB_MODE(TDM, tdm);
