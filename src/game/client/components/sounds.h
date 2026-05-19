/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SOUNDS_H
#define GAME_CLIENT_COMPONENTS_SOUNDS_H

#include <engine/console.h>
#include <engine/shared/jobs.h>
#include <engine/sound.h>
#include <game/client/component.h>

class CSounds : public CComponent
{
	enum
	{
		QUEUE_SIZE = 32,
	};
	struct QueueEntry
	{
		int m_Channel;
		ISound::CSampleHandle m_Sample;
	} m_aQueue[QUEUE_SIZE];
	int m_QueuePos;
	int64 m_QueueWaitTime;
	class CJob m_SoundJob;
	bool m_WaitForSoundJob;

	ISound::CSampleHandle GetSampleId(int SetId);

	void UpdateChannelVolume();

public:
	// sound channels
	enum
	{
		CHN_GUI = 0,
		CHN_MUSIC,
		CHN_WORLD,
		CHN_GLOBAL,
		CHN_MAPSOUND,
	};

	virtual int GetInitAmount() const;
	virtual void OnInit();
	virtual void OnReset();
	virtual void OnStateChange(int NewState, int OldState);
	virtual void OnRender();

	void ClearQueue();
	void EnqueueSample(int Channel, ISound::CSampleHandle Sample);
	int PlaySample(int Channel, ISound::CSampleHandle Sample, float Vol, int Flags = 0);
	int PlaySampleAt(int Channel, ISound::CSampleHandle Sample, float Vol, vec2 Pos, int Flags = 0);
	void StopSample(ISound::CSampleHandle Sample);
	bool IsPlayingSample(ISound::CSampleHandle Sample);

	void Enqueue(int Channel, int SetId);
	void Play(int Channel, int SetId, float Vol);
	void PlayAt(int Channel, int SetId, float Vol, vec2 Pos);
	void Stop(int SetId);
	bool IsPlaying(int SetId);

	ISound::CSampleHandle LoadSampleMemory(const char *pContext, const unsigned char *pData, int DataSize);
	bool UnloadSample(ISound::CSampleHandle *pSample);
};

#endif
