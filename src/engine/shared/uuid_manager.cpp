/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/packer.h>

#include "protocol_ex.h"
#include "uuid_manager.h"

#include <engine/uuid.h>

static CUuidManager CreateGlobalUuidManager()
{
	CUuidManager Manager;
	RegisterUuids(&Manager);
	RegisterGameUuids(&Manager);
	return Manager;
}

CUuidManager g_UuidManager = CreateGlobalUuidManager();

static int GetIndex(int ID)
{
	return ID - OFFSET_UUID;
}

static int GetID(int Index)
{
	return Index + OFFSET_UUID;
}

void CUuidManager::RegisterName(int ID, const char *pName)
{
	int Index = GetIndex(ID);
	dbg_assert(Index == m_aNames.size(), "names must be registered with increasing ID");
	CName Name;
	Name.m_pName = pName;
	Name.m_Uuid = calculate_uuid(pName);
	dbg_assert(LookupUuid(Name.m_Uuid) == -1, "duplicate uuid");

	m_aNames.add(Name);
}

Uuid CUuidManager::GetUuid(int ID) const
{
	return m_aNames[GetIndex(ID)].m_Uuid;
}

const char *CUuidManager::GetName(int ID) const
{
	return m_aNames[GetIndex(ID)].m_pName;
}

int CUuidManager::LookupUuid(Uuid Uuid) const
{
	for(int i = 0; i < m_aNames.size(); i++)
	{
		if(Uuid == m_aNames[i].m_Uuid)
		{
			return GetID(i);
		}
	}
	return UUID_UNKNOWN;
}

int CUuidManager::UnpackUuid(CUnpacker *pUnpacker) const
{
	Uuid Temp;
	return UnpackUuid(pUnpacker, &Temp);
}

int CUuidManager::UnpackUuid(CUnpacker *pUnpacker, Uuid *pOut) const
{
	const Uuid *pUuid = (const Uuid *) pUnpacker->GetRaw(sizeof(*pUuid));
	if(pUuid == NULL)
	{
		return UUID_INVALID;
	}
	*pOut = *pUuid;
	return LookupUuid(*pUuid);
}

void CUuidManager::PackUuid(int ID, CPacker *pPacker) const
{
	Uuid Uuid = GetUuid(ID);
	pPacker->AddRaw(&Uuid, sizeof(Uuid));
}

void CUuidManager::DebugDump() const
{
	for(int i = 0; i < m_aNames.size(); i++)
	{
		char aBuf[UUID_MAXSTRSIZE];
		format_uuid(m_aNames[i].m_Uuid, aBuf, sizeof(aBuf));
		dbg_msg("uuid", "%s %s", aBuf, m_aNames[i].m_pName);
	}
}
