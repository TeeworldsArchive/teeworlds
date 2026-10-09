/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_MOTD_H
#define GAME_CLIENT_COMPONENTS_MOTD_H
#include <base/system/time.h>
#include <game/client/component.h>

class CMotd : public CComponent
{
	// motd
	int64 m_ServerMotdTime;
	char m_aServerMotd[1024];
	CTextCursor m_ServerMotdCursor;

public:
	void Clear();
	bool IsActive();
	const char *GetMotd() const { return m_aServerMotd; }

	virtual void OnRender() override;
	virtual void OnStateChange(int NewState, int OldState) override;
	virtual void OnMessage(int MsgType, void *pRawMsg) override;
	virtual bool OnInput(IInput::CEvent Event) override;
};

#endif
