/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_COMPONENTS_CAMERA_H
#define GAME_CLIENT_COMPONENTS_CAMERA_H
#include <base/vmath.h>
#include <game/client/component.h>

class CCamera : public CComponent
{
	enum
	{
		// duration of the camera flying from the menu into the game in ms
		JOIN_TRANSITION_TIME = 500,

		// duration of the camera catching up with a jump of the followed
		// position (respawn, teleport, ...) in ms
		POSITION_JUMP_TRANSITION_TIME = 250,
	};

	// jumps smaller than this are normal movement and are applied right away,
	// in world units
	static constexpr float POSITION_JUMP_MIN_DISTANCE = 32.0f;

public:
	enum
	{
		POS_START = 0,
		POS_INTERNET,
		POS_LAN,
		POS_DEMOS,
		POS_SETTINGS_GENERAL, // order here should be the same like enum for settings pages in menu
		POS_SETTINGS_PLAYER,
		POS_SETTINGS_CONTROLS,
		POS_SETTINGS_GRAPHICS,
		POS_SETTINGS_SOUND,

		NUM_POS,
	};

	CCamera();
	virtual void OnRender() override;

	void ChangePosition(int PositionNumber);
	int GetCurrentPosition() const { return m_CurrentPosition; }

	// gallery mode: offset the menu camera by a free pan the player dragged
	void PanMenu(const vec2 &Delta)
	{
		m_MenuPan += Delta;
		m_MenuPanTarget += Delta;
	}
	// ease the menu camera pan back to its origin
	void ResetMenuPan() { m_MenuPanTarget = vec2(0.0f, 0.0f); }
	// limit the gallery pan to this world space rectangle (camera center bounds)
	void SetMenuPanBounds(const vec2 &Min, const vec2 &Max)
	{
		m_MenuPanBoundsMin = Min;
		m_MenuPanBoundsMax = Max;
		m_MenuPanBoundsSet = true;
	}
	void ClearMenuPanBounds() { m_MenuPanBoundsSet = false; }

	const vec2 *GetCenter() const { return &m_Center; }
	// camera center the menu was using when the game was joined, so that a menu
	// map can still be rendered correctly while it fades over the game map
	const vec2 *GetMenuCenter() const { return &m_MenuCenter; }
	float GetMenuZoom() const { return m_MenuZoom; }
	// camera the game was last rendered with, so that the game map can still be
	// rendered correctly while it fades out after a disconnect
	const vec2 *GetGameCenter() const { return &m_GameCenter; }
	float GetGameZoom() const { return m_GameZoom; }
	float GetZoom() const { return m_Zoom; }

	static void ConSetPosition(IConsole::IResult *pResult, void *pUserData);

	virtual void OnConsoleInit() override;
	virtual void OnStateChange(int NewState, int OldState) override;

private:
	enum
	{
		CAMTYPE_UNDEFINED = -1,
		CAMTYPE_SPEC,
		CAMTYPE_PLAYER,
	};

	// velocity of the tee the camera follows, in units per tick
	float FollowedVelocity() const;

	vec2 m_Center;
	vec2 m_MenuCenter;
	float m_MenuZoom;
	vec2 m_GameCenter;
	float m_GameZoom;

	// camera transition from the menu into the game
	bool m_EnteringGame;
	bool m_JoinTransitionActive;
	float m_JoinTransitionTime;
	vec2 m_JoinTransitionStartCenter;
	float m_JoinTransitionStartZoom;

	// camera transition when the followed position jumps (respawn, teleport)
	vec2 m_PrevFollowPos;
	bool m_PositionJumpActive;
	float m_PositionJumpTime;
	vec2 m_PositionJumpStartCenter;

	vec2 m_RotationCenter;
	vec2 m_MenuPan;
	vec2 m_MenuPanTarget;
	vec2 m_MenuPanBoundsMin;
	vec2 m_MenuPanBoundsMax;
	bool m_MenuPanBoundsSet;
	float m_Zoom;
	int m_CamType;
	vec2 m_PrevCenter;
	vec2 m_Positions[NUM_POS];
	int m_CurrentPosition;
	vec2 m_AnimationStartPos;
	float m_MoveTime;
};

#endif
