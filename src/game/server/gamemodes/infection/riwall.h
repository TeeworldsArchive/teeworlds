#ifndef GAME_SERVER_GAMEMODES_INFECTION_RIWALL_H
#define GAME_SERVER_GAMEMODES_INFECTION_RIWALL_H

#include <game/server/entity.h>

class CRIWall : public CEntity
{
public:
	CRIWall(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner);

	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

	int GetOwner() { return m_Owner; }
protected:
	bool CheckHit(class CCharacter *pChr);
	bool IsActive();
	vec2 GetPoint2();

private:
	vec2 m_Dir;
	int m_StartTick;
	int m_Owner;
	int m_Team;
};

#endif // GAME_SERVER_GAMEMODES_INFECTION_RIWALL_H
