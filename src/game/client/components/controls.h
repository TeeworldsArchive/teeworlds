/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_CONTROLS_H
#include <base/vmath.h>
#include <game/client/component.h>

class CControls : public CComponent
{
public:
	vec2 m_MousePos;
	vec2 m_TargetPos;

	CNetObj_PlayerInput m_InputData;
	CNetObj_PlayerInput m_LastData;
	int m_InputDirectionLeft;
	int m_InputDirectionRight;

	CControls();

	virtual void OnReset() override;
	virtual void OnRelease() override;
	virtual void OnRender() override;
	virtual void OnMessage(int MsgType, void *pRawMsg) override;
	virtual bool OnCursorMove(float x, float y, int CursorType) override;
	virtual void OnConsoleInit() override;
	virtual void OnPlayerDeath();

	int SnapInput(int *pData);
	void ClampMousePos();
	float GetMaxMouseDistance() const;
};
#endif
