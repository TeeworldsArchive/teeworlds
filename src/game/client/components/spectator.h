/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_SPECTATOR_H
#define GAME_CLIENT_COMPONENTS_SPECTATOR_H
#include <base/vmath.h>

#include <game/client/component.h>

class CSpectator : public CComponent
{
	enum
	{
		NO_SELECTION = -1,
	};

	bool m_Active;
	bool m_WasActive;

	int m_SelectedSpectatorID;
	int m_SelectedSpecMode;
	vec2 m_SelectorMouse;

	bool CanSpectate();
	bool SpecModePossible(int SpecMode, int SpectatorID);
	void HandleSpectateNextPrev(int Direction);

	static void ConKeySpectator(IConsole::IResult *pResult, void *pUserData);
	static void ConSpectate(IConsole::IResult *pResult, void *pUserData);
	static void ConSpectateNext(IConsole::IResult *pResult, void *pUserData);
	static void ConSpectatePrevious(IConsole::IResult *pResult, void *pUserData);

public:
	CSpectator();

	virtual void OnConsoleInit() override;
	virtual bool OnCursorMove(float x, float y, int CursorType) override;
	virtual void OnRender() override;
	virtual void OnRelease() override;
	virtual void OnReset() override;

	void SendSpectate(int SpecMode, int SpectatorID);
};

#endif
