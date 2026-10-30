/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <engine/shared/config.h>

#include <base/math.h>
#include <game/client/component.h>
#include <game/client/gameclient.h>
#include <game/collision.h>

#include "camera.h"
#include "controls.h"
#include "map.h"

// menu camera displacement curve: picks up speed immediately and settles
// softly instead of the symmetric ease-in/ease-out that felt stiff
static float MenuTransitionEase(float T)
{
	const float U = 1.0f - clamp(T, 0.0f, 1.0f);
	return 1.0f - U * U * U;
}

CCamera::CCamera()
{
	m_CamType = CAMTYPE_UNDEFINED;
	m_RotationCenter = vec2(0.0f, 0.0f);
	m_AnimationStartPos = vec2(0.0f, 0.0f);
	m_Center = vec2(0.0f, 0.0f);
	m_PrevCenter = vec2(0.0f, 0.0f);
	m_MenuCenter = vec2(0.0f, 0.0f);
	m_MenuZoom = 0.7f;
	m_MenuPan = vec2(0.0f, 0.0f);
	m_MenuPanTarget = vec2(0.0f, 0.0f);
	m_MenuPanBoundsMin = vec2(0.0f, 0.0f);
	m_MenuPanBoundsMax = vec2(0.0f, 0.0f);
	m_MenuPanBoundsSet = false;
	m_GameCenter = vec2(0.0f, 0.0f);
	m_GameZoom = 1.0f;

	m_EnteringGame = false;
	m_JoinTransitionActive = false;
	m_JoinTransitionTime = 0.0f;
	m_JoinTransitionStartCenter = vec2(0.0f, 0.0f);
	m_JoinTransitionStartZoom = 1.0f;

	m_SnapNextFrame = false;
	m_ViewPosOffset = vec2(0.0f, 0.0f);
	m_PrevFollowPos = vec2(0.0f, 0.0f);
	m_PositionJumpActive = false;
	m_PositionJumpTime = 0.0f;
	m_PositionJumpStartCenter = vec2(0.0f, 0.0f);

	m_Zoom = 0.7f;

	// main row: main menu, server list and demos from left to right. The
	// settings pages are an overlay and do not own a camera position.
	m_Positions[POS_START] = vec2(500.0f, 500.0f);
	m_Positions[POS_SERVERS] = vec2(1000.0f, 500.0f);
	m_Positions[POS_DEMOS] = vec2(1500.0f, 500.0f);

	m_CurrentPosition = -1;
	m_AnimationTarget = vec2(0.0f, 0.0f);
	m_MenuTransitionActive = false;
	m_MoveTime = 0.0f;
}

void CCamera::OnRender()
{
	if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		vec2 TargetCenter;

		// update camera center
		if(m_pClient->m_Snap.m_SpecInfo.m_Active && !m_pClient->m_Snap.m_SpecInfo.m_UsePosition &&
			(m_pClient->m_Snap.m_SpecInfo.m_SpecMode == SPEC_FREEVIEW || (m_pClient->m_Snap.m_pLocalInfo && !(m_pClient->m_Snap.m_pLocalInfo->m_Flag & TEEFLAG_DEAD))))
		{
			if(m_CamType != CAMTYPE_SPEC)
			{
				m_pClient->m_pControls->m_MousePos = m_PrevCenter;
				m_pClient->m_pControls->ClampMousePos();
				m_CamType = CAMTYPE_SPEC;
			}
			TargetCenter = m_pClient->m_pControls->m_MousePos;
		}
		else
		{
			if(m_CamType != CAMTYPE_PLAYER)
			{
				m_pClient->m_pControls->ClampMousePos();
				m_CamType = CAMTYPE_PLAYER;
			}

			float DeltaTime = Client()->RenderFrameTime();
			static vec2 s_LastMousePos(0, 0);
			static vec2 s_CurrentCameraOffset(0, 0);
			static float s_SpeedBias = 0.5f;

			if(Config()->m_ClCameraSmoothness > 0)
			{
				float CameraSpeed = (1.0f - (Config()->m_ClCameraSmoothness / 100.0f)) * 9.5f + 0.5f;
				float CameraStabilizingFactor = 1 + Config()->m_ClCameraStabilizing / 100.0f;

				s_SpeedBias += CameraSpeed * DeltaTime;
				if(Config()->m_ClDynamicCamera)
				{
					s_SpeedBias -= length(m_pClient->m_pControls->m_MousePos - s_LastMousePos) * log10f(CameraStabilizingFactor) * 0.02f;
					s_SpeedBias = clamp(s_SpeedBias, 0.5f, CameraSpeed);
				}
				else
				{
					s_SpeedBias = maximum(5.0f, CameraSpeed); // make sure toggle back is fast
				}
			}

			vec2 TargetCameraOffset(0, 0);
			s_LastMousePos = m_pClient->m_pControls->m_MousePos;
			float l = length(s_LastMousePos);
			if(Config()->m_ClDynamicCamera && l > 0.0001f) // make sure that this isn't 0
			{
				float DeadZone = Config()->m_ClMouseDeadzone;
				float FollowFactor = Config()->m_ClMouseFollowfactor / 100.0f;
				float OffsetAmount = maximum(l - DeadZone, 0.0f) * FollowFactor;

				TargetCameraOffset = normalize(m_pClient->m_pControls->m_MousePos) * OffsetAmount;
			}

			if(Config()->m_ClCameraSmoothness > 0)
				s_CurrentCameraOffset += (TargetCameraOffset - s_CurrentCameraOffset) * minimum(DeltaTime * s_SpeedBias, 1.0f);
			else
				s_CurrentCameraOffset = TargetCameraOffset;

			if(m_pClient->m_Snap.m_SpecInfo.m_Active)
				TargetCenter = m_pClient->m_Snap.m_SpecInfo.m_Position + s_CurrentCameraOffset;
			else
				TargetCenter = m_pClient->m_LocalCharacterPos + s_CurrentCameraOffset;
		}

		if(m_SnapNextFrame)
		{
			// keep the old view: Old = New - Offset
			TargetCenter = m_Center + m_ViewPosOffset;

			// a world switch: hand the view offset to the fading map and snap
			m_pClient->m_pMapComponent->SetFadeMapOffset(TargetCenter - m_Center);
			m_SnapNextFrame = false;
			m_PositionJumpActive = false;
			m_PrevFollowPos = m_pClient->m_LocalCharacterPos;
			m_Center = TargetCenter;
			m_Zoom = 1.0f;
		}
		else
		{
			// A jump the current velocity cannot explain is a respawn, a teleport
			// or a switch of the spectated tee: smooth it out instead of snapping,
			// unless the game data asks for an unsmoothed camera.
			const bool NoSmoothCamera = m_pClient->GameDataNoSmoothCamera();
			if(NoSmoothCamera)
				m_PositionJumpActive = false;
			const float Ticks = maximum(Client()->RenderFrameTime() * Client()->GameTickSpeed(), 1.0f);
			const float MaxStep = maximum(FollowedVelocity() * 4.0f, POSITION_JUMP_MIN_DISTANCE) * Ticks;
			if(!m_JoinTransitionActive && !NoSmoothCamera && distance(m_pClient->m_LocalCharacterPos, m_PrevFollowPos) > MaxStep)
			{
				m_PositionJumpActive = true;
				m_PositionJumpTime = 0.0f;
				m_PositionJumpStartCenter = m_PrevCenter;
			}
			m_PrevFollowPos = m_pClient->m_LocalCharacterPos;

			// when joining a game the camera flies in from where the menu was
			if(m_JoinTransitionActive)
			{
				m_JoinTransitionTime += Client()->RenderFrameTime();
				const float T = clamp(m_JoinTransitionTime * 1000.0f / JOIN_TRANSITION_TIME, 0.0f, 1.0f);
				const float Eased = 1.0f - powf(1.0f - T, 3.0f);
				m_Center = mix(m_JoinTransitionStartCenter, TargetCenter, Eased);
				m_Zoom = mix(m_JoinTransitionStartZoom, 1.0f, Eased);
				if(T >= 1.0f)
					m_JoinTransitionActive = false;
			}
			else if(m_PositionJumpActive)
			{
				m_PositionJumpTime += Client()->RenderFrameTime();
				const float T = clamp(m_PositionJumpTime * 1000.0f / POSITION_JUMP_TRANSITION_TIME, 0.0f, 1.0f);
				const float Eased = 1.0f - powf(1.0f - T, 3.0f);
				m_Center = mix(m_PositionJumpStartCenter, TargetCenter, Eased);
				m_Zoom = 1.0f;
				if(T >= 1.0f)
					m_PositionJumpActive = false;
			}
			else
			{
				m_Center = TargetCenter;
				m_Zoom = 1.0f;
			}
		}
	}
	else
	{
		m_Zoom = 0.7f;
		static vec2 s_Dir = vec2(1.0f, 0.0f);

		// the gallery pan is applied on top of the menu camera, the rotation
		// and the position animation always work with the un-panned center
		const vec2 BaseCenter = m_Center - m_MenuPan;

		if(m_MenuTransitionActive)
		{
			// fly from the old view point to the new one on a quick ease-out
			// curve; the menu page follows this same progress
			m_MoveTime = minimum(1.0f, m_MoveTime + Client()->RenderFrameTime() * Config()->m_ClCameraSpeed / 1.5f);
			m_Center = mix(m_AnimationStartPos, m_AnimationTarget, MenuTransitionEase(m_MoveTime));
			if(m_MoveTime >= 1.0f)
			{
				m_MenuTransitionActive = false;
				const vec2 Dir = m_Center - m_RotationCenter;
				if(length(Dir) > 0.001f)
					s_Dir = normalize(Dir);
			}
		}
		else if(distance(BaseCenter, m_RotationCenter) <= (float) Config()->m_ClRotationRadius + 0.5f)
		{
			// do little rotation
			float RotPerTick = 360.0f / (float) Config()->m_ClRotationSpeed * Client()->RenderFrameTime();
			s_Dir = rotate(s_Dir, RotPerTick);
			m_Center = m_RotationCenter + s_Dir * (float) Config()->m_ClRotationRadius;
		}
		else
		{
			// the menu camera is outside its rotation circle (e.g. after a game),
			// fly back in on the same smooth path
			m_AnimationStartPos = BaseCenter;
			vec2 Dir = m_AnimationStartPos - m_RotationCenter;
			Dir = length(Dir) > 0.001f ? normalize(Dir) : vec2(1.0f, 0.0f);
			m_AnimationTarget = m_RotationCenter + Dir * (float) Config()->m_ClRotationRadius;
			m_MoveTime = 0.0f;
			m_MenuTransitionActive = true;
		}

		// ease the gallery pan back to the menu origin once gallery mode is left
		if(distance(m_MenuPan, m_MenuPanTarget) > 0.25f)
			m_MenuPan += (m_MenuPanTarget - m_MenuPan) * minimum(Client()->RenderFrameTime() * 10.0f, 1.0f);
		else
			m_MenuPan = m_MenuPanTarget;

		// keep the gallery pan inside the map, but never fight the regular menu
		// camera: the un-panned center is always allowed
		if(m_MenuPanBoundsSet)
		{
			const vec2 Lo = vec2(minimum(BaseCenter.x, m_MenuPanBoundsMin.x), minimum(BaseCenter.y, m_MenuPanBoundsMin.y));
			const vec2 Hi = vec2(maximum(BaseCenter.x, m_MenuPanBoundsMax.x), maximum(BaseCenter.y, m_MenuPanBoundsMax.y));
			const vec2 PanMin = Lo - BaseCenter;
			const vec2 PanMax = Hi - BaseCenter;
			m_MenuPan = vec2(clamp(m_MenuPan.x, PanMin.x, PanMax.x), clamp(m_MenuPan.y, PanMin.y, PanMax.y));
			m_MenuPanTarget = vec2(clamp(m_MenuPanTarget.x, PanMin.x, PanMax.x), clamp(m_MenuPanTarget.y, PanMin.y, PanMax.y));
		}

		m_Center += m_MenuPan;
	}

	m_PrevCenter = m_Center;
}

void CCamera::ChangePosition(int PositionNumber)
{
	if(m_pClient->Client()->State() == IClient::STATE_ONLINE)
		return; // Do not change Main Menu Camera Positions while we are changing settings in-game

	if(PositionNumber < 0 || PositionNumber > NUM_POS - 1)
		return;

	m_AnimationStartPos = m_Center - m_MenuPan;
	m_RotationCenter = m_Positions[PositionNumber];

	// end on the rotation circle around the new view point, in the direction
	// we are coming from, so the idle rotation continues without a jump
	vec2 Dir = m_AnimationStartPos - m_RotationCenter;
	Dir = length(Dir) > 0.001f ? normalize(Dir) : vec2(1.0f, 0.0f);
	m_AnimationTarget = m_RotationCenter + Dir * (float) Config()->m_ClRotationRadius;

	m_CurrentPosition = PositionNumber;
	m_MoveTime = 0.0f;
	m_MenuTransitionActive = true;
}

float CCamera::GetMenuTransitionProgress() const
{
	return MenuTransitionEase(m_MoveTime);
}

void CCamera::ConSetPosition(IConsole::IResult *pResult, void *pUserData)
{
	CCamera *pSelf = (CCamera *) pUserData;
	int PositionNumber = clamp(pResult->GetInteger(0), 0, NUM_POS - 1);
	vec2 Position = vec2(pResult->GetInteger(1) * 32.0f + 16.0f, pResult->GetInteger(2) * 32.0f + 16.0f);
	pSelf->m_Positions[PositionNumber] = Position;

	// update
	if(pSelf->GetCurrentPosition() == PositionNumber)
		pSelf->ChangePosition(PositionNumber);
}

void CCamera::OnConsoleInit()
{
	Console()->Register("set_position", "iii", CFGFLAG_CLIENT, ConSetPosition, this, "Sets the rotation position");
}

void CCamera::OnStateChange(int NewState, int OldState)
{
	m_MenuTransitionActive = false;

	if(OldState == IClient::STATE_OFFLINE)
	{
		m_MenuCenter = m_Center;
		m_MenuZoom = m_Zoom;
		// we are leaving the menu, a game might follow
		m_EnteringGame = true;
	}
	else if(OldState >= IClient::STATE_ONLINE && NewState < IClient::STATE_ONLINE)
	{
		// remember where the game was last rendered so its map can fade out in place
		m_GameCenter = m_Center;
		m_GameZoom = m_Zoom;
		m_EnteringGame = false;
	}

	if(NewState >= IClient::STATE_ONLINE && OldState < IClient::STATE_ONLINE && m_EnteringGame)
	{
		// fly from the menu camera to the player camera instead of snapping
		m_JoinTransitionActive = true;
		m_JoinTransitionTime = 0.0f;
		m_JoinTransitionStartCenter = m_Center;
		m_JoinTransitionStartZoom = m_Zoom;
		m_EnteringGame = false;
	}
	else if(NewState < IClient::STATE_ONLINE)
	{
		m_JoinTransitionActive = false;
		m_PositionJumpActive = false;
	}

	// the first frame in a game must not look like the position jumped
	if(NewState >= IClient::STATE_ONLINE && OldState < IClient::STATE_ONLINE)
		m_PrevFollowPos = m_pClient->m_LocalCharacterPos;

	if(OldState != IClient::STATE_OFFLINE && NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		m_Center = m_MenuCenter;
}

float CCamera::FollowedVelocity() const
{
	const CGameClient::CSnapState &Snap = m_pClient->m_Snap;
	const CNetObj_Character *pCharacter = 0;

	if(Snap.m_pLocalCharacter)
		pCharacter = Snap.m_pLocalCharacter;
	else if(Snap.m_SpecInfo.m_Active && Snap.m_SpecInfo.m_SpectatorID >= 0)
	{
		const CGameClient::CSnapState::CCharacterInfo *pCharInfo = m_pClient->GetCharacterInfo(Snap.m_SpecInfo.m_SpectatorID);
		if(pCharInfo && pCharInfo->m_Active)
			pCharacter = &pCharInfo->m_Cur;
	}

	if(!pCharacter)
		return 0.0f;
	return length(vec2(pCharacter->m_VelX / 256.0f, pCharacter->m_VelY / 256.0f));
}
