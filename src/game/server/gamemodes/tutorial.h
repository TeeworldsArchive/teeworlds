/* (c) Teeworlds Archive Project Contributors. See license.txt. */
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
	virtual void Tick() override;
	virtual bool OnEntity(int Index, vec2 Pos) override;
	virtual bool OnExtraTile(int Index, vec2 Pos) override;
	virtual bool IsFriendlyFire([[maybe_unused]] int ClientID1, [[maybe_unused]] int ClientID2, [[maybe_unused]] int Damage) const override { return true; };
	virtual void OnFlagReturn(class CFlag *pFlag) override;
	virtual void OnPlayerConnect(class CPlayer *pPlayer) override;
	virtual void OnPlayerDisconnect(class CPlayer *pPlayer) override;
	virtual void HandleCharacterTiles(class CCharacter *pChr, vec2 LastPos, vec2 NewPos) override;
	virtual bool IsPureTuning() const override { return true; }
};
#endif
