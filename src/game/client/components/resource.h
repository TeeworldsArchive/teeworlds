#ifndef GAME_CLIENT_COMPONENTS_RESOURCE_H
#define GAME_CLIENT_COMPONENTS_RESOURCE_H

#include <base/tl/array.h>
#include <base/uuid.h>
#include <engine/graphics.h>
#include <engine/sound.h>
#include <game/client/component.h>
#include <game/resource.h>

class CClientResManager : public CComponent
{
    struct CClientResource : public CResource
    {
        char m_aPath[IO_MAX_PATH_LENGTH];
        char m_aTempPath[IO_MAX_PATH_LENGTH];
        int m_DownloadedSize;
        int m_ChunkPerRequest;
        ISound::CSampleHandle m_Sample;
        IGraphics::CTextureHandle m_Texture;
        IOHANDLE m_DownloadTemp;
    };

    struct CSoundEntity
    {
        int m_Voice;
        int m_SnapshotID;
        bool m_Active;
    };
    array<CSoundEntity> m_lSoundEntities;
    array<CClientResource> m_lResources;
    void RequestDownload(const Uuid *pRequest);
    bool LoadResource(CClientResource *pResource);

    void RenderImageEntity(const CNetObj_CustomImageEntity *pPrev, const CNetObj_CustomImageEntity *pCur);
    void RenderSoundEntity(const CNetObj_CustomSoundEntity *pPrev, const CNetObj_CustomSoundEntity *pCur, int ItemID);
public:
    CClientResManager();
    virtual void OnRender();
	virtual void OnMessage(int MsgType, void *pRawMsg);
	virtual void OnStateChange(int NewState, int OldState);
    CClientResource *FindResource(Uuid ResourceID);

    ISound::CSampleHandle GetResourceSample(Uuid ResID);
    IGraphics::CTextureHandle GetResourceTexture(Uuid ResID);
};

#endif // GAME_CLIENT_COMPONENTS_RESOURCE_H