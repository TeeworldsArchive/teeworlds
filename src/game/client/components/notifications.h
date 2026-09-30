/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_NOTIFICATIONS_H
#define GAME_CLIENT_COMPONENTS_NOTIFICATIONS_H
#include <game/client/component.h>

class CNotifications : public CComponent
{
	float m_SoundToggleTime;

	void OnConsoleInit();
	void RenderSoundNotification();

	static void Con_SndToggle(IConsole::IResult *pResult, void *pUserData);

public:
	CNotifications();
	virtual void OnRender();
};

#endif
