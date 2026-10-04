/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* Portions from DDNet (zlib license) - https://github.com/ddnet/ddnet                       */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
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

	virtual void OnMapLoad() override;
	virtual void OnMapUnload() override;
	virtual void OnRender() override;
	virtual void OnStateChange(int NewState, int OldState) override;
};

#endif // GAME_CLIENT_COMPONENTS_MAPSOUNDS_H
