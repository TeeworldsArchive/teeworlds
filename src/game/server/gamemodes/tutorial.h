#ifndef GAME_SERVER_GAMEMODES_TUTORIAL_H
#define GAME_SERVER_GAMEMODES_TUTORIAL_H
#include <game/server/gamecontroller.h>

class CGameControllerTutorial : public IGameController
{
	enum
	{
		TRIGGER_START = 0,
		TRIGGER_WEAPONS,
		TRIGGER_CAVE,
		TRIGGER_FLAGS,
		TRIGGER_FINISH,
		NUM_TRIGGERS,

		TILE_TRIGGER_WEAPONS = 16,
		TILE_TRIGGER_CAVE,
		TILE_TRIGGER_FINISH,
	};
	int m_Trigger;
	class CFlag *m_apFlags[2];

public:
	CGameControllerTutorial(class CGameContext *pGameServer);
	virtual void Tick();
	virtual bool OnEntity(int Index, vec2 Pos);
	virtual bool OnExtraTile(int Index, vec2 Pos);
	virtual bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const { return true; };
	virtual void OnFlagReturn(class CFlag *pFlag);
	virtual void OnPlayerConnect(class CPlayer *pPlayer);
	virtual void OnPlayerDisconnect(class CPlayer *pPlayer);
	virtual void HandleCharacterTiles(class CCharacter *pChr, vec2 LastPos, vec2 NewPos);
	virtual bool IsPureTuning() const { return true; }
};
#endif
