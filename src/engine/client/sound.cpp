/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/storage.h>

#include <engine/shared/config.h>

#include <SDL3/SDL.h>
#include <opus.h>
#include <opusfile.h>

#include "sound.h"

#include <limits.h>
#include <math.h>

enum
{
	NUM_SAMPLES = 512,
	NUM_VOICES = 64,
	NUM_CHANNELS = 16,
};

struct CSample
{
	short *m_pData;
	int m_NumFrames;
	int m_Rate;
	int m_Channels;
	int m_LoopStart;
	int m_LoopEnd;
	int m_PausedAt;
};

struct CChannel
{
	int m_Vol; // 0 - 255
};

struct CVoice
{
	CSample *m_pSample;
	CChannel *m_pChannel;
	int m_Tick;
	int m_Vol; // 0 - 255
	int m_Flags;
	int m_X, m_Y;
};

static CSample m_aSamples[NUM_SAMPLES] = {{0}};
static CVoice m_aVoices[NUM_VOICES] = {{0}};
static CChannel m_aChannels[NUM_CHANNELS];

static LOCK m_SoundLock = 0;

static int m_CenterX = 0;
static int m_CenterY = 0;

static float m_MaxDistance = 1500.0f;

static int m_MixingRate = 48000;
static int m_SoundVolume = 100;

static int m_NextVoice = 0;
static int *m_pMixBuffer = 0; // buffer only used by the thread callback function
static unsigned m_MaxFrames = 0;

static short Int2Short(int i)
{
	return clamp(i, SHRT_MIN, SHRT_MAX);
}

static void Mix(short *pFinalOut, unsigned Frames)
{
	int MasterVol;
	mem_zero(m_pMixBuffer, m_MaxFrames * 2 * sizeof(int));
	Frames = minimum(Frames, m_MaxFrames);

	// aquire lock while we are mixing
	lock_wait(m_SoundLock);

	MasterVol = m_SoundVolume;

	for(unsigned i = 0; i < NUM_VOICES; i++)
	{
		if(m_aVoices[i].m_pSample)
		{
			// mix voice
			CVoice *v = &m_aVoices[i];
			int *pOut = m_pMixBuffer;

			int Step = v->m_pSample->m_Channels; // setup input sources
			short *pInL = &v->m_pSample->m_pData[v->m_Tick * Step];
			short *pInR = &v->m_pSample->m_pData[v->m_Tick * Step + 1];

			unsigned End = v->m_pSample->m_NumFrames - v->m_Tick;

			int Rvol = v->m_pChannel->m_Vol;
			int Lvol = v->m_pChannel->m_Vol;

			// make sure that we don't go outside the sound data
			if(Frames < End)
				End = Frames;

			// check if we have a mono sound
			if(v->m_pSample->m_Channels == 1)
				pInR = pInL;

			// volume calculation
			if(v->m_Flags & ISound::FLAG_POS)
			{
				int dx = v->m_X - m_CenterX;
				int dy = v->m_Y - m_CenterY;
				float Dist = sqrtf((float) dx * dx + dy * dy);
				if(Dist >= 0.0f && Dist < m_MaxDistance)
				{
					// linear falloff
					float Falloff = 1.0f - Dist / m_MaxDistance;

					// amplitude after falloff
					float FalloffAmp = v->m_pChannel->m_Vol * Falloff;

					// distribute volume to the channels depending on x difference
					float Lpan = 0.5f - dx / m_MaxDistance / 2.0f;
					float Rpan = 1.0f - Lpan;

					// apply square root to preserve sound power after panning
					float LampFactor = sqrt(Lpan);
					float RampFactor = sqrt(Rpan);

					// volume of the channels
					Lvol = FalloffAmp * LampFactor;
					Rvol = FalloffAmp * RampFactor;
				}
				else
				{
					Lvol = 0;
					Rvol = 0;
				}
			}

			// process all frames
			for(unsigned s = 0; s < End; s++)
			{
				*pOut++ += (*pInL) * Lvol;
				*pOut++ += (*pInR) * Rvol;
				pInL += Step;
				pInR += Step;
				v->m_Tick++;
			}

			// free voice if not used any more
			if(v->m_Tick == v->m_pSample->m_NumFrames)
			{
				if(v->m_Flags & ISound::FLAG_LOOP)
					v->m_Tick = 0;
				else
					v->m_pSample = 0;
			}
		}
	}

	// release the lock
	lock_unlock(m_SoundLock);

	// clamp accumulated values
	// TODO: this seams slow
	for(unsigned i = 0; i < Frames; ++i)
	{
		int j = i << 1;
		int vl = ((m_pMixBuffer[j] * MasterVol) / 101) >> 8;
		int vr = ((m_pMixBuffer[j + 1] * MasterVol) / 101) >> 8;

		pFinalOut[j] = Int2Short(vl);
		pFinalOut[j + 1] = Int2Short(vr);
	}

#if defined(CONF_ARCH_ENDIAN_BIG)
	swap_endian(pFinalOut, sizeof(short), Frames * 2);
#endif
}

static void SDLNewCallback(void *pUnused, SDL_AudioStream *pStream, int AdditionalAmount, int TotalAmount)
{
	(void) pUnused;
	/* Calculate a little more audio here, maybe using `userdata`, write it to `stream`
	 *
	 * If you want to use the original callback, you could do something like this:
	 */
	if(AdditionalAmount > 0)
	{
		Uint8 *pData = SDL_stack_alloc(Uint8, AdditionalAmount);
		if(pData)
		{
			Mix((short *) pData, AdditionalAmount / 2 / 2);
			SDL_PutAudioStreamData(pStream, pData, AdditionalAmount);
			SDL_stack_free(pData);
		}
	}
}

int CSound::Init()
{
	for(int i = 0; i < NUM_CHANNELS; ++i)
		m_aChannels[i].m_Vol = 255;

	m_SoundEnabled = 0;
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	m_SoundLock = lock_create();

	if(!m_pConfig->m_SndInit)
		return 0;

	if(!SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		dbg_msg("gfx", "unable to init SDL audio: %s", SDL_GetError());
		return -1;
	}

	m_MixingRate = m_pConfig->m_SndRate;

	m_MixingRate = m_pConfig->m_SndRate;

	SDL_AudioSpec Format;
	Format.freq = m_pConfig->m_SndRate;
	Format.format = SDL_AUDIO_S16;
	Format.channels = 2;

	SDL_AudioStream *pStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &Format, SDLNewCallback, nullptr);
	// Open the audio device and start playing sound!
	if(!pStream)
	{
		dbg_msg("client/sound", "unable to open audio: %s", SDL_GetError());
		return -1;
	}
	else
		dbg_msg("client/sound", "sound init successful using audio driver '%s'", SDL_GetCurrentAudioDriver());

	m_MaxFrames = m_pConfig->m_SndBufferSize * 2;
	m_pMixBuffer = (int *) mem_alloc(m_MaxFrames * 2 * sizeof(int));

	SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(pStream));

	m_SoundEnabled = 1;
	Update(); // update the volume
	return 0;
}

int CSound::Update()
{
	// update volume
	int WantedVolume = m_pConfig->m_SndVolume;

	if(!m_pGraphics->WindowActive() && m_pConfig->m_SndNonactiveMute)
		WantedVolume = 0;

	if(WantedVolume != m_SoundVolume)
	{
		lock_wait(m_SoundLock);
		m_SoundVolume = WantedVolume;
		lock_unlock(m_SoundLock);
	}

	return 0;
}

int CSound::Shutdown()
{
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	lock_destroy(m_SoundLock);
	if(m_pMixBuffer)
	{
		mem_free(m_pMixBuffer);
		m_pMixBuffer = 0;
	}
	return 0;
}

int CSound::AllocID()
{
	// TODO: linear search, get rid of it
	for(unsigned SampleID = 0; SampleID < NUM_SAMPLES; SampleID++)
	{
		if(m_aSamples[SampleID].m_pData == 0x0)
			return SampleID;
	}

	return -1;
}

void CSound::RateConvert(int SampleID)
{
	CSample *pSample = &m_aSamples[SampleID];
	int NumFrames = 0;
	short *pNewData = 0;

	// make sure that we need to convert this sound
	if(!pSample->m_pData || pSample->m_Rate == m_MixingRate)
		return;

	// allocate new data
	NumFrames = (int) ((pSample->m_NumFrames / (float) pSample->m_Rate) * m_MixingRate);
	pNewData = (short *) mem_alloc(NumFrames * pSample->m_Channels * sizeof(short));

	for(int i = 0; i < NumFrames; i++)
	{
		// resample TODO: this should be done better, like linear at least
		float a = i / (float) NumFrames;
		int f = (int) (a * pSample->m_NumFrames);
		if(f >= pSample->m_NumFrames)
			f = pSample->m_NumFrames - 1;

		// set new data
		if(pSample->m_Channels == 1)
			pNewData[i] = pSample->m_pData[f];
		else if(pSample->m_Channels == 2)
		{
			pNewData[i * 2] = pSample->m_pData[f * 2];
			pNewData[i * 2 + 1] = pSample->m_pData[f * 2 + 1];
		}
	}

	// free old data and apply new
	mem_free(pSample->m_pData);
	pSample->m_pData = pNewData;
	pSample->m_NumFrames = NumFrames;
}

ISound::CSampleHandle CSound::LoadOpus(const char *pFilename)
{
	CSample *pSample;
	int SampleID = -1;

	// don't waste memory on sound when we are stress testing
#ifdef CONF_DEBUG
	if(m_pConfig->m_DbgStress)
		return CSampleHandle();
#endif

	// no need to load sound when we are running with no sound
	if(!m_SoundEnabled)
		return CSampleHandle();

	if(!m_pStorage)
		return CSampleHandle();

	lock_wait(m_SoundLock);
	unsigned char *pFileData;
	unsigned FileSize;
	if(!m_pStorage->ReadFile(pFilename, IStorage::TYPE_ALL, (void **) &pFileData, &FileSize))
	{
		dbg_msg("sound/opus", "failed to open file. filename='%s'", pFilename);
		lock_unlock(m_SoundLock);
		return CSampleHandle();
	}

	int Error = 0;
	OggOpusFile *pOpusFile = op_open_memory(pFileData, FileSize, &Error);
	if(!pOpusFile)
	{
		dbg_msg("sound/opus", "failed to open opus file '%s': error %d", pFilename, Error);
		mem_free(pFileData);
		lock_unlock(m_SoundLock);
		return CSampleHandle();
	}

	int NumChannels = op_channel_count(pOpusFile, -1);
	;
	if(NumChannels < 0 || NumChannels > 2)
	{
		dbg_msg("sound/opus", "only mono/stereo supported. channels=%d, file='%s'", NumChannels, pFilename);
		op_free(pOpusFile);
		mem_free(pFileData);
		lock_unlock(m_SoundLock);
		return CSampleHandle();
	}

	int TotalSamples = op_pcm_total(pOpusFile, -1);
	if(TotalSamples < 0)
	{
		dbg_msg("sound/opus", "failed to get number of samples, error %d. file='%s'", TotalSamples, pFilename);
		op_free(pOpusFile);
		mem_free(pFileData);
		lock_unlock(m_SoundLock);
		return CSampleHandle();
	}

	SampleID = AllocID();
	if(SampleID < 0)
	{
		op_free(pOpusFile);
		mem_free(pFileData);
		lock_unlock(m_SoundLock);
		return CSampleHandle();
	}
	pSample = &m_aSamples[SampleID];
	pSample->m_pData = (short *) mem_alloc((size_t) TotalSamples * NumChannels * sizeof(short));
	if(!pSample->m_pData)
	{
		op_free(pOpusFile);
		mem_free(pFileData);
		lock_unlock(m_SoundLock);
		return CSampleHandle();
	}

	int Pos = 0;
	while(Pos < TotalSamples)
	{
		int Read = (int) op_read(pOpusFile, pSample->m_pData + Pos * NumChannels, (TotalSamples - Pos) * NumChannels, nullptr);
		if(Read < 0)
		{
			mem_free(pSample->m_pData);
			op_free(pOpusFile);
			mem_free(pFileData);
			dbg_msg("sound/opus", "op_read error %d at %d. file='%s'", Read, Pos, pFilename);
			return CSampleHandle();
		}
		else if(Read == 0) // EOF
			break;

		Pos += Read;
	}

	op_free(pOpusFile);
	mem_free(pFileData);

	pSample->m_Channels = NumChannels;
	pSample->m_Rate = 48000;
	pSample->m_NumFrames = (int) TotalSamples;
	pSample->m_LoopStart = -1;
	pSample->m_LoopEnd = -1;
	pSample->m_PausedAt = 0;

	if(m_pConfig->m_Debug)
		dbg_msg("sound/opus", "loaded %s (%d samples, %d channels)", pFilename, TotalSamples, NumChannels);

	RateConvert(SampleID);
	lock_unlock(m_SoundLock);
	return CreateSampleHandle(SampleID);
}

void CSound::SetListenerPos(float x, float y)
{
	m_CenterX = (int) x;
	m_CenterY = (int) y;
}

void CSound::SetMaxDistance(float Distance)
{
	m_MaxDistance = Distance;
}

void CSound::SetChannelVolume(int ChannelID, float Vol)
{
	m_aChannels[ChannelID].m_Vol = (int) (Vol * 255.0f);
}

int CSound::Play(int ChannelID, CSampleHandle SampleID, int Flags, float x, float y)
{
	if(!SampleID.IsValid())
		return -1;

	int VoiceID = -1;
	int i;

	lock_wait(m_SoundLock);

	// search for voice
	for(i = 0; i < NUM_VOICES; i++)
	{
		int id = (m_NextVoice + i) % NUM_VOICES;
		if(!m_aVoices[id].m_pSample)
		{
			VoiceID = id;
			m_NextVoice = id + 1;
			break;
		}
	}

	// voice found, use it
	if(VoiceID != -1)
	{
		m_aVoices[VoiceID].m_pSample = &m_aSamples[SampleID.Id()];
		m_aVoices[VoiceID].m_pChannel = &m_aChannels[ChannelID];
		if(Flags & FLAG_LOOP)
			m_aVoices[VoiceID].m_Tick = m_aSamples[SampleID.Id()].m_PausedAt;
		else
			m_aVoices[VoiceID].m_Tick = 0;
		m_aVoices[VoiceID].m_Vol = 255;
		m_aVoices[VoiceID].m_Flags = Flags;
		m_aVoices[VoiceID].m_X = (int) x;
		m_aVoices[VoiceID].m_Y = (int) y;
	}

	lock_unlock(m_SoundLock);
	return VoiceID;
}

int CSound::PlayAt(int ChannelID, CSampleHandle SampleID, int Flags, float x, float y)
{
	return Play(ChannelID, SampleID, Flags | ISound::FLAG_POS, x, y);
}

int CSound::Play(int ChannelID, CSampleHandle SampleID, int Flags)
{
	return Play(ChannelID, SampleID, Flags, 0, 0);
}

void CSound::Stop(CSampleHandle SampleID)
{
	if(!SampleID.IsValid())
		return;

	// TODO: a nice fade out
	lock_wait(m_SoundLock);
	CSample *pSample = &m_aSamples[SampleID.Id()];
	for(int i = 0; i < NUM_VOICES; i++)
	{
		if(m_aVoices[i].m_pSample == pSample)
		{
			if(m_aVoices[i].m_Flags & FLAG_LOOP)
				m_aVoices[i].m_pSample->m_PausedAt = m_aVoices[i].m_Tick;
			else
				m_aVoices[i].m_pSample->m_PausedAt = 0;
			m_aVoices[i].m_pSample = 0;
		}
	}
	lock_unlock(m_SoundLock);
}

void CSound::StopAll()
{
	// TODO: a nice fade out
	lock_wait(m_SoundLock);
	for(int i = 0; i < NUM_VOICES; i++)
	{
		if(m_aVoices[i].m_pSample)
		{
			if(m_aVoices[i].m_Flags & FLAG_LOOP)
				m_aVoices[i].m_pSample->m_PausedAt = m_aVoices[i].m_Tick;
			else
				m_aVoices[i].m_pSample->m_PausedAt = 0;
		}
		m_aVoices[i].m_pSample = 0;
	}
	lock_unlock(m_SoundLock);
}

bool CSound::IsPlaying(CSampleHandle SampleID)
{
	if(!SampleID.IsValid())
		return false;

	bool Ret = false;
	lock_wait(m_SoundLock);
	CSample *pSample = &m_aSamples[SampleID.Id()];
	for(int i = 0; i < NUM_VOICES; i++)
	{
		if(m_aVoices[i].m_pSample == pSample)
		{
			Ret = true;
		}
	}
	lock_unlock(m_SoundLock);
	return Ret;
}

IEngineSound *CreateEngineSound() { return new CSound; }
