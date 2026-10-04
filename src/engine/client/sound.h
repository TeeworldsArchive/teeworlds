/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
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

	virtual int Init() override;
	int InitAudioDevice(bool Reset);

	virtual int Update() override;
	virtual int Shutdown() override;
	int AllocID();

	static void RateConvert(int SampleID);

	virtual bool IsSoundEnabled() override { return m_SoundEnabled != 0; }

	virtual CSampleHandle LoadOpusMemory(const char *pContext, const unsigned char *pData, int DataSize) override;
	virtual CSampleHandle LoadOpus(const char *pFilename) override;
	virtual bool UnloadSample(CSampleHandle *pSampleID) override;

	virtual void SetListenerPos(float x, float y) override;
	virtual void SetChannelVolume(int ChannelID, float Vol) override;
	virtual void SetChannelPan(int ChannelID, float Pan) override;
	virtual void SetMaxDistance(float Distance) override;

	int Play(int ChannelID, CSampleHandle SampleID, float Volume, int Flags, float x, float y);
	virtual int PlayAt(int ChannelID, CSampleHandle SampleID, float Volume, int Flags, float x, float y) override;
	virtual int Play(int ChannelID, CSampleHandle SampleID, float Volume, int Flags) override;
	virtual void Stop(CSampleHandle SampleID) override;
	virtual void StopAll() override;
	virtual bool IsPlaying(CSampleHandle SampleID) override;

	virtual void SetVoiceVolume(int VoiceID, float Volume) override;
	virtual void SetVoiceFalloff(int VoiceID, float Falloff) override;
	virtual void SetVoicePos(int VoiceID, float x, float y) override;
	virtual void SetVoiceCircle(int VoiceID, float Radius) override;
	virtual void SetVoiceRectangle(int VoiceID, float Width, float Height) override;
	virtual void SetVoiceTimeOffset(int VoiceID, float Offset) override;
	virtual void StopVoice(int VoiceID) override;

	virtual void SwitchAudioDevice(int NewDeviceIndex) override;
	virtual int GetAudioDevices(CAudioDevice *pDevices, int MaxDevices) override;
};

#endif
