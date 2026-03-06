#include <base/hash.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <generated/client_data.h>

#include "resource.h"
#include "sounds.h"

static void FormatResourcePath(char *pBuffer, int BufferSize, const char *pName, bool Temp, const SHA256_DIGEST *pSha256, const unsigned int *pCrc)
{
    char aSha256[SHA256_MAXSTRSIZE];
    sha256_str(*pSha256, aSha256, sizeof(aSha256));
    if(Temp)
        str_format(pBuffer, BufferSize, "downloadedres/%s_%08x%s.twres.%d.tmp", pName, *pCrc, aSha256, pid());
    else
        str_format(pBuffer, BufferSize, "downloadedres/%s_%08x%s.twres", pName, *pCrc, aSha256);
}

CClientResManager::CClientResManager()
{
    m_lResources.clear();
}

void CClientResManager::RequestDownload(const Uuid *pRequest)
{
    if(!pRequest)
        return;

    CNetMsg_Cl_ReqeustCustomResource Msg;
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

void CClientResManager::RenderImageEntity(const CNetObj_CustomImageEntity *pPrev, const CNetObj_CustomImageEntity *pCur)
{
    Uuid TextureID;
    mem_copy(&TextureID, pCur->m_Uuid, sizeof(Uuid));
    IGraphics::CTextureHandle Texture = GetResourceTexture(TextureID);
    Texture = Texture.IsValid() ? Texture : g_pData->m_aImages[IMAGE_DEADTEE].m_Id; // fallback
    vec2 Pos = mix(vec2(pPrev->m_X, pPrev->m_Y), vec2(pCur->m_X, pCur->m_Y), Client()->IntraGameTick());
	vec2 Size = mix(vec2(pPrev->m_Width, pPrev->m_Height), vec2(pCur->m_Width, pCur->m_Height), Client()->IntraGameTick());
    float Angle = mix(pPrev->m_Angle / 256.0f, pCur->m_Angle / 256.0f, Client()->IntraGameTick());

	Graphics()->BlendNormal();
    Graphics()->TextureSet(Texture);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(Angle);
	IGraphics::CQuadItem QuadItem(Pos.x, Pos.y, Size.x, Size.y);
	Graphics()->QuadsDraw(&QuadItem, 1);
	Graphics()->QuadsEnd();
}

void CClientResManager::RenderSoundEntity(const CNetObj_CustomSoundEntity *pPrev, const CNetObj_CustomSoundEntity *pCur, int ItemID)
{
    Uuid SoundID;
    mem_copy(&SoundID, pCur->m_Uuid, sizeof(Uuid));
    ISound::CSampleHandle Sample = GetResourceSample(SoundID);
    if(!Sample.IsValid())
        return;
    // search and mark the entity as active
    CSoundEntity *pEntity = 0;
    for(int i = 0; i < m_lSoundEntities.size(); i++)
    {
        if(ItemID == m_lSoundEntities[i].m_SnapshotID)
        {
            pEntity = &m_lSoundEntities[i];
            pEntity->m_Active = true;
            if(mem_comp(pPrev->m_Uuid, pCur->m_Uuid, sizeof(Uuid)) != 0)
            {
                Sound()->StopVoice(pEntity->m_Voice);
                pEntity->m_Voice = Sound()->PlayAt(CSounds::CHN_WORLD, Sample, 1.0f, ISound::FLAG_LOOP, 0, 0);
            }
            break;
        }
    }

    int Volume = mix(pPrev->m_Vol, pCur->m_Vol, Client()->IntraGameTick());
	float Distance = mix(pPrev->m_Distance, pCur->m_Distance, Client()->IntraGameTick()) / 256.0f;
    vec2 Pos = mix(vec2(pPrev->m_X, pPrev->m_Y), vec2(pCur->m_X, pCur->m_Y), Client()->IntraGameTick());
    if(!pEntity)
    {
        CSoundEntity &NewEntity = m_lSoundEntities.emplace();
        NewEntity.m_Active = true;
        NewEntity.m_SnapshotID = ItemID;
        NewEntity.m_Voice = Sound()->PlayAt(CSounds::CHN_WORLD, Sample, 1.0f, ISound::FLAG_LOOP, 0, 0);
        pEntity = &NewEntity;
    }

    Sound()->SetVoiceVolume(pEntity->m_Voice, Volume / 256.0f);
    Sound()->SetVoiceCircle(pEntity->m_Voice, Distance);
    Sound()->SetVoicePos(pEntity->m_Voice, Pos.x, Pos.y);

    static float s_Time = 0.0f;
    if(m_pClient->m_Snap.m_pGameData)
    {
        s_Time = mix((Client()->PrevGameTick() - m_pClient->m_Snap.m_pGameData->m_GameStartTick) / (float) Client()->GameTickSpeed(),
            (Client()->GameTick() - m_pClient->m_Snap.m_pGameData->m_GameStartTick) / (float) Client()->GameTickSpeed(),
            Client()->IntraGameTick());
    }
    float Offset = maximum(s_Time - pCur->m_StartTick / (float) Client()->GameTickSpeed(),  0.0f);
    Sound()->SetVoiceTimeOffset(pEntity->m_Voice, Offset);
}

void CClientResManager::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

    for(int i = 0; i < m_lSoundEntities.size(); i++)
    {
        m_lSoundEntities[i].m_Active = false;
    }

	int Num = Client()->SnapNumItems(IClient::SNAP_CURRENT);
	for(int i = 0; i < Num; i++)
	{
		IClient::CSnapItem Item;
		const void *pData = Client()->SnapGetItem(IClient::SNAP_CURRENT, i, &Item);

		if(Item.m_Type == NETOBJTYPE_CUSTOMIMAGEENTITY)
		{
			const void *pPrev = Client()->SnapFindItem(IClient::SNAP_PREV, Item.m_Type, Item.m_ID);
			if(pPrev)
			    RenderImageEntity((const CNetObj_CustomImageEntity *) pPrev, (const CNetObj_CustomImageEntity *) pData);
		}
        else if(Config()->m_SndEnable && Item.m_Type == NETOBJTYPE_CUSTOMSOUNDENTITY)
        {
            const void *pPrev = Client()->SnapFindItem(IClient::SNAP_PREV, Item.m_Type, Item.m_ID);
            if(pPrev)
                RenderSoundEntity((const CNetObj_CustomSoundEntity *) pPrev, (const CNetObj_CustomSoundEntity *) pData, Item.m_ID);
        }
	}

    for(int i = 0; i < m_lSoundEntities.size(); i++)
    {
        if(!m_lSoundEntities[i].m_Active)
        {
            Sound()->StopVoice(m_lSoundEntities[i].m_Voice);
            m_lSoundEntities.remove_index_fast(i);
        }
    }
}

void CClientResManager::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_CUSTOMRESOURCE)
	{
		CNetMsg_Sv_CustomResource *pMsg = (CNetMsg_Sv_CustomResource *) pRawMsg;
        
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

        if(FindResource(*static_cast<const Uuid *>(pMsg->m_Uuid))) // there couldn't be uuid collision, if that happened, then the server-side resource name must be wrong.
        {
            Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "resource", "invalid resource uuid");
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
        Resource.m_Type = pMsg->m_Type;
        Resource.m_ChunkPerRequest = pMsg->m_ChunkPerRequest;
        mem_copy(&Resource.m_Sha256, pMsg->m_Sha256, sizeof(SHA256_DIGEST));
        FormatResourcePath(Resource.m_aPath, sizeof(Resource.m_aPath), Resource.m_aName, false, &Resource.m_Sha256, &Resource.m_Crc);
        FormatResourcePath(Resource.m_aTempPath, sizeof(Resource.m_aTempPath), Resource.m_aName, true, &Resource.m_Sha256, &Resource.m_Crc);
        Resource.m_DownloadTemp = 0;

        int Index = m_lResources.add(Resource);
        if(!LoadResource(&m_lResources[Index]))
        {
            m_lResources[Index].m_DownloadTemp = Storage()->OpenFile(m_lResources[Index].m_aTempPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
            RequestDownload(&m_lResources[Index].m_Uuid);
        }
    }
    else if(MsgType == NETMSGTYPE_SV_CUSTOMRESOURCEDATA)
    {
		CNetMsg_Sv_CustomResourceData *pMsg = (CNetMsg_Sv_CustomResourceData *) pRawMsg;
        Uuid TargetResource = *static_cast<const Uuid *>(pMsg->m_Uuid);
        CClientResource *pResource = FindResource(TargetResource);
        if(!pResource)
            return;
        pResource->m_DownloadedSize += pMsg->m_DataSize;
        if(pResource->m_DownloadedSize > pResource->m_DataSize)
        {
            io_close(pResource->m_DownloadTemp);
            pResource->m_DownloadTemp = 0;
            Storage()->RemoveFile(pResource->m_aTempPath, IStorage::TYPE_SAVE);
            m_lResources.remove(*pResource);
            return; // invalid!
        }
        io_write(pResource->m_DownloadTemp, pMsg->m_Data, pMsg->m_DataSize);
        if(pResource->m_DownloadedSize == pResource->m_DataSize)
        {
            char aBuf[128];
            str_format(aBuf, sizeof(aBuf), Localize("Resource '%s': download complete"), pResource->m_aName);
            UI()->DoToast(aBuf);
            io_close(pResource->m_DownloadTemp);
            pResource->m_DownloadTemp = 0;

            Storage()->RemoveFile(pResource->m_aPath, IStorage::TYPE_SAVE);
            Storage()->RenameFile(pResource->m_aTempPath, pResource->m_aPath, IStorage::TYPE_SAVE);

            if(!LoadResource(pResource))
            {
                Storage()->RemoveFile(pResource->m_aPath, IStorage::TYPE_SAVE);
                m_lResources.remove(*pResource);
                return; // invalid!
            }
        }
        else if((pMsg->m_ChunkIndex + 1) % pResource->m_ChunkPerRequest == 0)
            RequestDownload(&TargetResource);
    }
}

void CClientResManager::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_OFFLINE)
	{
        if(OldState >= IClient::STATE_CONNECTING && NewState <= IClient::STATE_ONLINE)
        {
            for(int i = 0; i < m_lResources.size(); i++)
            {
                if(m_lResources[i].m_Sample.IsValid())
                    m_pClient->m_pSounds->UnloadSample(&m_lResources[i].m_Sample);
                if(m_lResources[i].m_Texture.IsValid())
                    Graphics()->UnloadTexture(&m_lResources[i].m_Texture);
                if(m_lResources[i].m_DownloadTemp)
                {
                    io_close(m_lResources[i].m_DownloadTemp);
                    Storage()->RemoveFile(m_lResources[i].m_aTempPath, IStorage::TYPE_SAVE);
                }
            }
            m_lResources.clear();

            for(int i = 0; i < m_lSoundEntities.size(); i++)
            {
                if(m_lSoundEntities[i].m_Active)
                    Sound()->StopVoice(m_lSoundEntities[i].m_Voice);
            }
            m_lSoundEntities.clear();
        }
    }
}

CClientResManager::CClientResource *CClientResManager::FindResource(Uuid ResourceID)
{
	for(int i = 0; i < m_lResources.size(); i++)
    {
        if(m_lResources[i].m_Uuid == ResourceID)
            return &m_lResources[i];
    }
    return nullptr;
}

ISound::CSampleHandle CClientResManager::GetResourceSample(Uuid ResID)
{
    CClientResource *pResource = FindResource(ResID);
    if(pResource)
	    return pResource->m_Sample;
    return ISound::CSampleHandle();
}

IGraphics::CTextureHandle CClientResManager::GetResourceTexture(Uuid ResID)
{
    CClientResource *pResource = FindResource(ResID);
    if(pResource)
	    return pResource->m_Texture;
    return IGraphics::CTextureHandle();
}
