#ifndef ENGINE_SHARED_UUID_MANAGER_H
#define ENGINE_SHARED_UUID_MANAGER_H

#include <base/tl/array.h>
#include <base/uuid.h>

struct CName
{
	Uuid m_Uuid;
	const char *m_pName;
};

class CPacker;
class CUnpacker;

class CUuidManager
{
	array<CName> m_aNames;

public:
	void RegisterName(int ID, const char *pName);
	Uuid GetUuid(int ID) const;
	const char *GetName(int ID) const;
	int LookupUuid(Uuid Uuid) const;

	int UnpackUuid(CUnpacker *pUnpacker) const;
	int UnpackUuid(CUnpacker *pUnpacker, Uuid *pOut) const;
	void PackUuid(int ID, CPacker *pPacker) const;

	void DebugDump() const;
};

extern CUuidManager g_UuidManager;

#endif // ENGINE_SHARED_UUID_MANAGER_H
