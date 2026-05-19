/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/textrender.h>

#include <engine/shared/config.h>

#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/version.h>

#include <generated/client_data.h>

#include "menus.h"

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

	static bool EditorHotkeyWasPressed = true;
	static float EditorHotKeyChecktime = 0;
	TopMenu.HSplitBottom(5.0f, &TopMenu, 0); // little space
	TopMenu.HSplitBottom(40.0f, &TopMenu, &Button);
	static CButtonContainer s_MapEditorButton;
	if(DoButton_Menu(&s_MapEditorButton, Localize("Editor"), 0, &Button, Config()->m_ClShowStartMenuImages ? "editor" : 0, CUIRect::CORNER_ALL, Rounding, 0.5f) || (!EditorHotkeyWasPressed && Client()->LocalTime() - EditorHotKeyChecktime < 0.1f && CheckHotKey(KEY_E)))
	{
		Config()->m_ClEditor = 1;
		Input()->MouseModeRelative();
		EditorHotkeyWasPressed = true;
	}
	if(!Input()->KeyIsPressed(KEY_E))
	{
		EditorHotkeyWasPressed = false;
		EditorHotKeyChecktime = Client()->LocalTime();
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

	// screenshots board
	CUIRect Screenshotsboard;
	MainView.VMargin(50.0f, &Screenshotsboard);
	Screenshotsboard.HSplitBottom(60 * Graphics()->ScreenUIScale(), &Screenshotsboard, 0);
	Screenshotsboard.Draw(vec4(0.0f, 0.0f, 0.0f, 0.25f));

	Screenshotsboard.VMargin(20.0f, &Screenshotsboard);
	Screenshotsboard.HSplitTop(20.0f, 0, &Screenshotsboard);
	Screenshotsboard.HSplitBottom(20.0f, &Screenshotsboard, 0);
	Screenshotsboard.HSplitBottom(20.0f, &Screenshotsboard, &Button);
	Button.VSplitRight(80.0f, 0, &Button);

	static bool s_InitScreenshotsList = true;
	// refresh
	static CButtonContainer s_RefreshButton;
	if(DoButton_Menu(&s_RefreshButton, Localize("Refresh"), 0, &Button))
	{
		Storage()->ListDirectory(IStorage::TYPE_SAVE, "screenshots", ScreenshotScan, this);
		s_InitScreenshotsList = true;
	}

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

		s_ListBox.DoBegin(&Screenshotsboard);
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

				const float FinalHeight = 45.0f;
				const float FinalWidth = s->m_Width / (s->m_Height / FinalHeight);
				{
					Graphics()->TextureSet(s->m_Texture);
					Graphics()->WrapClamp();
					Graphics()->QuadsBegin();
					IGraphics::CQuadItem QuadItem(Item.m_Rect.Center().x - FinalWidth / 2.0f, Item.m_Rect.Center().y - FinalHeight / 2.0f - 10.0f, FinalWidth, FinalHeight);
					Graphics()->SingleQuadDrawTL(&QuadItem);
					Graphics()->QuadsEnd();
					Graphics()->WrapNormal();
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
		UI()->DoLabel(&Screenshotsboard, Localize("There's no any thumbnail for screenshots"), 20.0f, TEXTALIGN_MC);
	}
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

int CMenus::ScreenshotScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	CMenus *pSelf = (CMenus *) pUser;
	const char *pSuffix = str_endswith(pName, "_thumbnail.png");
	if(IsDir || !pSuffix)
	{
		return 0;
	}
	char aFilename[IO_MAX_PATH_LENGTH];
	str_truncate(aFilename, sizeof(aFilename), pName, pSuffix - pName);
	str_append(aFilename, ".png", sizeof(aFilename));

	for(int i = 0; i < pSelf->m_lScreenshots.size(); i++)
	{
		if(str_comp(pSelf->m_lScreenshots[i].m_Name, aFilename) == 0)
		{
			return 0;
		}
	}
	CImageInfo Image;
	// load thumbnail
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "screenshots/%s", pName);
	if(!pSelf->Graphics()->LoadPNG(&Image, aBuf, IStorage::TYPE_SAVE))
	{
		str_format(aBuf, sizeof(aBuf), "failed to load thumbnail '%s'", pName);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "game", aBuf);
		return 0;
	}

	CScreenshot &Screenshot = pSelf->m_lScreenshots.emplace();
	Screenshot.m_Texture = pSelf->Graphics()->LoadTextureRaw(Image.m_Width, Image.m_Height, 1, Image.m_Format, Image.m_pData, Image.m_Format, IGraphics::TEXLOAD_NORESAMPLE);
	Screenshot.m_Width = Image.m_Width;
	Screenshot.m_Height = Image.m_Height;
	Screenshot.m_Name = aFilename;
	mem_free(Image.m_pData);
	return 0;
}
