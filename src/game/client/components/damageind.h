/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_DAMAGEIND_H
#define GAME_CLIENT_COMPONENTS_DAMAGEIND_H
#include <base/vmath.h>
#include <game/client/component.h>

class CDamageInd : public CComponent
{
	struct CItem
	{
		vec2 m_Pos;
		vec2 m_Dir;
		float m_LifeTime;
		float m_StartAngle;
	};

	enum
	{
		MAX_ITEMS = 64,
	};

	CItem m_aItems[MAX_ITEMS];
	int m_NumItems;

	CItem *CreateItem();
	void DestroyItem(CItem *pItem);

public:
	CDamageInd();

	void Create(vec2 Pos, vec2 Dir);

	virtual void OnRender() override;
	virtual void OnReset() override;
};
#endif
