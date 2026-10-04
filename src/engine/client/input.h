/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_CLIENT_INPUT_H
#define ENGINE_CLIENT_INPUT_H

#include <base/tl/sorted_array.h>

class CInput : public IEngineInput
{
public:
	class CJoystick : public IJoystick
	{
		CInput *m_pInput;
		int m_Index;
		char m_aName[64];
		char m_aGUID[34];
		SDL_JoystickID m_InstanceID;
		int m_NumAxes;
		int m_NumButtons;
		int m_NumBalls;
		int m_NumHats;
		SDL_Joystick *m_pDelegate;

		CInput *Input() { return m_pInput; }

	public:
		CJoystick() { /* empty constructor for sorted_array */ }
		CJoystick(CInput *pInput, int Index, SDL_Joystick *pDelegate);

		virtual int GetIndex() const override { return m_Index; }
		virtual const char *GetName() const override { return m_aName; }
		const char *GetGUID() const { return m_aGUID; }
		SDL_JoystickID GetInstanceID() const { return m_InstanceID; }
		virtual int GetNumAxes() const override { return m_NumAxes; }
		virtual int GetNumButtons() const override { return m_NumButtons; }
		virtual int GetNumBalls() const override { return m_NumBalls; }
		virtual int GetNumHats() const override { return m_NumHats; }
		virtual float GetAxisValue(int Axis) override;
		virtual int GetHatValue(int Hat) override;
		virtual bool Relative(float *pX, float *pY) override;
		virtual bool Absolute(float *pX, float *pY) override;

		static int GetJoystickHatKey(int Hat, int HatValue);
	};

private:
	IEngineGraphics *m_pGraphics;
	CConfig *m_pConfig;
	IConsole *m_pConsole;

	IEngineGraphics *Graphics() { return m_pGraphics; }
	CConfig *Config() { return m_pConfig; }
	IConsole *Console() { return m_pConsole; }

	// joystick
	array<CJoystick> m_aJoysticks;
	CJoystick *m_pActiveJoystick;
	void InitJoysticks();
	void CloseJoysticks();
	void UpdateActiveJoystick();
	static void ConchainJoystickGuidChanged(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	float GetJoystickDeadzone();

	bool m_MouseInputRelative;
	char *m_pClipboardText;

	bool m_MouseDoubleClick;

	// ime support
	char m_aComposition[MAX_COMPOSITION_ARRAY_SIZE];
	int m_CompositionCursor;
	int m_CompositionSelectedLength;
	int m_CompositionLength;
	char m_aaCandidates[MAX_CANDIDATES][MAX_CANDIDATE_ARRAY_SIZE];
	int m_CandidateCount;
	int m_CandidateSelectedIndex;

	void AddEvent(const char *pText, int Key, int Flags);
	virtual void Clear() override;
	virtual bool IsEventValid(CEvent *pEvent) const override { return pEvent->m_InputCount == m_InputCounter; }

	// quick access to input
	unsigned short m_aInputCount[g_MaxKeys];
	bool m_aInputState[g_MaxKeys];
	int m_InputCounter;

	void UpdateMouseState();
	void UpdateJoystickState();
	void HandleJoystickAxisMotionEvent(const SDL_Event &Event);
	void HandleJoystickButtonEvent(const SDL_Event &Event);
	void HandleJoystickHatMotionEvent(const SDL_Event &Event);

	void ClearKeyStates();
	bool KeyState(int Key) const;

	static const void *ClipboardImageCallback(void *pUser, const char *pType, size_t *pSize);
	static void ClipboardCleanupCallback(void *pUser);

public:
	CInput();

	virtual void Init() override;
	virtual void Shutdown() override;
	virtual int Update() override;

	virtual bool KeyIsPressed(int Key) const override { return KeyState(Key); }
	virtual bool KeyPress(int Key, bool CheckCounter) const override { return CheckCounter ? (m_aInputCount[Key] == m_InputCounter) : m_aInputCount[Key]; }

	virtual int NumJoysticks() const override { return m_aJoysticks.size(); }
	virtual CJoystick *GetActiveJoystick() override { return m_pActiveJoystick; }
	virtual void SelectNextJoystick() override;

	virtual void MouseModeRelative() override;
	virtual void MouseModeAbsolute() override;
	virtual bool MouseDoubleClick() override;
	virtual bool MouseRelative(float *pX, float *pY) override;

	virtual const char *GetClipboardText() override;
	virtual void SetClipboardText(const char *pText) override;
	virtual void SetClipboardImage(unsigned char *pData, int DataSize) override;

	virtual void StartTextInput() override;
	virtual void StopTextInput() override;
	virtual const char *GetComposition() const override { return m_aComposition; }
	virtual bool HasComposition() const override { return m_CompositionLength != COMP_LENGTH_INACTIVE; }
	virtual int GetCompositionCursor() const override { return m_CompositionCursor; }
	virtual int GetCompositionSelectedLength() const override { return m_CompositionSelectedLength; }
	virtual int GetCompositionLength() const override { return m_CompositionLength; }
	virtual const char *GetCandidate(int Index) const override { return m_aaCandidates[Index]; }
	virtual int GetCandidateCount() const override { return m_CandidateCount; }
	virtual int GetCandidateSelectedIndex() const override { return m_CandidateSelectedIndex; }
	virtual void SetCompositionWindowPosition(float X, float Y, float H) override;
};
#endif
