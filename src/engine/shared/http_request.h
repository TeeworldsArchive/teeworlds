#ifndef ENGINE_SHARED_HTTP_REQUEST_H
#define ENGINE_SHARED_HTTP_REQUEST_H

#include <base/tl/array.h>
#include <base/tl/stream.h>
#include "jobs.h"

extern "C"
{
    struct curl_slist;
}

enum
{
    HTTP_IPRESOLVE_BOTH = 0,
    HTTP_IPRESOLVE_IPV4ONLY,
    HTTP_IPRESOLVE_IPV6ONLY,
};

class CHttpRequest
{
    char m_aRequest[16];
    char m_aUrl[256];
    long m_TimeoutSeconds;

    int m_IPResolve;

    array<unsigned char> m_PostData;
    array<unsigned char> m_ReceivedData;

    int m_ResponseCode;

    CJob m_Job;
    void *m_pHandle;
    curl_slist *m_pHeaderList;

    bool m_IsChecked;

    static size_t WriteCallback(char *pData, size_t Size, size_t Number, void *pUser);
    static int Run(void *pUser);
public:
    CHttpRequest(const char *pRequest, const char *pUrl, long TimeoutSeconds, int IPResolve = HTTP_IPRESOLVE_BOTH);
    void PostData(const unsigned char *pPost, int Size);
    void PostJson(const char *pJson);
    void AddHeader(const char *pHeader);
    void StartRun(class IEngine *pEngine);

    void MarkAsChecked() { m_IsChecked = true; }

    bool IsChecked() const { return m_IsChecked; }
    int Status() const { return m_Job.Status(); }
    int Result() const { return m_Job.Result(); }
    int ResponseCode() const { return m_ResponseCode; }
    const unsigned char *ReceivedData() const { return m_ReceivedData.base_ptr(); }
    int ReceivedDataSize() const { return m_ReceivedData.size(); }
};

class CCurlInit
{
    bool m_Failed;
public:
    CCurlInit();
    ~CCurlInit();

    bool IsFailed() const { return m_Failed; }
};

void EscapeUrl(char *pBuf, int Size, const char *pStr);

#endif // ENGINE_SHARED_HTTP_REQUEST_H
