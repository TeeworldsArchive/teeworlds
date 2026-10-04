/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_TEES_H
#define GAME_CLIENT_COMPONENTS_TEES_H
#include <game/client/component.h>

class CTees : public CComponent
{
	void RenderTee(
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pCurChar,
		const CNetObj_TeeInfo *pTeeInfo,
		const CTeeRenderInfo *pRenderInfo,
		int ClientID) const;
	void RenderHook(
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pCurChar,
		const CTeeRenderInfo *pRenderInfo,
		int ClientID) const;

public:
	virtual void OnRender() override;
};

#endif
