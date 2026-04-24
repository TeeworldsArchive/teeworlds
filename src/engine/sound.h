/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SOUND_H
#define ENGINE_SOUND_H

#include "kernel.h"

struct CAudioDevice
{
	int m_DeviceID;
	char m_aName[64];
};

class ISound : public IInterface
{
	MACRO_INTERFACE("sound", 0)
public:
	enum
	{
		FLAG_LOOP = 1,
		FLAG_POS = 2,
		FLAG_NO_PANNING = 4,
		FLAG_ALL = FLAG_LOOP | FLAG_POS | FLAG_NO_PANNING
	};

	enum
	{
		SHAPE_CIRCLE,
		SHAPE_RECTANGLE,
	};

	struct CVoiceShapeCircle
	{
		float m_Radius;
	};

	struct CVoiceShapeRectangle
	{
		float m_Width;
		float m_Height;
	};

	class CSampleHandle
	{
		friend class ISound;
		int m_Id;

	public:
		CSampleHandle() : m_Id(-1)
		{
		}

		bool IsValid() const { return Id() >= 0; }
		int Id() const { return m_Id; }
		void Invalidate() { m_Id = -1; }
	};

	virtual bool IsSoundEnabled() = 0;

	virtual CSampleHandle LoadOpusMemory(const char *pContext, const unsigned char *pData, int DataSize) = 0;
	virtual CSampleHandle LoadOpus(const char *pFilename) = 0;
	virtual bool UnloadSample(CSampleHandle *pSampleID) = 0;

	virtual void SetChannelVolume(int ChannelID, float Volume) = 0;
	virtual void SetChannelPan(int ChannelID, float Pan) = 0;
	virtual void SetListenerPos(float x, float y) = 0;
	virtual void SetMaxDistance(float Distance) = 0;

	virtual int PlayAt(int ChannelID, CSampleHandle SampleID, float Volume, int Flags, float x, float y) = 0;
	virtual int Play(int ChannelID, CSampleHandle SampleID, float Volume, int Flags) = 0;
	virtual void Stop(CSampleHandle Sound) = 0;
	virtual void StopAll() = 0;
	virtual bool IsPlaying(CSampleHandle Sound) = 0;

	virtual void SetVoiceVolume(int VoiceID, float Volume) = 0;
	virtual void SetVoiceFalloff(int VoiceID, float Falloff) = 0;
	virtual void SetVoicePos(int VoiceID, float x, float y) = 0;
	virtual void SetVoiceCircle(int VoiceID, float Radius) = 0;
	virtual void SetVoiceRectangle(int VoiceID, float Width, float Height) = 0;
	virtual void SetVoiceTimeOffset(int VoiceID, float Offset) = 0;
	virtual void StopVoice(int VoiceID) = 0;

	virtual void SwitchAudioDevice(int NewDeviceIndex) = 0;
	virtual int GetAudioDevices(CAudioDevice *pDevices, int MaxDevices) = 0;

protected:
	inline CSampleHandle CreateSampleHandle(int Index)
	{
		CSampleHandle Tex;
		Tex.m_Id = Index;
		return Tex;
	}
};

class IEngineSound : public ISound
{
	MACRO_INTERFACE("enginesound", 0)
public:
	virtual int Init() = 0;
	virtual int Update() = 0;
	virtual int Shutdown() = 0;
};

extern IEngineSound *CreateEngineSound();

#endif
