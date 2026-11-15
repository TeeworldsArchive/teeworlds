/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <base/system/mem.h>
#include <base/system/thread.h>
#include <base/tl/threading.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/textrender.h>

#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <game/client/ui.h>
#include <game/version.h>

#include <generated/client_data.h>

#include "menus.h"
#include "steamfriends.h"

void CMenus::RenderStartMenu(CUIRect MainView)
{
	// render logo
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	IGraphics::CQuadItem QuadItem(160 + 40, 60 * Graphics()->ScreenUIScale(), 280, 70);
	Graphics()->SingleQuadDrawTL(&QuadItem);
	Graphics()->QuadsEnd();

	const float Rounding = 10.0f;

	CUIRect TopMenu, BottomMenu;
	MainView.HSplitTop(60 * Graphics()->ScreenUIScale(), 0, &MainView);
	MainView.VSplitLeft(150.0f, 0, &MainView);
	MainView.VSplitLeft(380.0f, &TopMenu, &MainView);
	TopMenu.HSplitTop(365.0f, &TopMenu, &BottomMenu);
	// TopMenu.HSplitBottom(145.0f, &TopMenu, 0);
	RenderBackgroundShadow(&TopMenu, false, Rounding);

	CUIRect Button;
	int NewPage = -1;

	TopMenu.HSplitBottom(40.0f, &TopMenu, &Button);
	static CButtonContainer s_SettingsButton;
	if(DoButton_Menu(&s_SettingsButton, Localize("Settings"), 0, &Button, Config()->m_ClShowStartMenuImages ? "settings" : 0, CUIRect::CORNER_ALL, Rounding, 0.5f) || CheckHotKey(KEY_S))
		NewPage = PAGE_SETTINGS;

	TopMenu.HSplitBottom(5.0f, &TopMenu, 0); // little space
	TopMenu.HSplitBottom(40.0f, &TopMenu, &Button);
	static CButtonContainer s_DemoButton;
	if(DoButton_Menu(&s_DemoButton, Localize("Demos"), 0, &Button, Config()->m_ClShowStartMenuImages ? "demos" : 0, CUIRect::CORNER_ALL, Rounding, 0.5f) || CheckHotKey(KEY_D))
	{
		NewPage = PAGE_DEMOS;
		DemolistPopulate();
		DemolistOnUpdate(false);
	}

	TopMenu.HSplitBottom(5.0f, &TopMenu, 0); // little space
	TopMenu.HSplitBottom(40.0f, &TopMenu, &Button);

	CUIRect TutorialButton, LocalServerButton;
	Button.VSplitLeft(100.0f, &TutorialButton, &LocalServerButton);
	static CButtonContainer s_TutorialButton;
	if(DoButton_Menu(&s_TutorialButton, Localize("Tutorial"), 0, &TutorialButton, 0, CUIRect::CORNER_ALL, Rounding, 0.5f))
	{
		if(Client()->IsLocalServerRunning())
			Client()->CloseLocalServer();
		char aLanguage[192];
		str_format(aLanguage, sizeof(aLanguage), "sv_languagefile \"%s\"", Config()->m_ClLanguagefile);
		Client()->OpenLocalServer(4, "sv_gametype \"tutorial\"", "sv_map \"tutorial\"", aLanguage, "sv_port 16606");
		Client()->Connect("127.0.0.1:16606");
	}
	static CButtonContainer s_LocalServerButton;
	if(DoButton_Menu(&s_LocalServerButton, Client()->IsLocalServerRunning() ? Localize("Shutdown server") : Localize("Local server"), 0, &LocalServerButton, Config()->m_ClShowStartMenuImages ? "local_server" : 0, CUIRect::CORNER_ALL, Rounding, 0.5f))
	{
		if(Client()->IsLocalServerRunning())
			Client()->CloseLocalServer();
		else
			Client()->OpenLocalServer(0);
	}

	TopMenu.HSplitBottom(5.0f, &TopMenu, 0); // little space
	TopMenu.HSplitBottom(40.0f, &TopMenu, &Button);
	static CButtonContainer s_PlayButton;
	if(DoButton_Menu(&s_PlayButton, Localize("Play"), 0, &Button, Config()->m_ClShowStartMenuImages ? "play_game" : 0, CUIRect::CORNER_ALL, Rounding, 0.5f) || UI()->ConsumeHotkey(CUI::HOTKEY_ENTER) || CheckHotKey(KEY_P))
		NewPage = Config()->m_UiBrowserPage;

	BottomMenu.HSplitTop(90.0f, 0, &BottomMenu);
	RenderBackgroundShadow(&BottomMenu, true, Rounding);

	BottomMenu.HSplitTop(40.0f, &Button, &TopMenu);
	static CButtonContainer s_QuitButton;
	if(DoButton_Menu(&s_QuitButton, Localize("Quit"), 0, &Button, 0, CUIRect::CORNER_ALL, Rounding, 0.5f) || UI()->ConsumeHotkey(CUI::HOTKEY_ESCAPE) || CheckHotKey(KEY_Q))
		m_Popup = POPUP_QUIT;

	CUIRect ExtButtons;
	MainView.VSplitLeft(30.0f, 0, &ExtButtons);
	ExtButtons.HSplitBottom(30.0f, &ExtButtons, 0);
	ExtButtons.VSplitLeft(80.0f, &ExtButtons, 0);
	ExtButtons.HSplitBottom(20.0f, &ExtButtons, &Button);

	// the screenshots board can be switched to the Steam friends list; the tab
	// row sits above the board, inside the same panel
	const bool SteamFriendsAvailable = m_pClient->SteamFriends() && m_pClient->SteamFriends()->Available();
	if(!SteamFriendsAvailable)
		m_StartMenuBoard = STARTMENU_BOARD_SCREENSHOTS;

	CUIRect Board;
	MainView.VMargin(50.0f, &Board);
	Board.HSplitBottom(60 * Graphics()->ScreenUIScale(), &Board, 0);
	Board.Draw(vec4(0.0f, 0.0f, 0.0f, 0.25f));

	Board.VMargin(20.0f, &Board);
	Board.HSplitTop(20.0f, 0, &Board);
	Board.HSplitBottom(20.0f, &Board, 0);

	CUIRect BoardTabs, BoardContent, BoardButton;
	Board.HSplitBottom(20.0f, &Board, &BoardButton);
	Board.HSplitTop(20.0f, &BoardTabs, &BoardContent);
	BoardContent.HSplitTop(4.0f, 0, &BoardContent);

	{
		CUIRect Tab;
		const float TabSpacing = 3.0f;
		const int NumTabs = SteamFriendsAvailable ? 2 : 1;
		// the panel gets narrow at 4:3, so the tabs have to share what is there
		const float TabWidth = minimum(100.0f, (BoardTabs.w - TabSpacing * (NumTabs - 1)) / NumTabs);

		BoardTabs.VSplitLeft(TabWidth, &Tab, &BoardTabs);
		static CButtonContainer s_ScreenshotsTab;
		if(DoButton_MenuTabTop(&s_ScreenshotsTab, Localize("Screenshots"), m_StartMenuBoard == STARTMENU_BOARD_SCREENSHOTS, &Tab))
			m_StartMenuBoard = STARTMENU_BOARD_SCREENSHOTS;

		if(SteamFriendsAvailable)
		{
			BoardTabs.VSplitLeft(TabSpacing, 0, &BoardTabs);
			BoardTabs.VSplitLeft(TabWidth, &Tab, &BoardTabs);
			static CButtonContainer s_SteamFriendsTab;
			if(DoButton_MenuTabTop(&s_SteamFriendsTab, Localize("Steam friends", "Steam Friends"), m_StartMenuBoard == STARTMENU_BOARD_STEAM_FRIENDS, &Tab))
				m_StartMenuBoard = STARTMENU_BOARD_STEAM_FRIENDS;
		}
	}

	if(m_StartMenuBoard == STARTMENU_BOARD_STEAM_FRIENDS)
		RenderStartMenuSteamFriends(BoardContent, BoardButton);
	else
		RenderStartMenuScreenshots(BoardContent, BoardButton);

	// render version
	CUIRect Version;
	MainView.HSplitBottom(50.0f, 0, &Version);
	Version.VMargin(50.0f, &Version);
	UI()->DoLabel(&Version, "Archive " GAME_VERSION, 14.0f, TEXTALIGN_TR);

	if(str_comp(Client()->LatestVersion(), GAME_VERSION) > 0)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Teeworlds Archive %s is out!"), Client()->LatestVersion());
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->TextSecondaryColor(0.0f, 0.0f, 0.0f, 0.5f);
		UI()->DoLabel(&TopMenu, aBuf, 14.0f, TEXTALIGN_MC, TopMenu.w * 0.9f, true);
		TextRender()->TextColor(CUI::ms_DefaultTextColor);
		TextRender()->TextSecondaryColor(CUI::ms_DefaultTextOutlineColor);
	}

	if(NewPage != -1)
		SetMenuPage(NewPage);
}

void CMenus::RenderStartMenuScreenshots(CUIRect View, CUIRect Button)
{
	Button.VSplitRight(80.0f, 0, &Button);

	static bool s_InitScreenshotsList = true;
	// refresh
	static CButtonContainer s_RefreshButton;
	if(DoButton_Menu(&s_RefreshButton, Localize("Refresh"), 0, &Button))
	{
		StartScreenshotScan();
		s_InitScreenshotsList = true;
	}

	if(!m_ScreenshotsScanned)
		StartScreenshotScan();

	// every entry gets its own spinner until the background scan delivers its
	// thumbnail, so the list is usable while the images are decoded
	if(m_lScreenshots.size())
	{
		static sorted_array<const CScreenshot *> s_lpList;
		static CListBox s_ListBox;
		static int OldSelected;
		if(s_InitScreenshotsList)
		{
			s_lpList.clear();
			for(int i = 0; i < m_lScreenshots.size(); ++i)
			{
				const CScreenshot *pScreenshot = &m_lScreenshots[i];
				if(s_ListBox.FilterMatches(pScreenshot->m_Name))
				{
					s_lpList.add(pScreenshot);
				}
			}
			s_InitScreenshotsList = false;
			OldSelected = -1;
		}

		s_ListBox.DoBegin(&View);
		s_InitScreenshotsList = s_ListBox.DoFilter();
		s_ListBox.DoStart(60.0f, s_lpList.size(), 3, 1, OldSelected);

		for(int i = 0; i < s_lpList.size(); ++i)
		{
			const CScreenshot *s = s_lpList[i];
			if(s == 0)
				continue;

			CListboxItem Item = s_ListBox.DoNextItem(&s_lpList[i], OldSelected == i);
			if(Item.m_Visible)
			{
				Item.m_Rect.HSplitTop(5.0f, 0, &Item.m_Rect); // some margin from the top

				if(s->m_Texture.IsValid())
				{
					float FinalHeight = 45.0f;
					float FinalWidth = s->m_Width / (s->m_Height / FinalHeight);
					const float MaxWidth = Item.m_Rect.w - 10.0f;
					if(MaxWidth > 0.0f && FinalWidth > MaxWidth)
					{
						FinalWidth = MaxWidth;
						FinalHeight = FinalWidth * s->m_Height / s->m_Width;
					}
					Graphics()->TextureSet(s->m_Texture);
					Graphics()->WrapClamp();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
					IGraphics::CQuadItem QuadItem(Item.m_Rect.Center().x - FinalWidth / 2.0f, Item.m_Rect.Center().y - FinalHeight / 2.0f - 10.0f, FinalWidth, FinalHeight);
					Graphics()->SingleQuadDrawTL(&QuadItem);
					Graphics()->QuadsEnd();
					Graphics()->WrapNormal();
				}
				else
				{
					// the thumbnail is still being decoded on the job thread
					CUIRect Spinner;
					Spinner.w = 40.0f;
					Spinner.h = 40.0f;
					Spinner.x = Item.m_Rect.Center().x - Spinner.w / 2.0f;
					Spinner.y = Item.m_Rect.Center().y - 10.0f - Spinner.h / 2.0f;
					UI()->RenderLoadingSpinner(&Spinner, s->m_LoadStart);
				}

				CUIRect Label;
				Item.m_Rect.Margin(5.0f, &Item.m_Rect);
				Item.m_Rect.HSplitBottom(10.0f, &Item.m_Rect, &Label);

				UI()->DoLabelSelected(&Label, s->m_Name, Item.m_Selected, 10.0f, TEXTALIGN_CENTER);
			}
		}
		const int NewSelected = s_ListBox.DoEnd();
		if(NewSelected == OldSelected && s_ListBox.WasItemActivated())
		{
			if(LoadScreenshot(s_lpList[OldSelected]->m_Name))
			{
				m_Popup = POPUP_SCREENSHOT;
			}
			else
			{
				UI()->DoToast(Localize("Couldn't load screenshot file"), CUI::TOAST_WARNING);
			}
		}
		OldSelected = NewSelected;
	}
	else
	{
		UI()->DoLabel(&View, Localize("No screenshots found"), 20.0f, TEXTALIGN_MC);
	}
}

bool CMenus::LoadScreenshot(const char *pName)
{
	if(m_Screenshot.m_Texture.IsValid())
		Graphics()->UnloadTexture(&m_Screenshot.m_Texture);
	CImageInfo Image;
	// load image
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "screenshots/%s", pName);
	if(!Graphics()->LoadPNG(&Image, aBuf, IStorage::TYPE_SAVE))
	{
		str_format(aBuf, sizeof(aBuf), "failed to load screenshot '%s'", pName);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "game", aBuf);
		return false;
	}
	m_Screenshot.m_Name = aBuf;
	m_Screenshot.m_Width = Image.m_Width;
	m_Screenshot.m_Height = Image.m_Height;
	m_Screenshot.m_Texture = Graphics()->LoadTextureRaw(Image.m_Width, Image.m_Height, 1, Image.m_Format, Image.m_pData, Image.m_Format, IGraphics::TEXLOAD_NORESAMPLE | IGraphics::TEXLOAD_NOMIPMAPS);
	return true;
}

/*
	Collects the screenshot files and decodes their previews on a job thread.
	The names are listed before the job starts and are not touched while it
	runs; finished thumbnails are published under a lock and uploaded by the
	menu in PollScreenshotScan(), so the client never blocks on a large
	screenshot library.
*/
class CScreenshotLoader
{
public:
	struct CResult
	{
		int m_Index;
		int m_Width;
		int m_Height;
		int m_Format;
		void *m_pData;
	};

	class CJob m_Job;
	lock m_Lock;
	array<string> m_lNames;
	array<CResult> m_lResults; // guarded by m_Lock
	class IGraphics *m_pGraphics;
	volatile bool m_Abort;

	CScreenshotLoader() :
		m_pGraphics(0),
		m_Abort(false)
	{
	}

	~CScreenshotLoader()
	{
		// thumbnails that were not uploaded yet have to be released here
		for(int i = 0; i < m_lResults.size(); i++)
			mem_free(m_lResults[i].m_pData);
		m_lResults.clear();
	}
};

static int ScreenshotLoadThread(void *pUser)
{
	CScreenshotLoader *pLoader = static_cast<CScreenshotLoader *>(pUser);

	for(int i = 0; i < pLoader->m_lNames.size() && !pLoader->m_Abort; i++)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "screenshots/%s", pLoader->m_lNames[i].cstr());

		CImageInfo Image;
		if(!pLoader->m_pGraphics->LoadPNG(&Image, aPath, IStorage::TYPE_SAVE))
			continue;

		// the list only draws a small preview, so scale the decoded screenshot
		// down here and leave only the thumbnail to the menu
		const int ThumbWidth = maximum(1, Image.m_Width / 4);
		const int ThumbHeight = maximum(1, Image.m_Height / 4);
		void *pThumbData = RescaleImage(Image.m_Width, Image.m_Height, ThumbWidth, ThumbHeight, Image.GetPixelSize(), static_cast<const unsigned char *>(Image.m_pData));
		if(pThumbData != Image.m_pData)
			mem_free(Image.m_pData);

		CScreenshotLoader::CResult Result;
		Result.m_Index = i;
		Result.m_Width = ThumbWidth;
		Result.m_Height = ThumbHeight;
		Result.m_Format = Image.m_Format;
		Result.m_pData = pThumbData;

		scope_lock Guard(&pLoader->m_Lock);
		pLoader->m_lResults.add(Result);
	}

	return 0;
}

void CMenus::StartScreenshotScan()
{
	StopScreenshotScan();
	ClearScreenshots();

	// only the names are collected here, the decoding runs on the job thread
	Storage()->ListDirectory(IStorage::TYPE_SAVE, "screenshots", ScreenshotScan, this);

	m_pScreenshotLoader = new CScreenshotLoader();
	m_pScreenshotLoader->m_pGraphics = Graphics();
	for(int i = 0; i < m_lScreenshots.size(); i++)
		m_pScreenshotLoader->m_lNames.add(m_lScreenshots[i].m_Name);

	m_ScreenshotsScanned = true;
	m_pClient->Engine()->AddJob(&m_pScreenshotLoader->m_Job, ScreenshotLoadThread, m_pScreenshotLoader);
}

void CMenus::StopScreenshotScan()
{
	if(!m_pScreenshotLoader)
		return;

	m_pScreenshotLoader->m_Abort = true;
	// the job may still be decoding one image; wait before its data goes away
	while(m_pScreenshotLoader->m_Job.Status() != CJob::STATE_DONE)
		thread_sleep(1);

	delete m_pScreenshotLoader;
	m_pScreenshotLoader = 0;
}

void CMenus::ClearScreenshots()
{
	for(int i = 0; i < m_lScreenshots.size(); i++)
	{
		if(m_lScreenshots[i].m_Texture.IsValid())
			Graphics()->UnloadTexture(&m_lScreenshots[i].m_Texture);
	}
	m_lScreenshots.clear();
}

void CMenus::PollScreenshotScan()
{
	if(!m_pScreenshotLoader)
		return;

	array<CScreenshotLoader::CResult> vResults;
	bool Done;
	{
		scope_lock Guard(&m_pScreenshotLoader->m_Lock);
		for(int i = 0; i < m_pScreenshotLoader->m_lResults.size(); i++)
			vResults.add(m_pScreenshotLoader->m_lResults[i]);
		m_pScreenshotLoader->m_lResults.clear();
		// the job adds its last thumbnail before the pool marks it done, so
		// nothing can be missed once the state is DONE
		Done = m_pScreenshotLoader->m_Job.Status() == CJob::STATE_DONE;
	}

	for(int i = 0; i < vResults.size(); i++)
	{
		CScreenshotLoader::CResult &Result = vResults[i];
		if(Result.m_Index < 0 || Result.m_Index >= m_lScreenshots.size())
		{
			mem_free(Result.m_pData);
			continue;
		}

		CScreenshot &Screenshot = m_lScreenshots[Result.m_Index];
		Screenshot.m_Width = Result.m_Width;
		Screenshot.m_Height = Result.m_Height;
		Screenshot.m_Texture = Graphics()->LoadTextureRaw(Result.m_Width, Result.m_Height, 1, Result.m_Format, Result.m_pData, Result.m_Format, IGraphics::TEXLOAD_NORESAMPLE);
		mem_free(Result.m_pData);
	}

	if(Done)
	{
		delete m_pScreenshotLoader;
		m_pScreenshotLoader = 0;
	}
}

int CMenus::ScreenshotScan(const char *pName, int IsDir, [[maybe_unused]] int DirType, void *pUser)
{
	CMenus *pSelf = (CMenus *) pUser;
	// thumbnails are scaled in memory now, but older versions wrote them next
	// to the screenshots; ignore those files instead of listing them twice
	if(IsDir || !str_endswith(pName, ".png") || str_endswith(pName, "_thumbnail.png"))
		return 0;

	for(int i = 0; i < pSelf->m_lScreenshots.size(); i++)
	{
		if(str_comp(pSelf->m_lScreenshots[i].m_Name, pName) == 0)
			return 0;
	}

	CScreenshot &Screenshot = pSelf->m_lScreenshots.emplace();
	Screenshot.m_Name = pName;
	Screenshot.m_Width = 0;
	Screenshot.m_Height = 0;
	Screenshot.m_Texture = IGraphics::CTextureHandle();
	Screenshot.m_LoadStart = time_get();
	return 0;
}
