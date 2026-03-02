/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_SOUND_H
#define ENGINE_CLIENT_SOUND_H

#include <engine/sound.h>

class CSound : public IEngineSound
{
	int m_SoundEnabled;

public:
	CConfig *m_pConfig;
	IEngineGraphics *m_pGraphics;
	IStorage *m_pStorage;

	virtual int Init();

	int Update();
	int Shutdown();
	int AllocID();

	static void RateConvert(int SampleID);

	virtual bool IsSoundEnabled() { return m_SoundEnabled != 0; }

	virtual CSampleHandle LoadOpusMemory(const char *pContext, const unsigned char *pData, int DataSize);
	virtual CSampleHandle LoadOpus(const char *pFilename);
	virtual bool UnloadSample(CSampleHandle *pSampleID);

	virtual void SetListenerPos(float x, float y);
	virtual void SetChannelVolume(int ChannelID, float Vol);
	virtual void SetChannelPan(int ChannelID, float Pan);
	virtual void SetMaxDistance(float Distance);

	int Play(int ChannelID, CSampleHandle SampleID, float Volume, int Flags, float x, float y);
	virtual int PlayAt(int ChannelID, CSampleHandle SampleID, float Volume, int Flags, float x, float y);
	virtual int Play(int ChannelID, CSampleHandle SampleID, float Volume, int Flags);
	virtual void Stop(CSampleHandle SampleID);
	virtual void StopAll();
	virtual bool IsPlaying(CSampleHandle SampleID);

	virtual void SetVoiceVolume(int VoiceID, float Volume);
	virtual void SetVoiceFalloff(int VoiceID, float Falloff);
	virtual void SetVoicePos(int VoiceID, float x, float y);
	virtual void SetVoiceCircle(int VoiceID, float Radius);
	virtual void SetVoiceRectangle(int VoiceID, float Width, float Height);
	virtual void SetVoiceTimeOffset(int VoiceID, float Offset);
	virtual void StopVoice(int VoiceID);
};

#endif
