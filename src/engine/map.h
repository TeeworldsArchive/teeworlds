/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_MAP_H
#define ENGINE_MAP_H

#include <base/hash.h>
#include "kernel.h"

class IMap : public IInterface
{
	MACRO_INTERFACE("map", 0)
public:
	// every map loaded at the same time is a slot of the one engine map object
	virtual int GetDataSize(int MapIndex, int Index) = 0;
	virtual void *GetData(int MapIndex, int Index) = 0;
	virtual void *GetDataSwapped(int MapIndex, int Index) = 0;
	virtual void UnloadData(int MapIndex, int Index) = 0;
	virtual void *GetItem(int MapIndex, int Index, int *Type, int *pID) = 0;
	virtual void GetType(int MapIndex, int Type, int *pStart, int *pNum) = 0;
	virtual void *FindItem(int MapIndex, int Type, int ID) = 0;
	virtual int NumItems(int MapIndex) = 0;
	// unique per successful load, tags map resources
	virtual unsigned Generation(int MapIndex) = 0;
};

class IEngineMap : public IMap
{
	MACRO_INTERFACE("enginemap", 0)
public:
	enum
	{
		SLOT_MAIN = 0,
		SLOT_MENU_FIRST,
		SLOT_MENU_B,
		SLOT_FADE,
		MAX_MAPS,
	};

	virtual bool Load(const char *pMapName, class IStorage *pStorage = 0, int MapIndex = SLOT_MAIN) = 0;
	virtual bool IsLoaded(int MapIndex = SLOT_MAIN) = 0;
	virtual void Unload(int MapIndex = SLOT_MAIN) = 0;
	// exchange two loaded maps in O(1)
	virtual void SwapSlots(int MapIndexA, int MapIndexB) = 0;
	virtual SHA256_DIGEST Sha256(int MapIndex = SLOT_MAIN) = 0;
	virtual unsigned Crc(int MapIndex = SLOT_MAIN) = 0;
};

extern IEngineMap *CreateEngineMap();

#endif
