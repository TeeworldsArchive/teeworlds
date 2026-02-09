#include <engine/console.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include "gamecontext.h"
#include "resource.h"

IConsole *CServerResManager::Console() { return m_pGameContext->Console(); }
IServer *CServerResManager::Server() { return m_pGameContext->Server(); }
IStorage *CServerResManager::Storage() { return m_pGameContext->Storage(); }
CConfig *CServerResManager::Config() { return m_pGameContext->Config(); }

CServerResManager::CServerResManager()
{
}

CServerResManager::~CServerResManager()
{
    Clear();
}

void CServerResManager::Init(CGameContext *pGameContext)
{
    m_pGameContext = pGameContext;
}

void CServerResManager::SendResourceData(int ClientID, const Uuid RequestUuid)
{
	CServerResource Res;
	Res.m_Uuid = RequestUuid;
	sorted_array<CServerResource>::range r = ::find_binary(m_lResources.all(), Res);
	if(r.empty() || r.size() > 1) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
		return;

    int ChunkSize = CResource::CHUNK_SIZE;
    CServerResource *pTarget = &r.front();

    // send resource chunks, copied from map download
    for(int i = 0; i < Config()->m_SvResDownloadSpeed && pTarget->m_aDownloadChunks[ClientID] >= 0; ++i)
    {
        int Chunk = pTarget->m_aDownloadChunks[ClientID];
        int Offset = Chunk * ChunkSize;

        // check for last part
        if(Offset + ChunkSize >= pTarget->m_DataSize)
        {
            ChunkSize = pTarget->m_DataSize - Offset;
            pTarget->m_aDownloadChunks[ClientID] = -1;
        }
        else
            pTarget->m_aDownloadChunks[ClientID]++;

		CNetMsg_Sv_CustomResData Msg;
		Msg.m_Uuid = &pTarget->m_Uuid;
		Msg.m_Data = &pTarget->m_pData[Offset];
		Msg.m_DataSize = ChunkSize;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH | MSGFLAG_NORECORD, ClientID);

        if(Config()->m_Debug)
        {
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
            Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "resource", aBuf);
        }
    }
}

void CServerResManager::AddResource(const char *pPath, const char *pName, const Uuid ResourceID)
{
    char aBuf[256];
    if(!str_endswith(pPath, ".png") && !str_endswith(pPath, ".opus"))
    {
        str_format(aBuf, sizeof(aBuf), "failed to load resource with wrong extension '%s'(%s)", pName, pPath);
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "resource", aBuf);
        return;
    }

	IOHANDLE File = Storage()->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
    {
        str_format(aBuf, sizeof(aBuf), "failed to load resource '%s'(%s)", pName, pPath);
        Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "resource", aBuf);
        return;
    }
    CServerResource NewRes;
    unsigned int DataSize;
    (void) Storage()->GetHashAndSize(pPath, IStorage::TYPE_ALL, &NewRes.m_Sha256, &NewRes.m_Crc, &DataSize);
    str_copy(NewRes.m_aName, pName, sizeof(NewRes.m_aName));
    NewRes.m_DataSize = static_cast<int>(DataSize);
    NewRes.m_Type = str_endswith(pPath, ".png") ? RESOURCE_IMAGE : RESOURCE_SOUND;
    NewRes.m_Uuid = ResourceID;
    NewRes.m_pData = (unsigned char *) mem_alloc(NewRes.m_DataSize);
    io_read(File, NewRes.m_pData, NewRes.m_DataSize);
    io_close(File);
    m_lResources.add(NewRes);

    str_format(aBuf, sizeof(aBuf), "loaded resource '%s'(%s)", pName, pPath);
    Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "resource", aBuf);
}

void CServerResManager::OnClientEnter(int ClientID)
{
	if(Server()->GetClientVersion(ClientID) < 0x0706)
		return;
	for(int i = 0; i < m_lResources.size(); i++)
	{
		CNetMsg_Sv_CustomRes Resource;
		Resource.m_Uuid = &m_lResources[i].m_Uuid;
		Resource.m_Type = m_lResources[i].m_Type;
		Resource.m_Name = m_lResources[i].m_aName;
		Resource.m_Crc = m_lResources[i].m_Crc;
		Resource.m_Sha256 = &m_lResources[i].m_Sha256;
		Resource.m_Size = m_lResources[i].m_DataSize;
		Server()->SendPackMsg(&Resource, MSGFLAG_VITAL | MSGFLAG_FLUSH | MSGFLAG_NORECORD, ClientID);

		m_lResources[i].m_aDownloadChunks[ClientID] = 0;
	}
}

void CServerResManager::Clear()
{
    for(int i = 0; i < m_lResources.size(); i++)
    {
        if(m_lResources[i].m_pData)
            mem_free(m_lResources[i].m_pData);
        m_lResources[i].m_pData = 0;
    }
}

bool CServerResManager::IsResourceSound(Uuid ResID)
{
	CServerResource Res;
	Res.m_Uuid = ResID;
	sorted_array<CServerResource>::range r = ::find_binary(m_lResources.all(), Res);
	if(r.empty() || r.size() > 1) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
		return false;
	return r.front().m_Type == RESOURCE_SOUND;
}

bool CServerResManager::IsResourceImage(Uuid ResID)
{
	CServerResource Res;
	Res.m_Uuid = ResID;
	sorted_array<CServerResource>::range r = ::find_binary(m_lResources.all(), Res);
	if(r.empty() || r.size() > 1) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
		return false;
	return r.front().m_Type == RESOURCE_IMAGE;
}
