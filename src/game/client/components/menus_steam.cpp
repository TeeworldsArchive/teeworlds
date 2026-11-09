/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <base/math.h>
#include <base/system/string.h>

#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/shared/config.h>

#include <game/client/ui.h>

#include "menus.h"
#include "steamfriends.h"

static void FormatFriendStatus(char *pBuf, int BufSize, const CSteamFriendInfo *pInfo)
{
	if(pInfo->m_PlayingThisGame)
	{
		if(pInfo->m_aGameType[0] && pInfo->m_aMap[0])
			str_format(pBuf, BufSize, Localize("Playing '%s' on '%s'", "Playing '(gametype)' on '(map)'"), pInfo->m_aGameType, pInfo->m_aMap);
		else if(pInfo->m_aMap[0])
			str_format(pBuf, BufSize, Localize("Playing on '%s'", "Playing on '(map)'"), pInfo->m_aMap);
		else
			str_copy(pBuf, Localize("In game", "Steam Friends"), BufSize);
		return;
	}

	if(pInfo->m_PlayingOtherGame)
	{
		str_copy(pBuf, Localize("In another game", "Steam Friends"), BufSize);
		return;
	}

	switch(pInfo->m_State)
	{
		case CSteamFriendInfo::STATE_ONLINE: str_copy(pBuf, Localize("Online", "Steam Friends"), BufSize); break;
		case CSteamFriendInfo::STATE_BUSY: str_copy(pBuf, Localize("Busy", "Steam Friends"), BufSize); break;
		case CSteamFriendInfo::STATE_AWAY: str_copy(pBuf, Localize("Away", "Steam Friends"), BufSize); break;
		case CSteamFriendInfo::STATE_SNOOZE: str_copy(pBuf, Localize("Snoozed", "Steam Friends"), BufSize); break;
		case CSteamFriendInfo::STATE_LOOKING_TO_TRADE: str_copy(pBuf, Localize("Looking to trade", "Steam Friends"), BufSize); break;
		case CSteamFriendInfo::STATE_LOOKING_TO_PLAY: str_copy(pBuf, Localize("Looking to play", "Steam Friends"), BufSize); break;
		default: str_copy(pBuf, Localize("Offline", "Steam Friends"), BufSize); break;
	}
}

static void RenderSteamFriendAvatar(CGameClient *pClient, CSteamFriends *pSteamFriends, const CSteamFriendInfo *pInfo, CUIRect *pRect)
{
	CGifAnimation *pAvatar = pSteamFriends->Avatar(pInfo);
	if(!pAvatar || !pAvatar->IsValid())
	{
		pRect->Draw(vec4(1.0f, 1.0f, 1.0f, 0.05f), 4.0f);
		return;
	}

	// animated avatars keep playing while they are on screen
	pAvatar->Update(pClient->Graphics(), pClient->Client()->RenderFrameTime());

	const float Scale = minimum(pRect->w / (float) pAvatar->Width(), pRect->h / (float) pAvatar->Height());
	const vec2 Center = pRect->Center();

	// the backends upload RGBA textures premultiplied, so fading the offline
	// friends has to go through the global alpha to scale the color channels
	// as well
	IGraphics *pGraphics = pClient->Graphics();
	const float PreviousAlpha = pGraphics->GetGlobalAlpha();
	pGraphics->SetGlobalAlpha(PreviousAlpha * (pInfo->IsOnline() ? 1.0f : 0.5f));
	pGraphics->TextureSet(pAvatar->Texture());
	pGraphics->WrapClamp();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem QuadItem(Center.x - pAvatar->Width() * Scale / 2.0f, Center.y - pAvatar->Height() * Scale / 2.0f,
		pAvatar->Width() * Scale, pAvatar->Height() * Scale);
	pGraphics->SingleQuadDrawTL(&QuadItem);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
	pGraphics->SetGlobalAlpha(PreviousAlpha);
}

void CMenus::RenderStartMenuSteamFriends(CUIRect View, CUIRect Button)
{
	View.Draw(vec4(0.0f, 0.0f, 0.0f, Config()->m_ClMenuAlpha / 100.0f), 5.0f, CUIRect::CORNER_ALL);

	CSteamFriends *pSteamFriends = m_pClient->SteamFriends();
	if(!pSteamFriends)
		return;
	pSteamFriends->Refresh();

	const bool ShowOffline = Config()->m_ClShowOfflineSteamFriends != 0;

	// hiding the offline friends is the only option of this board, so its
	// checkbox sits in the button row of the panel
	CUIRect Offline;
	Button.VSplitLeft(minimum(150.0f, Button.w), &Offline, 0);
	if(DoButton_CheckBox(&Config()->m_ClShowOfflineSteamFriends, Localize("Show offline friends", "Steam Friends"), ShowOffline, &Offline))
		Config()->m_ClShowOfflineSteamFriends ^= 1;

	// the list is rebuilt while it is drawn, so count the friends first
	int NumFriends = 0;
	for(int i = 0; i < pSteamFriends->NumFriends(); i++)
	{
		const CSteamFriendInfo *pInfo = pSteamFriends->GetFriend(i);
		if(pInfo && (ShowOffline || pInfo->IsOnline()))
			NumFriends++;
	}

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 44.0f;
	ScrollParams.m_ClipBgColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	ScrollParams.m_ScrollbarBgColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	ScrollParams.m_ScrollbarWidth = 14;
	ScrollParams.m_ScrollbarMargin = 5;

	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	s_ScrollRegion.Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	if(NumFriends == 0)
	{
		UI()->DoLabel(&View, ShowOffline ? Localize("No Steam friends", "Steam Friends") : Localize("No online Steam friends", "Steam Friends"), 14.0f, TEXTALIGN_MC);
		s_ScrollRegion.End();
		return;
	}

	// a handful of rows fit on screen, so the button ids of a sliding window
	// are enough to keep the interaction state stable
	static CButtonContainer s_aFriendButtons[16];

	const float RowHeight = 44.0f;
	int RowIndex = 0;
	for(int i = 0; i < pSteamFriends->NumFriends(); i++)
	{
		const CSteamFriendInfo *pInfo = pSteamFriends->GetFriend(i);
		if(!pInfo || (!ShowOffline && !pInfo->IsOnline()))
			continue;

		CUIRect Row, Spacing;
		View.HSplitTop(RowHeight, &Row, &View);
		View.HSplitTop(2.0f, &Spacing, &View);
		s_ScrollRegion.AddRect(Row);
		s_ScrollRegion.AddRect(Spacing);
		if(s_ScrollRegion.IsRectClipped(Row))
			continue;

		const void *pID = &s_aFriendButtons[RowIndex % 16];
		RowIndex++;

		const bool Hovered = UI()->MouseHovered(&Row);
		const bool Clicked = UI()->DoButtonLogic(pID, &Row);
		if(pInfo->m_PlayingThisGame && pInfo->m_aServerAddress[0])
		{
			UI()->DoTooltip(pID, &Row, Localize("Double click to join your friend."));
			if(Clicked && Input()->MouseDoubleClick())
				Client()->Connect(pInfo->m_aServerAddress);
		}

		Row.Draw(vec4(1.0f, 1.0f, 1.0f, Hovered ? 0.12f : 0.05f), 5.0f);

		// avatar, keeping its aspect ratio inside the square of the row
		CUIRect Layout = Row;
		Layout.HMargin(3.0f, &Layout);
		CUIRect Avatar;
		Layout.VSplitLeft(Layout.h, &Avatar, &Layout);
		Avatar.VMargin(2.0f, &Avatar);

		RenderSteamFriendAvatar(m_pClient, pSteamFriends, pInfo, &Avatar);

		// name and where the friend is playing
		Layout.VMargin(6.0f, &Layout);
		Layout.VSplitLeft(2.0f, 0, &Layout);

		CUIRect Name, Status;
		Layout.HSplitTop(Layout.h * 0.5f, &Name, &Status);

		UI()->DoLabelColor(&Name, vec4(1.0f, 1.0f, 1.0f, pInfo->IsOnline() ? 1.0f : 0.5f), pInfo->m_aName, 12.0f, TEXTALIGN_ML);

		char aStatus[256];
		FormatFriendStatus(aStatus, sizeof(aStatus), pInfo);

		vec4 StatusColor(1.0f, 1.0f, 1.0f, 0.55f);
		if(pInfo->m_PlayingThisGame)
			StatusColor = vec4(0.6f, 1.0f, 0.6f, 0.8f);
		else if(pInfo->m_State == CSteamFriendInfo::STATE_BUSY || pInfo->m_State == CSteamFriendInfo::STATE_AWAY || pInfo->m_State == CSteamFriendInfo::STATE_SNOOZE)
			StatusColor = vec4(1.0f, 0.8f, 0.5f, 0.7f);
		UI()->DoLabelColor(&Status, StatusColor, aStatus, 9.0f, TEXTALIGN_ML);
	}

	s_ScrollRegion.End();
}