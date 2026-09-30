/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_ANIMSTATE_H
#define GAME_CLIENT_ANIMSTATE_H

#include <generated/client_data.h>

class CAnimState
{
	CAnimKeyframe m_Body;
	CAnimKeyframe m_BackFoot;
	CAnimKeyframe m_FrontFoot;
	CAnimKeyframe m_Attach;

public:
	CAnimKeyframe *GetBody() { return &m_Body; }
	CAnimKeyframe *GetBackFoot() { return &m_BackFoot; }
	CAnimKeyframe *GetFrontFoot() { return &m_FrontFoot; }
	CAnimKeyframe *GetAttach() { return &m_Attach; }
	void Set(CAnimation *pAnim, float Time);
	void Add(CAnimation *pAdded, float Time, float Amount);

	static CAnimState *GetIdle();
};

#endif
