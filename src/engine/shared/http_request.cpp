#include <base/system.h>
#include <engine/engine.h>
#include <game/version.h>

#include "http_request.h"

#include <curl/curl.h>

CCurlInit::CCurlInit()
{
	if(curl_global_init(CURL_GLOBAL_ALL))
	{
		dbg_msg("http", "failed to init curl");
		m_Failed = true;
	}
	else
	{
		m_Failed = false;
	}
}

CCurlInit::~CCurlInit()
{
	curl_global_cleanup();
}

void EscapeUrl(char *pBuf, int Size, const char *pStr)
{
	char *pEsc = curl_easy_escape(nullptr, pStr, 0);
	str_copy(pBuf, pEsc, Size);
	curl_free(pEsc);
}

CHttpRequest::CHttpRequest(const char *pRequest, const char *pUrl, long TimeoutSeconds, int IPResolve)
{
	str_copy(m_aRequest, pRequest, sizeof(m_aRequest));
	str_copy(m_aUrl, pUrl, sizeof(m_aUrl));

	m_pHeaderList = 0;
	m_IPResolve = IPResolve;

	m_IsChecked = false;
	m_TimeoutSeconds = TimeoutSeconds;
	m_PostData.clear();
}

void CHttpRequest::PostData(const unsigned char *pPost, int Size)
{
	memory_stream<unsigned char> Stream(&m_PostData);
	Stream.write(pPost, Size);
}

void CHttpRequest::PostJson(const char *pJson)
{
	PostData((const unsigned char *) pJson, str_length(pJson) + 1);
	AddHeader("Content-Type: application/json");
}

void CHttpRequest::AddHeader(const char *pHeader)
{
	m_pHeaderList = curl_slist_append(m_pHeaderList, pHeader);
}

// for windows build
#ifdef AddJob
#undef AddJob
#endif

void CHttpRequest::StartRun(IEngine *pEngine)
{
	pEngine->AddJob(&m_Job, CHttpRequest::Run, this);
}

size_t CHttpRequest::WriteCallback(char *pData, size_t Size, size_t Number, void *pUser)
{
	CHttpRequest *pRequest = static_cast<CHttpRequest *>(pUser);
	size_t TotalSize = Size * Number;
	memory_stream<unsigned char> Stream(&pRequest->m_ReceivedData);
	Stream.write((const unsigned char *) pData, TotalSize);
	return TotalSize;
}

int CHttpRequest::Run(void *pUser)
{
	CHttpRequest *pRequest = static_cast<CHttpRequest *>(pUser);
	pRequest->m_pHandle = curl_easy_init();
	if(!pRequest->m_pHandle)
		return -1;
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_URL, pRequest->m_aUrl);
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_CUSTOMREQUEST, pRequest->m_aRequest);
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_PROTOCOLS_STR, "https");

	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_MAXREDIRS, 5L);

	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_TIMEOUT, pRequest->m_TimeoutSeconds);

	if(pRequest->m_PostData.size() > 0)
	{
		curl_easy_setopt(pRequest->m_pHandle, CURLOPT_POSTFIELDS, (const char *) pRequest->m_PostData.base_ptr());
		curl_easy_setopt(pRequest->m_pHandle, CURLOPT_POSTFIELDSIZE, pRequest->m_PostData.size());
	}

	switch(pRequest->m_IPResolve)
	{
		case HTTP_IPRESOLVE_IPV4ONLY: curl_easy_setopt(pRequest->m_pHandle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4); break;
		case HTTP_IPRESOLVE_IPV6ONLY: curl_easy_setopt(pRequest->m_pHandle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6); break;
		default: curl_easy_setopt(pRequest->m_pHandle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER); break;
	}

	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_USERAGENT, "TeeworldsArchive " GAME_VERSION " (" CONF_PLATFORM_STRING "; " CONF_ARCH_STRING ")");
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_HTTPHEADER, pRequest->m_pHeaderList);
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_ACCEPT_ENCODING, "");

	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_WRITEFUNCTION, CHttpRequest::WriteCallback);
	curl_easy_setopt(pRequest->m_pHandle, CURLOPT_WRITEDATA, pRequest);

	CURLcode Result = curl_easy_perform(pRequest->m_pHandle);
	if(Result != CURLE_OK)
		dbg_msg("http", "libcurl error (%u): %s", Result, curl_easy_strerror(Result));

	{
		long ResponseCode;
		curl_easy_getinfo(pRequest->m_pHandle, CURLINFO_RESPONSE_CODE, &ResponseCode);
		pRequest->m_ResponseCode = ResponseCode;
	}

	curl_slist_free_all(pRequest->m_pHeaderList);
	curl_easy_cleanup(pRequest->m_pHandle);
	return Result != CURLE_OK;
}
