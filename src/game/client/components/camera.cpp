/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <base/math.h>
#include <game/client/component.h>
#include <game/client/gameclient.h>
#include <game/collision.h>

#include "camera.h"
#include "controls.h"

CCamera::CCamera()
{
	m_CamType = CAMTYPE_UNDEFINED;
	m_RotationCenter = vec2(0.0f, 0.0f);
	m_AnimationStartPos = vec2(0.0f, 0.0f);
	m_Center = vec2(0.0f, 0.0f);
	m_PrevCenter = vec2(0.0f, 0.0f);
	m_MenuCenter = vec2(0.0f, 0.0f);
	m_MenuZoom = 0.7f;
	m_GameCenter = vec2(0.0f, 0.0f);
	m_GameZoom = 1.0f;

	m_EnteringGame = false;
	m_JoinTransitionActive = false;
	m_JoinTransitionTime = 0.0f;
	m_JoinTransitionStartCenter = vec2(0.0f, 0.0f);
	m_JoinTransitionStartZoom = 1.0f;

	m_PrevFollowPos = vec2(0.0f, 0.0f);
	m_PositionJumpActive = false;
	m_PositionJumpTime = 0.0f;
	m_PositionJumpStartCenter = vec2(0.0f, 0.0f);

	m_Zoom = 0.7f;

	m_Positions[POS_START] = vec2(500.0f, 500.0f);
	m_Positions[POS_INTERNET] = vec2(1000.0f, 1000.0f);
	m_Positions[POS_LAN] = vec2(1100.0f, 1000.0f);
	m_Positions[POS_DEMOS] = vec2(1500.0f, 500.0f);
	m_Positions[POS_SETTINGS_GENERAL] = vec2(500.0f, 1000.0f);
	m_Positions[POS_SETTINGS_PLAYER] = vec2(600.0f, 1000.0f);
	m_Positions[POS_SETTINGS_TBD] = vec2(700.0f, 1000.0f);
	m_Positions[POS_SETTINGS_CONTROLS] = vec2(800.0f, 1000.0f);
	m_Positions[POS_SETTINGS_GRAPHICS] = vec2(900.0f, 1000.0f);
	m_Positions[POS_SETTINGS_SOUND] = vec2(1000.0f, 1000.0f);

	m_CurrentPosition = -1;
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

		// A jump the current velocity cannot explain is a respawn, a teleport
		// or a switch of the spectated tee: smooth it out instead of snapping.
		const float Ticks = maximum(Client()->RenderFrameTime() * Client()->GameTickSpeed(), 1.0f);
		const float MaxStep = maximum(FollowedVelocity() * 4.0f, POSITION_JUMP_MIN_DISTANCE) * Ticks;
		if(!m_JoinTransitionActive && distance(m_pClient->m_LocalCharacterPos, m_PrevFollowPos) > MaxStep)
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
	else
	{
		m_Zoom = 0.7f;
		static vec2 s_Dir = vec2(1.0f, 0.0f);

		if(distance(m_Center, m_RotationCenter) <= (float) Config()->m_ClRotationRadius + 0.5f)
		{
			// do little rotation
			float RotPerTick = 360.0f / (float) Config()->m_ClRotationSpeed * Client()->RenderFrameTime();
			s_Dir = rotate(s_Dir, RotPerTick);
			m_Center = m_RotationCenter + s_Dir * (float) Config()->m_ClRotationRadius;
		}
		else
		{
			// positions for the animation
			s_Dir = normalize(m_AnimationStartPos - m_RotationCenter);
			vec2 TargetPos = m_RotationCenter + s_Dir * (float) Config()->m_ClRotationRadius;
			float Distance = distance(m_AnimationStartPos, TargetPos);

			// move time
			m_MoveTime += Client()->RenderFrameTime() * Config()->m_ClCameraSpeed / 10.0f;
			float XVal = 1 - m_MoveTime;
			XVal = pow(XVal, 7.0f);

			m_Center = TargetPos + s_Dir * (XVal * Distance);
		}
	}

	m_PrevCenter = m_Center;
}

void CCamera::ChangePosition(int PositionNumber)
{
	if(m_pClient->Client()->State() == IClient::STATE_ONLINE)
		return; // Do not change Main Menu Camera Positions while we are changing settings in-game

	if(PositionNumber < 0 || PositionNumber > NUM_POS - 1)
		return;

	m_AnimationStartPos = m_Center;
	m_RotationCenter = m_Positions[PositionNumber];
	m_CurrentPosition = PositionNumber;
	m_MoveTime = 0.0f;
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
	else if(Snap.m_SpecInfo.m_Active && Snap.m_SpecInfo.m_SpectatorID >= 0 && Snap.m_SpecInfo.m_SpectatorID < MAX_CLIENTS && Snap.m_aCharacters[Snap.m_SpecInfo.m_SpectatorID].m_Active)
		pCharacter = &Snap.m_aCharacters[Snap.m_SpecInfo.m_SpectatorID].m_Cur;

	if(!pCharacter)
		return 0.0f;
	return length(vec2(pCharacter->m_VelX / 256.0f, pCharacter->m_VelY / 256.0f));
}
