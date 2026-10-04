/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_SERVER_ENTITIES_PICKUP_H
#define GAME_SERVER_ENTITIES_PICKUP_H

#include <game/server/entity.h>

const int PickupPhysSize = 14;

class CPickup : public CEntity
{
public:
	CPickup(CGameWorld *pGameWorld, int Type, vec2 Pos);

	virtual void Reset() override;
	virtual void Tick() override;
	virtual void TickPaused() override;
	virtual void Snap(int SnappingClient) override;

private:
	int m_Type;
	int m_SpawnTick;
};

#endif
