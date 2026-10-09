/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_GAMECORE_H
#define GAME_GAMECORE_H

#include <base/math.h>
#include <base/system/debug.h>
#include <base/system/fs.h>
#include <base/system/mem.h>
#include <base/tl/partial_array.h>

#include <engine/console.h>
#include <engine/shared/protocol.h>
#include <generated/protocol.h>
#include "collision.h"
#include <math.h>

class CTuneParam
{
	int m_Value;

public:
	void Set(int v) { m_Value = v; }
	int Get() const { return m_Value; }
	CTuneParam &operator=(int v)
	{
		m_Value = (int) (v * 100.0f);
		return *this;
	}
	CTuneParam &operator=(float v)
	{
		m_Value = (int) (v * 100.0f);
		return *this;
	}
	operator float() const { return m_Value / 100.0f; }
};

class CTuningParams
{
	static const char *ms_apNames[];

public:
	CTuningParams()
	{
		const float TicksPerSecond = 50.0f;
#define MACRO_TUNING_PARAM(Name, ScriptName, Value) m_##Name.Set((int) (Value * 100.0f));
#include "tuning.h"
#undef MACRO_TUNING_PARAM
	}

#define MACRO_TUNING_PARAM(Name, ScriptName, Value) CTuneParam m_##Name;
#include "tuning.h"
#undef MACRO_TUNING_PARAM

	static constexpr int Num() { return sizeof(CTuningParams) / sizeof(CTuneParam); }
	bool Set(int Index, float Value);
	bool Set(const char *pName, float Value);
	bool Get(int Index, float *pValue) const;
	bool Get(const char *pName, float *pValue) const;
	const char *GetName(int Index) const { return ms_apNames[Index]; }
	int PossibleTunings(const char *pStr, IConsole::FPossibleCallback pfnCallback = IConsole::EmptyPossibleCommandCallback, void *pUser = 0);
};

inline void StrToInts(int *pInts, int Num, const char *pStr)
{
	int Index = 0;
	while(Num)
	{
		char aBuf[4] = {0, 0, 0, 0};
		for(int c = 0; c < 4 && pStr[Index]; c++, Index++)
			aBuf[c] = pStr[Index];
		*pInts = ((aBuf[0] + 128) << 24) | ((aBuf[1] + 128) << 16) | ((aBuf[2] + 128) << 8) | (aBuf[3] + 128);
		pInts++;
		Num--;
	}

	// null terminate
	pInts[-1] &= 0xffffff00;
}

inline void IntsToStr(const int *pInts, int Num, char *pStr)
{
	while(Num)
	{
		pStr[0] = (((*pInts) >> 24) & 0xff) - 128;
		pStr[1] = (((*pInts) >> 16) & 0xff) - 128;
		pStr[2] = (((*pInts) >> 8) & 0xff) - 128;
		pStr[3] = ((*pInts) & 0xff) - 128;
		pStr += 4;
		pInts++;
		Num--;
	}

	// null terminate
	pStr[-1] = 0;
}

inline vec2 CalcPos(vec2 Pos, vec2 Velocity, float Curvature, float Speed, float Time)
{
	vec2 n;
	Time *= Speed;
	n.x = Pos.x + Velocity.x * Time;
	n.y = Pos.y + Velocity.y * Time + Curvature / 10000 * (Time * Time);
	return n;
}

template<typename T>
inline T SaturatedAdd(T Min, T Max, T Current, T Modifier)
{
	if(Modifier < 0)
	{
		if(Current < Min)
			return Current;
		Current += Modifier;
		if(Current < Min)
			Current = Min;
		return Current;
	}
	else
	{
		if(Current > Max)
			return Current;
		Current += Modifier;
		if(Current > Max)
			Current = Max;
		return Current;
	}
}

float VelocityRamp(float Value, float Start, float Range, float Curvature);

// hooking stuff
enum
{
	HOOK_RETRACTED = -1,
	HOOK_IDLE = 0,
	HOOK_RETRACT_START = 1,
	HOOK_RETRACT_END = 3,
	HOOK_FLYING,
	HOOK_GRABBED,
};

class CWorldCore
{
public:
	// Bots use the sparse [MAX_CLIENTS, MAX_TEES) range and ids such as
	// m_HookedPlayer arrive straight from the snapshot, so characters are kept
	// in a container covering the whole id space.
	enum
	{
		CHARACTER_PART_SIZE = 1024,
		CHARACTER_PARTS = 64,
	};
	static_assert(CHARACTER_PARTS * CHARACTER_PART_SIZE >= MAX_TEES, "character storage must cover the whole TeeID space");

	CTuningParams m_Tuning;
	// The world only borrows the character cores.
	partial_array<class CCharacterCore *, CHARACTER_PARTS, CHARACTER_PART_SIZE, allocator_non_owning<class CCharacterCore *>> m_apCharacters;

	// TeeIDs currently bound in m_apCharacters, so the physics loops cost the
	// number of characters rather than the size of the id space.
	array<int> m_aCharacterIDs;

	// Returns the character core for a TeeID, or 0 when there is none.
	class CCharacterCore *GetCharacter(int TeeID) const
	{
		class CCharacterCore *const *ppChar = m_apCharacters.get(TeeID);
		return ppChar ? *ppChar : 0;
	}

	// Binds a character core to a TeeID, or unbinds it when pCharacter is 0.
	void SetCharacter(int TeeID, class CCharacterCore *pCharacter)
	{
		if(TeeID < 0 || TeeID >= MAX_TEES)
			return;

		const bool WasBound = GetCharacter(TeeID) != 0;
		if(pCharacter)
		{
			m_apCharacters[TeeID] = pCharacter;
			if(!WasBound)
				m_aCharacterIDs.add(TeeID);
		}
		else
		{
			if(WasBound)
			{
				// Drop the id from the iteration list. The slot keeps its part
				// allocated, because other ids in that part may still be used.
				for(int i = 0; i < m_aCharacterIDs.size(); i++)
				{
					if(m_aCharacterIDs[i] == TeeID)
					{
						m_aCharacterIDs.remove_index(i);
						break;
					}
				}
				m_apCharacters[TeeID] = 0;
			}
		}
	}

	// Removes every binding, so the world can be reused for a new prediction.
	void ClearCharacters()
	{
		for(int i = 0; i < m_aCharacterIDs.size(); i++)
			m_apCharacters[m_aCharacterIDs[i]] = 0;
		m_aCharacterIDs.clear_size();
	}
};

class CCharacterCore
{
	CWorldCore *m_pWorld;
	CCollision *m_pCollision;

public:
	static const float PHYS_SIZE;
	vec2 m_Pos;
	vec2 m_Vel;

	vec2 m_HookDragVel;

	vec2 m_HookPos;
	vec2 m_HookDir;
	int m_HookTick;
	int m_HookState;
	int m_HookedPlayer;

	int m_Jumped;

	int m_Direction;
	int m_Angle;

	bool m_Death;

	CNetObj_PlayerInput m_Input;

	int m_TriggeredEvents;

	void Init(CWorldCore *pWorld, CCollision *pCollision);
	void Reset();
	void Tick(bool UseInput);
	void Move();

	void AddDragVelocity();
	void ResetDragVelocity();

	void Read(const CNetObj_CharacterCore *pObjCore);
	void Write(CNetObj_CharacterCore *pObjCore) const;
	void Quantize();
};

#endif
