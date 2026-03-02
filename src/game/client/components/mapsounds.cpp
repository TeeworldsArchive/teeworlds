#include "mapsounds.h"

#include <engine/demo.h>
#include <engine/shared/config.h>
#include <engine/sound.h>

#include <game/client/components/camera.h>
#include <game/client/components/maplayers.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/mapitems.h>

CMapSounds::CMapSounds()
{
	m_Count = 0;
}

void CMapSounds::Play(int Channel, int SoundId)
{
	if(SoundId < 0 || SoundId >= m_Count)
		return;

	m_pClient->m_pSounds->PlaySample(Channel, m_aSounds[SoundId], 0);
}

void CMapSounds::PlayAt(int Channel, int SoundId, vec2 Position)
{
	if(SoundId < 0 || SoundId >= m_Count)
		return;

	m_pClient->m_pSounds->PlaySampleAt(Channel, m_aSounds[SoundId], 0, Position);
}

void CMapSounds::OnMapLoad()
{
	IMap *pMap = Kernel()->RequestInterface<IMap>();

	Clear();

	if(!Sound()->IsSoundEnabled())
		return;

	// load samples
	int Start;
	pMap->GetType(MAPITEMTYPE_SOUND, &Start, &m_Count);

	m_Count = clamp<int>(m_Count, 0, MAX_MAPSOUNDS);

	// load new samples
	bool ShowWarning = false;
	for(int i = 0; i < m_Count; i++)
	{
		CMapItemSound *pSound = (CMapItemSound *) pMap->GetItem(Start + i, 0, 0);
		const char *pName = (char *) pMap->GetData(pSound->m_SoundName);
		if(pName == nullptr || pName[0] == '\0')
		{
			if(pSound->m_External)
			{
				dbg_msg("mapsounds", "Failed to load map sound %d: failed to load name.", i);
				ShowWarning = true;
				continue;
			}
			pName = "(error)";
		}

		if(pSound->m_External)
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			str_format(aBuf, sizeof(aBuf), "mapres/%s.opus", pName);
			m_aSounds[i] = Sound()->LoadOpus(aBuf);
			pMap->UnloadData(pSound->m_SoundName);
		}
		else
		{
			const unsigned char *pData = (unsigned char *) pMap->GetData(pSound->m_SoundData);
			if(pData == nullptr)
			{
				dbg_msg("mapsounds", "Failed to load map sound %d: failed to load data.", i);
				ShowWarning = true;
				continue;
			}
			const int SoundDataSize = pMap->GetDataSize(pSound->m_SoundData);
			m_aSounds[i] = Sound()->LoadOpusMemory(pName, pData, SoundDataSize);
			pMap->UnloadData(pSound->m_SoundData);
		}
		ShowWarning = ShowWarning || !m_aSounds[i].IsValid();
	}
	if(ShowWarning)
	{
		UI()->DoToast(Localize("Some map sounds could not be loaded. Check the local console for details."), CUI::TOAST_WARNING);
	}

	// enqueue sound sources
	for(int GroupIndex = 0; GroupIndex < Layers()->NumGroups(); GroupIndex++)
	{
		const CMapItemGroup *pGroup = Layers()->GetGroup(GroupIndex);
		if(!pGroup)
			continue;

		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			const CMapItemLayer *pLayer = Layers()->GetLayer(pGroup->m_StartLayer + LayerIndex);
			if(!pLayer)
				continue;
			if(pLayer->m_Type != LAYERTYPE_SOUNDS)
				continue;

			const CMapItemLayerSounds *pSoundLayer = reinterpret_cast<const CMapItemLayerSounds *>(pLayer);
			if(pSoundLayer->m_Version < 1 || pSoundLayer->m_Version > 2)
				continue;
			if(pSoundLayer->m_Sound < 0 || pSoundLayer->m_Sound >= m_Count || !m_aSounds[pSoundLayer->m_Sound].IsValid())
				continue;

			const CSoundSource *pSources = static_cast<CSoundSource *>(pMap->GetDataSwapped(pSoundLayer->m_Data));
			if(!pSources)
				continue;

			const int NumSources = minimum<int>(pSoundLayer->m_NumSources, pMap->GetDataSize(pSoundLayer->m_Data) / sizeof(CSoundSource));
			for(int SourceIndex = 0; SourceIndex < NumSources; SourceIndex++)
			{
				CSourceQueueEntry &Source = m_lSourceQueue.emplace();
				Source.m_Sound = pSoundLayer->m_Sound;
				Source.m_HighDetail = pLayer->m_Flags & LAYERFLAG_DETAIL;
				Source.m_pGroup = pGroup;
				Source.m_pSource = &pSources[SourceIndex];
				Source.m_Voice = -1;
			}
		}
	}
}

void CMapSounds::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	bool DemoPlayerPaused = Client()->State() == IClient::STATE_DEMOPLAYBACK && DemoPlayer()->BaseInfo()->m_Paused;

	// enqueue sounds
	for(int i = 0; i < m_lSourceQueue.size(); i++)
	{
		CSourceQueueEntry *pSource = &m_lSourceQueue[i];
		static float s_Time = 0.0f;
		if(m_pClient->m_Snap.m_pGameData)
		{
			s_Time = mix((Client()->PrevGameTick() - m_pClient->m_Snap.m_pGameData->m_GameStartTick) / (float) Client()->GameTickSpeed(),
				(Client()->GameTick() - m_pClient->m_Snap.m_pGameData->m_GameStartTick) / (float) Client()->GameTickSpeed(),
				Client()->IntraGameTick());
		}
		float Offset = s_Time - pSource->m_pSource->m_TimeDelay;
		if(!DemoPlayerPaused && Offset >= 0.0f && Config()->m_SndEnable && (Config()->m_GfxHighDetail || !pSource->m_HighDetail))
		{
			if(pSource->m_Voice != -1)
			{
				// currently playing, set offset
				Sound()->SetVoiceTimeOffset(pSource->m_Voice, Offset);
			}
			else
			{
				// need to enqueue
				int Flags = 0;
				if(pSource->m_pSource->m_Loop)
					Flags |= ISound::FLAG_LOOP;
				if(!pSource->m_pSource->m_Pan)
					Flags |= ISound::FLAG_NO_PANNING;

				pSource->m_Voice = m_pClient->m_pSounds->PlaySampleAt(CSounds::CHN_MAPSOUND, m_aSounds[pSource->m_Sound], 1.0f, vec2(fx2f(pSource->m_pSource->m_Position.x), fx2f(pSource->m_pSource->m_Position.y)), Flags);
				Sound()->SetVoiceTimeOffset(pSource->m_Voice, Offset);
				Sound()->SetVoiceFalloff(pSource->m_Voice, pSource->m_pSource->m_Falloff / 255.0f);
				switch(pSource->m_pSource->m_Shape.m_Type)
				{
					case CSoundShape::SHAPE_CIRCLE:
					{
						Sound()->SetVoiceCircle(pSource->m_Voice, pSource->m_pSource->m_Shape.m_Circle.m_Radius);
						break;
					}

					case CSoundShape::SHAPE_RECTANGLE:
					{
						Sound()->SetVoiceRectangle(pSource->m_Voice, fx2f(pSource->m_pSource->m_Shape.m_Rectangle.m_Width), fx2f(pSource->m_pSource->m_Shape.m_Rectangle.m_Height));
						break;
					}
				};
			}
		}
		else
		{
			// stop voice
			Sound()->StopVoice(pSource->m_Voice);
			pSource->m_Voice = -1;
		}
	}

	const vec2 Center = *m_pClient->m_pCamera->GetCenter();
	for(int i = 0; i < m_lSourceQueue.size(); i++)
	{
		CSourceQueueEntry *pSource = &m_lSourceQueue[i];
		if(pSource->m_Voice == -1)
			continue;

		float OffsetX = 0, OffsetY = 0;
		if(pSource->m_pSource->m_PosEnv >= 0)
		{
			float aChannels[4];
			CMapLayers::EnvelopeEval(pSource->m_pSource->m_PosEnvOffset, pSource->m_pSource->m_PosEnv, aChannels, m_pClient->m_pMapLayersBackGround);
			OffsetX = aChannels[0];
			OffsetY = aChannels[1];
		}
		float x = fx2f(pSource->m_pSource->m_Position.x) + OffsetX;
		float y = fx2f(pSource->m_pSource->m_Position.y) + OffsetY;

		x += Center.x * (1.0f - pSource->m_pGroup->m_ParallaxX / 100.0f);
		y += Center.y * (1.0f - pSource->m_pGroup->m_ParallaxY / 100.0f);

		x -= pSource->m_pGroup->m_OffsetX;
		y -= pSource->m_pGroup->m_OffsetY;

		Sound()->SetVoicePos(pSource->m_Voice, x, y);

		if(pSource->m_pSource->m_SoundEnv >= 0)
		{
			float aChannels[4];
			CMapLayers::EnvelopeEval(pSource->m_pSource->m_SoundEnvOffset / 1000.0f, pSource->m_pSource->m_SoundEnv, aChannels, m_pClient->m_pMapLayersBackGround);
			Sound()->SetVoiceVolume(pSource->m_Voice, clamp(aChannels[0], 0.0f, 1.0f));
		}
	}
}

void CMapSounds::Clear()
{
	// unload all samples
	m_lSourceQueue.clear();
	for(int i = 0; i < m_Count; i++)
	{
		Sound()->UnloadSample(&m_aSounds[i]);
	}
	m_Count = 0;
}

void CMapSounds::OnStateChange(int NewState, int OldState)
{
	if(NewState < IClient::STATE_ONLINE)
		Clear();
}
