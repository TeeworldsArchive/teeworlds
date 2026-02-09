#ifndef GAME_SERVER_RESOURCE_H
#define GAME_SERVER_RESOURCE_H

#include <base/tl/sorted_array.h>
#include <game/resource.h>

class CServerResManager
{
    class CGameContext *m_pGameContext;
    class CGameContext *GameServer() const { return m_pGameContext; }
    class IConsole *Console();
    class IServer *Server();
    class IStorage *Storage();
    class CConfig *Config();

    struct CServerResource : public CResource
    {
        int m_aDownloadChunks[MAX_CLIENTS];
    };

    sorted_array<CServerResource> m_lResources;
public:
    CServerResManager();
    ~CServerResManager();

    void Init(class CGameContext *pGameContext);
    void AddResource(const char *pPath, const char *pName, const Uuid ResourceID);
    void SendResourceData(int ClientID, const Uuid RequestUuid);
    void OnClientEnter(int ClientID);
    void Clear();

    bool IsResourceSound(Uuid ResID);
    bool IsResourceImage(Uuid ResID);
};

#endif // GAME_SERVER_RESOURCE_H