#include <base/hash.h>
#include <engine/shared/datafile.h>

#include "resource.h"
#include "sounds.h"

static void FormatResourcePath(char *pBuffer, int BufferSize, const char *pName, bool Temp, const SHA256_DIGEST *pSha256, const unsigned int *pCrc)
{
    char aSha256[SHA256_MAXSTRSIZE];
    sha256_str(*pSha256, aSha256, sizeof(aSha256));
    if(Temp)
        str_format(pBuffer, BufferSize, "downloadedres/%s_%08x%s.res.%d.tmp", pName, *pCrc, aSha256, pid());
    else
        str_format(pBuffer, BufferSize, "downloadedres/%s_%08x%s.res", pName, *pCrc, aSha256);
}

CClientResManager::CClientResManager()
{
    m_lResources.clear();
}

void CClientResManager::RequestDownload(const Uuid *pRequest)
{
    CNetMsg_Cl_ReqeustCustomRes Msg;
    Msg.m_Uuid = pRequest;
    Client()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH | MSGFLAG_NORECORD);
}

bool CClientResManager::LoadResource(CClientResource *pResource)
{
    IOHANDLE File = Storage()->OpenFile(pResource->m_aPath, IOFLAG_READ, IStorage::TYPE_SAVE, 0, 0, CDataFileReader::CheckSha256, &pResource->m_Sha256);
    if(!File)
        return false;
    pResource->m_pData = (unsigned char *) mem_alloc(pResource->m_DataSize);
    io_read(File, pResource->m_pData, pResource->m_DataSize);
    io_close(File);
    if(pResource->m_Type == RESOURCE_SOUND)
    {
        pResource->m_Sample = m_pClient->m_pSounds->LoadSampleMemory(pResource->m_aPath, pResource->m_pData, pResource->m_DataSize);
    }
    else if(pResource->m_Type == RESOURCE_IMAGE)
    {
        pResource->m_Texture = Graphics()->LoadTexture(pResource->m_aPath, IStorage::TYPE_SAVE, CImageInfo::FORMAT_AUTO, 0);
    }
    mem_free(pResource->m_pData);
    pResource->m_pData = 0;
    return true;
}

void CClientResManager::OnMapLoad()
{
    for(int i = 0; i < m_lResources.size(); i++)
    {
        if(m_lResources[i].m_Sample.IsValid())
            m_pClient->m_pSounds->UnloadSample(&m_lResources[i].m_Sample);
        if(m_lResources[i].m_Texture.IsValid())
            Graphics()->UnloadTexture(&m_lResources[i].m_Texture);
    }
    m_lResources.clear();
}

void CClientResManager::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_CUSTOMRES)
	{
		CNetMsg_Sv_CustomRes *pMsg = (CNetMsg_Sv_CustomRes *) pRawMsg;
        
        // protect the player from nasty map names
        for(int i = 0; pMsg->m_Name[i]; i++)
        {
            if(pMsg->m_Name[i] == '/' || pMsg->m_Name[i] == '\\')
            {
                char aBuf[IO_MAX_PATH_LENGTH + 64];
                str_format(aBuf, sizeof(aBuf), "got strange path from custom resource '%s'", pMsg->m_Name);
                Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "resource", aBuf);
                return;
            }
        }

        if(pMsg->m_Size <= 0)
        {
            Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "resource", "invalid resource size");
            return;
        }
        CClientResource Resource;
        str_copy(Resource.m_aName, pMsg->m_Name, sizeof(Resource.m_aName));
        Resource.m_Crc = pMsg->m_Crc;
        Resource.m_DataSize = pMsg->m_Size;
        Resource.m_Uuid = *static_cast<const Uuid *>(pMsg->m_Uuid);
        Resource.m_pData = 0;
        Resource.m_DownloadedSize = 0;
        Resource.m_Sample = ISound::CSampleHandle();
        Resource.m_Texture = IGraphics::CTextureHandle();
        mem_copy(&Resource.m_Sha256, pMsg->m_Sha256, sizeof(SHA256_DIGEST));
        FormatResourcePath(Resource.m_aPath, sizeof(Resource.m_aPath), Resource.m_aName, false, &Resource.m_Sha256, &Resource.m_Crc);
        FormatResourcePath(Resource.m_aTempPath, sizeof(Resource.m_aTempPath), Resource.m_aName, true, &Resource.m_Sha256, &Resource.m_Crc);

        int Index = m_lResources.add(Resource);
        if(!LoadResource(&m_lResources[Index]))
        {
            m_lResources[Index].m_DownloadTemp = Storage()->OpenFile(m_lResources[Index].m_aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
            RequestDownload(&m_lResources[Index].m_Uuid);
        }
    }
    else if(MsgType == NETMSGTYPE_SV_CUSTOMRESDATA)
    {
		CNetMsg_Sv_CustomResData *pMsg = (CNetMsg_Sv_CustomResData *) pRawMsg;
        Uuid TargetResource = *static_cast<const Uuid *>(pMsg->m_Uuid);

        CClientResource TargetRes;
        TargetRes.m_Uuid = TargetResource;
        sorted_array<CClientResource>::range r = ::find_binary(m_lResources.all(), TargetRes);
        if(r.empty() || r.size() > 1) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
            return;

        CClientResource &Resource = r.front();
        Resource.m_DownloadedSize += pMsg->m_DataSize;
        if(Resource.m_DownloadedSize > Resource.m_DataSize)
        {
            io_close(Resource.m_DownloadTemp);
            Storage()->RemoveFile(Resource.m_aTempPath, IStorage::TYPE_SAVE);
            m_lResources.remove(Resource);
            return; // invalid!
        }
        io_write(Resource.m_DownloadTemp, pMsg->m_Data, pMsg->m_DataSize);
        if(Resource.m_DownloadedSize == Resource.m_DataSize)
        {
            char aBuf[128];
            str_format(aBuf, sizeof(aBuf), Localize("Resource '%s': download complete"), Resource.m_aName);
            UI()->DoToast(aBuf);
            io_close(Resource.m_DownloadTemp);

            Storage()->RemoveFile(Resource.m_aPath, IStorage::TYPE_SAVE);
            Storage()->RenameFile(Resource.m_aTempPath, Resource.m_aPath, IStorage::TYPE_SAVE);

            if(!LoadResource(&Resource))
            {
                Storage()->RemoveFile(Resource.m_aPath, IStorage::TYPE_SAVE);
                m_lResources.remove(Resource);
                return; // invalid!
            }
        }
        else
            RequestDownload(&TargetResource);
    }
}

bool CClientResManager::IsResourceSound(Uuid ResID)
{
	CClientResource Res;
	Res.m_Uuid = ResID;
	sorted_array<CClientResource>::range r = ::find_binary(m_lResources.all(), Res);
	if(r.empty() || r.size() > 1) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
		return false;
	return r.front().m_Type == RESOURCE_SOUND;
}

bool CClientResManager::IsResourceImage(Uuid ResID)
{
	CClientResource Res;
	Res.m_Uuid = ResID;
	sorted_array<CClientResource>::range r = ::find_binary(m_lResources.all(), Res);
	if(r.empty() || r.size() > 1) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
		return false;
	return r.front().m_Type == RESOURCE_IMAGE;
}

ISound::CSampleHandle CClientResManager::GetResourceSample(Uuid ResID)
{
	CClientResource Res;
	Res.m_Uuid = ResID;
	sorted_array<CClientResource>::range r = ::find_binary(m_lResources.all(), Res);
	if(r.empty() || r.size() > 1) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
		return ISound::CSampleHandle();
	return r.front().m_Sample;
}

IGraphics::CTextureHandle CClientResManager::GetResourceTexture(Uuid ResID)
{
	CClientResource Res;
	Res.m_Uuid = ResID;
	sorted_array<CClientResource>::range r = ::find_binary(m_lResources.all(), Res);
	if(r.empty() || r.size() > 1) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
		return IGraphics::CTextureHandle();
	return r.front().m_Texture;
}
