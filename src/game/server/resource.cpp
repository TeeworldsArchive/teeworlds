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

CServerResManager::CServerResource *CServerResManager::FindResource(Uuid ResourceID)
{
	for(int i = 0; i < m_lResources.size(); i++)
    {
        if(m_lResources[i].m_Uuid == ResourceID)
            return &m_lResources[i];
    }
    return nullptr;
}

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
    m_ChunksPerRequest = Config()->m_SvResDownloadSpeed;
}

void CServerResManager::SendResourceData(int ClientID, const Uuid RequestUuid)
{
    int ChunkSize = CResource::CHUNK_SIZE;
    CServerResource *pTarget = FindResource(RequestUuid);
    if(!pTarget)
        return;

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

		CNetMsg_Sv_CustomResourceData Msg;
		Msg.m_Uuid = &pTarget->m_Uuid;
        Msg.m_ChunkIndex = Chunk;
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
    m_aResourceSendIndex[ClientID] = 0;
}

void CServerResManager::Clear()
{
    for(int i = 0; i < m_lResources.size(); i++)
    {
        if(m_lResources[i].m_pData)
            mem_free(m_lResources[i].m_pData);
        m_lResources[i].m_pData = 0;
    }
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        m_aResourceSendIndex[i] = -1;
    }
}

void CServerResManager::TrySendResourceInfo(int ClientID)
{
    if(!GameServer()->m_apPlayers[ClientID])
        return;

    int Index = m_aResourceSendIndex[ClientID];
    if(Index < 0 || Index >= m_lResources.size())
        return;
    CNetMsg_Sv_CustomResource Resource;
    Resource.m_Uuid = &m_lResources[Index].m_Uuid;
    Resource.m_Type = m_lResources[Index].m_Type;
    Resource.m_Name = m_lResources[Index].m_aName;
    Resource.m_Crc = m_lResources[Index].m_Crc;
    Resource.m_Sha256 = &m_lResources[Index].m_Sha256;
    Resource.m_Size = m_lResources[Index].m_DataSize;
    Resource.m_ChunkPerRequest = m_ChunksPerRequest;
    Server()->SendPackMsg(&Resource, MSGFLAG_VITAL | MSGFLAG_FLUSH | MSGFLAG_NORECORD, ClientID);

    m_lResources[Index].m_aDownloadChunks[ClientID] = 0;
    m_aResourceSendIndex[ClientID]++;
}
