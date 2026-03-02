// these parts are from DDNet codes which is under zlib license. see their license.txt
#ifndef GAME_CLIENT_COMPONENTS_MAPSOUNDS_H
#define GAME_CLIENT_COMPONENTS_MAPSOUNDS_H

#include <base/tl/array.h>
#include <engine/sound.h>

#include <game/client/component.h>
#include <game/mapitems.h>

class CMapSounds : public CComponent
{
	enum
	{
		MAX_MAPSOUNDS = 64,
	};

	ISound::CSampleHandle m_aSounds[MAX_MAPSOUNDS];
	int m_Count;

	class CSourceQueueEntry
	{
	public:
		int m_Sound;
		bool m_HighDetail;
		int m_Voice;
		const CMapItemGroup *m_pGroup;
		const CSoundSource *m_pSource;
	};
	array<CSourceQueueEntry> m_lSourceQueue;
	void Clear();

public:
	CMapSounds();

	void Play(int Channel, int SoundId);
	void PlayAt(int Channel, int SoundId, vec2 Position);

	virtual void OnMapLoad();
	virtual void OnRender();
	virtual void OnStateChange(int NewState, int OldState);
};

#endif // GAME_CLIENT_COMPONENTS_MAPSOUNDS_H
