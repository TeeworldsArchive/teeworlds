#include "protocol_ex.h"

#include "config.h"
#include "protocol.h"
#include "uuid_manager.h"

void RegisterUuids(class CUuidManager *pManager)
{
#define UUID(id, name) pManager->RegisterName(id, name);
#include "protocol_ex_msgs.h"
#undef UUID
}

int UnpackMessageID(int *pID, bool *pSys, Uuid *pUuid, CUnpacker *pUnpacker, CMsgPacker *pPacker)
{
	*pID = 0;
	*pSys = false;
	mem_zero(pUuid, sizeof(*pUuid));

	int MsgID = pUnpacker->GetInt();

	if(pUnpacker->Error())
	{
		return UNPACKMESSAGE_ERROR;
	}

	*pID = MsgID >> 1;
	*pSys = MsgID & 1;

	if(*pID < 0 || *pID >= OFFSET_UUID)
	{
		return UNPACKMESSAGE_ERROR;
	}

	if(*pID != 0) // NETMSG_EX, NETMSGTYPE_EX
	{
		return UNPACKMESSAGE_OK;
	}

	*pID = g_UuidManager.UnpackUuid(pUnpacker, pUuid);

	if(*pID == UUID_INVALID || *pID == UUID_UNKNOWN)
	{
		return UNPACKMESSAGE_ERROR;
	}

	return UNPACKMESSAGE_OK;
}
