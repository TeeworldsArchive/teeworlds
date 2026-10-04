/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_BINDS_H
#define GAME_CLIENT_COMPONENTS_BINDS_H
#include <engine/keys.h>
#include <game/client/component.h>

class CBinds : public CComponent
{
	int DecodeBindString(const char *pKeyName, int *pModifier);

	static void ConBind(IConsole::IResult *pResult, void *pUserData);
	static void ConUnbind(IConsole::IResult *pResult, void *pUserData);
	static void ConUnbindAll(IConsole::IResult *pResult, void *pUserData);
	static void ConBinds(IConsole::IResult *pResult, void *pUserData);
	class IConsole *GetConsole() const { return Console(); }

	static void ConfigSaveCallback(class IConfigManager *pConfigManager, void *pUserData);

public:
	CBinds();

	class CBindsSpecial : public CComponent
	{
	public:
		CBinds *m_pBinds;
		virtual bool OnInput(IInput::CEvent Event) override;
	};

	enum
	{
		BIND_LENGTH = 128,

		MODIFIER_NONE = 0,
		MODIFIER_SHIFT,
		MODIFIER_CTRL,
		MODIFIER_ALT,
		MODIFIER_COUNT
	};

	CBindsSpecial m_SpecialBinds;

	void Bind(int KeyID, int Modifier, const char *pStr);
	void SetDefaults();
	void UnbindAll();
	const char *Get(int KeyID, int Modifier);
	void GetKeyID(const char *pBindStr, int &KeyID, int &Modifier);
	void GetKey(char aKey[64], unsigned BufSize, int KeyID, int Modifier);
	void GetKey(const char *pBindStr, char aKey[64], unsigned BufSize);
	static const char *GetModifierName(int m);
	static int GetModifierMask(IInput *i);
	static int GetModifierMaskOfKey(int Key);
	static bool ModifierMatchesKey(int Modifier, int Key);

	virtual void OnConsoleInit() override;
	virtual bool OnInput(IInput::CEvent Event) override;

private:
	char m_aaaKeyBindings[KEY_LAST][MODIFIER_COUNT][BIND_LENGTH];
	static const int s_aaDefaultBindKeys[][2];
	static const char s_aaDefaultBindValues[][32];
};
#endif
