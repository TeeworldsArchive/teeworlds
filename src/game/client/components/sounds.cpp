/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/sound.h>
#include <game/client/components/camera.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <generated/client_data.h>
#include "sounds.h"

struct CUserData
{
	CGameClient *m_pGameClient;
	bool m_Render;
} g_UserData;

static int LoadSoundsThread(void *pUser)
{
	CUserData *pData = static_cast<CUserData *>(pUser);

	for(int s = 0; s < g_pData->m_NumSounds; s++)
	{
		for(int i = 0; i < g_pData->m_aSounds[s].m_NumSounds; i++)
		{
			ISound::CSampleHandle Id = pData->m_pGameClient->Sound()->LoadOpus(g_pData->m_aSounds[s].m_aSounds[i].m_pFilename);
			g_pData->m_aSounds[s].m_aSounds[i].m_Id = Id;
		}

		if(pData->m_Render)
			pData->m_pGameClient->m_pMenus->RenderLoading(1);
	}

	return 0;
}

ISound::CSampleHandle CSounds::GetSampleId(int SetId)
{
	if(!Config()->m_SndEnable || !Sound()->IsSoundEnabled() || m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return ISound::CSampleHandle();

	CDataSoundset *pSet = &g_pData->m_aSounds[SetId];
	if(!pSet->m_NumSounds)
		return ISound::CSampleHandle();

	if(pSet->m_NumSounds == 1)
		return pSet->m_aSounds[0].m_Id;

	// return random one
	int Id;
	do
	{
		Id = random_int() % pSet->m_NumSounds;
	} while(Id == pSet->m_Last);
	pSet->m_Last = Id;
	return pSet->m_aSounds[Id].m_Id;
}

int CSounds::GetInitAmount() const
{
	if(Config()->m_SndAsyncLoading)
		return 0;
	return g_pData->m_NumSounds;
}

void CSounds::OnInit()
{
	// setup sound channels
	Sound()->SetChannelVolume(CSounds::CHN_GUI, 1.0f);
	Sound()->SetChannelVolume(CSounds::CHN_MUSIC, 1.0f);
	Sound()->SetChannelVolume(CSounds::CHN_WORLD, 0.9f);
	Sound()->SetChannelVolume(CSounds::CHN_GLOBAL, 1.0f);
	Sound()->SetChannelVolume(CSounds::CHN_MAPSOUND, 0.7f);
	Sound()->SetChannelPan(CSounds::CHN_GUI, 0.0f);
	Sound()->SetChannelPan(CSounds::CHN_MUSIC, 1.0f);
	Sound()->SetChannelPan(CSounds::CHN_WORLD, 1.0f);
	Sound()->SetChannelPan(CSounds::CHN_GLOBAL, 0.0f);
	Sound()->SetChannelPan(CSounds::CHN_MAPSOUND, 1.0f);

	Sound()->SetListenerPos(0.0f, 0.0f);

	ClearQueue();

	// load sounds
	if(Config()->m_SndAsyncLoading)
	{
		g_UserData.m_pGameClient = m_pClient;
		g_UserData.m_Render = false;
		m_pClient->Engine()->AddJob(&m_SoundJob, LoadSoundsThread, &g_UserData);
		m_WaitForSoundJob = true;
	}
	else
	{
		g_UserData.m_pGameClient = m_pClient;
		g_UserData.m_Render = true;
		LoadSoundsThread(&g_UserData);
		m_WaitForSoundJob = false;
	}
}

void CSounds::OnReset()
{
	if(Client()->State() >= IClient::STATE_ONLINE)
	{
		Sound()->StopAll();
		ClearQueue();
	}
}

void CSounds::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK)
		OnReset();
}

void CSounds::OnRender()
{
	// check for sound initialisation
	if(m_WaitForSoundJob)
	{
		if(m_SoundJob.Status() == CJob::STATE_DONE)
			m_WaitForSoundJob = false;
		else
			return;
	}

	// set listener pos
	vec2 Pos = *m_pClient->m_pCamera->GetCenter();
	Sound()->SetListenerPos(Pos.x, Pos.y);

	// play sound from queue
	if(m_QueuePos > 0)
	{
		int64 Now = time_get();
		if(m_QueueWaitTime <= Now)
		{
			PlaySample(m_aQueue[0].m_Channel, m_aQueue[0].m_Sample, 1.0f);
			m_QueueWaitTime = Now + time_freq() * 3 / 10; // wait 300ms before playing the next one
			if(--m_QueuePos > 0)
				mem_move(m_aQueue, m_aQueue + 1, m_QueuePos * sizeof(QueueEntry));
		}
	}
}

void CSounds::ClearQueue()
{
	mem_zero(m_aQueue, sizeof(m_aQueue));
	m_QueuePos = 0;
	m_QueueWaitTime = time_get();
}

void CSounds::EnqueueSample(int Channel, ISound::CSampleHandle Sample)
{
	if(m_pClient->m_SuppressEvents)
		return;
	if(m_QueuePos >= QUEUE_SIZE)
		return;
	if(Channel != CHN_MUSIC && Config()->m_ClEditor)
		return;
	if(!Sample.IsValid())
		return;

	m_aQueue[m_QueuePos].m_Channel = Channel;
	m_aQueue[m_QueuePos++].m_Sample = Sample;
}

int CSounds::PlaySample(int Channel, ISound::CSampleHandle Sample, float Vol, int Flags)
{
	if(m_pClient->m_SuppressEvents)
		return -1;
	if(Channel == CHN_MUSIC && !Config()->m_SndMusic)
		return -1;

	if(!Sample.IsValid())
		return -1;

	if(Channel == CHN_MUSIC)
		Flags |= ISound::FLAG_LOOP;

	return Sound()->Play(Channel, Sample, Vol, Flags);
}

int CSounds::PlaySampleAt(int Channel, ISound::CSampleHandle Sample, float Vol, vec2 Pos, int Flags)
{
	if(m_pClient->m_SuppressEvents)
		return -1;
	if(Channel == CHN_MUSIC && !Config()->m_SndMusic)
		return -1;

	if(!Sample.IsValid())
		return -1;

	if(Channel == CHN_MUSIC)
		Flags |= ISound::FLAG_LOOP;

	return Sound()->PlayAt(Channel, Sample, Vol, Flags, Pos.x, Pos.y);
}

void CSounds::StopSample(ISound::CSampleHandle Sample)
{
	if(m_WaitForSoundJob)
		return;

	Sound()->Stop(Sample);
}

bool CSounds::IsPlayingSample(ISound::CSampleHandle Sample)
{
	if(m_WaitForSoundJob)
		return false;
	return Sound()->IsPlaying(Sample);
}

void CSounds::Enqueue(int Channel, int SetId)
{
	EnqueueSample(Channel, GetSampleId(SetId));
}

void CSounds::Play(int Channel, int SetId, float Vol)
{
	PlaySample(Channel, GetSampleId(SetId), Vol);
}

void CSounds::PlayAt(int Channel, int SetId, float Vol, vec2 Pos)
{
	PlaySampleAt(Channel, GetSampleId(SetId), Vol, Pos);
}

void CSounds::Stop(int SetId)
{
	if(m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return;

	CDataSoundset *pSet = &g_pData->m_aSounds[SetId];

	for(int i = 0; i < pSet->m_NumSounds; i++)
		StopSample(pSet->m_aSounds[i].m_Id);
}

bool CSounds::IsPlaying(int SetId)
{
	if(m_WaitForSoundJob || SetId < 0 || SetId >= g_pData->m_NumSounds)
		return false;

	CDataSoundset *pSet = &g_pData->m_aSounds[SetId];
	for(int i = 0; i < pSet->m_NumSounds; i++)
	{
		if(IsPlayingSample(pSet->m_aSounds[i].m_Id))
			return true;
	}
	return false;
}

ISound::CSampleHandle CSounds::LoadSampleMemory(const char *pContext, const unsigned char *pData, int DataSize)
{
	return m_pClient->Sound()->LoadOpusMemory(pContext, pData, DataSize);
}

bool CSounds::UnloadSample(ISound::CSampleHandle *pSample)
{
	return Sound()->UnloadSample(pSample);
}
