/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#include <base/system/mem.h>
#include <base/system/string.h>
#include <base/tl/stream.h>

#include <engine/http.h>

#include "http_request.h"

#include <curl/curl.h>

void EscapeUrl(char *pBuf, int Size, const char *pStr)
{
	char *pEsc = curl_easy_escape(nullptr, pStr, 0);
	if(!pEsc)
	{
		pBuf[0] = 0;
		return;
	}
	str_copy(pBuf, pEsc, Size);
	curl_free(pEsc);
}

CHttpRequest::CHttpRequest(const char *pRequest, const char *pUrl, long TimeoutSeconds, int IPResolve)
{
	str_copy(m_aRequest, pRequest, sizeof(m_aRequest));
	str_copy(m_aUrl, pUrl, sizeof(m_aUrl));

	m_TimeoutSeconds = TimeoutSeconds;
	m_IPResolve = IPResolve;

	m_ReceivedBytes = 0;
	m_ResponseCode = 0;
	m_Result = 0;
	m_State = STATE_IDLE;
	m_Cancel = false;

	m_IsChecked = false;
	m_pHttp = 0;

	m_PostData.clear();
	m_lHeaders.clear();
	m_ReceivedData.clear();
}

CHttpRequest::~CHttpRequest()
{
	// cancels a running transfer and waits until the http thread let go of the
	// object, so the buffers below can be freed
	if(m_pHttp)
		m_pHttp->Abort(this);

	for(int i = 0; i < m_lHeaders.size(); i++)
		mem_free(m_lHeaders[i]);
	m_lHeaders.clear();
}

void CHttpRequest::PostData(const unsigned char *pPost, int Size)
{
	// memory_stream no longer resets its buffer, so clear it to replace the body
	m_PostData.clear();
	memory_stream<unsigned char> Stream(&m_PostData);
	Stream.write(pPost, Size);
}

void CHttpRequest::PostJson(const char *pJson)
{
	PostData((const unsigned char *) pJson, str_length(pJson));
	AddHeader("Content-Type: application/json");
}

void CHttpRequest::AddHeader(const char *pHeader)
{
	const int Length = str_length(pHeader);
	char *pCopy = (char *) mem_alloc(Length + 1);
	mem_copy(pCopy, pHeader, Length + 1);
	m_lHeaders.add(pCopy);
}

bool CHttpRequest::AppendData(const void *pData, unsigned Size)
{
	if((unsigned) m_ReceivedData.size() + Size > (unsigned) MAX_RESPONSE_SIZE)
		return false;
	memory_stream<unsigned char> Stream(&m_ReceivedData);
	Stream.write((const unsigned char *) pData, Size);
	m_ReceivedBytes = m_ReceivedData.size();
	return true;
}

void CHttpRequest::SetFinished(int Result, int ResponseCode)
{
	m_Result = Result;
	m_ResponseCode = ResponseCode;
	m_State = STATE_DONE;
}

void CHttpRequest::MoveReceivedData(array<unsigned char> *pOut)
{
	m_ReceivedData.move(*pOut);
	m_ReceivedBytes = 0;
}
