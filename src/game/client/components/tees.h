/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
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
	virtual void OnRender();
};

#endif
