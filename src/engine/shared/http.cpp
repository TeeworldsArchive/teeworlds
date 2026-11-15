/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#include <base/math.h>
#include <base/system/debug.h>
#include <base/system/thread.h>
#include <base/tl/threading.h>

#include <engine/http.h>

#include <game/version.h>

#include "http_request.h"

#include <curl/curl.h>

static bool ContainsRequest(const array<CHttpRequest *> &lRequests, const CHttpRequest *pRequest)
{
	for(int i = 0; i < lRequests.size(); i++)
		if(lRequests[i] == pRequest)
			return true;
	return false;
}

/*
	The http thread. All requests of the process go through here, so that a
	transfer never holds on to a job thread of the engine: one thread drives
	libcurl's multi interface and can keep several transfers running at the
	same time, and connections to the same host are reused.

	The main thread only queues requests and reads their state; everything
	curl related stays inside this class. Deleting a request is safe at any
	time: Abort() marks it as cancelled and waits until the thread dropped it
	from m_lLive, so no callback can touch it afterwards.
*/
class CHttpManager : public IHttp
{
	class CTransfer
	{
	public:
		CHttpRequest *m_pRequest;
		CURL *m_pHandle;
		curl_slist *m_pHeaders;
		char m_aError[CURL_ERROR_SIZE];

		CTransfer(CHttpRequest *pRequest) :
			m_pRequest(pRequest),
			m_pHandle(0),
			m_pHeaders(0)
		{
			m_aError[0] = 0;
		}
	};

	lock m_Lock;
	array<CHttpRequest *> m_lPending; // guarded by m_Lock
	array<CHttpRequest *> m_lLive; // guarded by m_Lock, every request in flight
	array<CTransfer *> m_lActive; // only the http thread touches this
	CURLM *m_pMulti;
	void *m_pThread;
	bool m_Shutdown;

	static void ThreadMain(void *pUser);
	static size_t WriteCallback(char *pData, size_t Size, size_t Number, void *pUser);
	static int ProgressCallback(void *pUser, curl_off_t DownloadTotal, curl_off_t DownloadNow, curl_off_t UploadTotal, curl_off_t UploadNow);

	void Run();
	CTransfer *CreateTransfer(CHttpRequest *pRequest);
	void StartPending(); // m_Lock has to be held by the caller
	void CancelActive();
	void FinishTransfer(CTransfer *pTransfer, CURLcode Result);
	void DropRequest(CHttpRequest *pRequest, int Result, int ResponseCode); // m_Lock has to be held

public:
	CHttpManager();
	virtual ~CHttpManager();

	virtual void Start(CHttpRequest *pRequest) override;
	virtual void Abort(CHttpRequest *pRequest) override;
	virtual void Shutdown() override;
};

CHttpManager::CHttpManager()
{
	m_pMulti = curl_multi_init();
	if(!m_pMulti)
		dbg_msg("http", "failed to init curl multi handle");
	m_pThread = 0;
	m_Shutdown = false;
}

CHttpManager::~CHttpManager()
{
	Shutdown();
	if(m_pMulti)
		curl_multi_cleanup(m_pMulti);
}

void CHttpManager::Start(CHttpRequest *pRequest)
{
	scope_lock Guard(&m_Lock);

	if(m_Shutdown || !m_pMulti)
	{
		dbg_msg("http", "rejected request for '%s', the http manager is shut down", pRequest->m_aUrl);
		pRequest->SetFinished(-1, 0);
		return;
	}

	if(pRequest->Status() != CHttpRequest::STATE_IDLE)
	{
		dbg_msg("http", "request for '%s' was started twice", pRequest->m_aUrl);
		return;
	}

	pRequest->m_pHttp = this;
	pRequest->m_State = CHttpRequest::STATE_QUEUED;
	m_lPending.add(pRequest);
	m_lLive.add(pRequest);

	if(!m_pThread)
		m_pThread = thread_init(ThreadMain, this);
}

void CHttpManager::Abort(CHttpRequest *pRequest)
{
	{
		scope_lock Guard(&m_Lock);
		if(pRequest->m_pHttp != this)
			return;
		pRequest->m_Cancel = true;
	}

	// wait until the http thread let go of the request; it notices the cancel
	// flag within one poll, so this only takes a moment
	for(;;)
	{
		{
			scope_lock Guard(&m_Lock);
			if(!ContainsRequest(m_lLive, pRequest))
				break;
			if(!m_pThread)
			{
				// no thread left that could still touch the request
				m_lPending.remove(pRequest);
				m_lLive.remove(pRequest);
				break;
			}
		}
		thread_sleep(1);
	}

	scope_lock Guard(&m_Lock);
	pRequest->m_pHttp = 0;
}

void CHttpManager::Shutdown()
{
	{
		scope_lock Guard(&m_Lock);
		m_Shutdown = true;
		for(int i = 0; i < m_lLive.size(); i++)
			m_lLive[i]->m_Cancel = true;
	}

	if(m_pThread)
	{
		// the worker finishes everything that is still in flight before it
		// leaves, so no request is touched after this returns
		thread_wait(m_pThread);
		scope_lock Guard(&m_Lock);
		m_pThread = 0;
	}
}

void CHttpManager::ThreadMain(void *pUser)
{
	CHttpManager *pSelf = static_cast<CHttpManager *>(pUser);
	pSelf->Run();
}

size_t CHttpManager::WriteCallback(char *pData, size_t Size, size_t Number, void *pUser)
{
	CTransfer *pTransfer = static_cast<CTransfer *>(pUser);
	const size_t TotalSize = Size * Number;

	if(pTransfer->m_pRequest->IsCancelled())
		return 0;

	if(!pTransfer->m_pRequest->AppendData(pData, TotalSize))
	{
		dbg_msg("http", "%s: response exceeds %d MiB, aborting", pTransfer->m_pRequest->m_aUrl,
			CHttpRequest::MAX_RESPONSE_SIZE / 1024 / 1024);
		return 0;
	}

	return TotalSize;
}

int CHttpManager::ProgressCallback(void *pUser, [[maybe_unused]] curl_off_t DownloadTotal, [[maybe_unused]] curl_off_t DownloadNow,
	[[maybe_unused]] curl_off_t UploadTotal, [[maybe_unused]] curl_off_t UploadNow)
{
	// A non-zero return aborts the transfer, which is how a request that is
	// still running when the client shuts down is stopped.
	return static_cast<CTransfer *>(pUser)->m_pRequest->IsCancelled() ? 1 : 0;
}

CHttpManager::CTransfer *CHttpManager::CreateTransfer(CHttpRequest *pRequest)
{
	CTransfer *pTransfer = new CTransfer(pRequest);
	pTransfer->m_pHandle = curl_easy_init();
	if(!pTransfer->m_pHandle)
	{
		delete pTransfer;
		return 0;
	}

	CURL *pHandle = pTransfer->m_pHandle;
	curl_easy_setopt(pHandle, CURLOPT_URL, pRequest->m_aUrl);
	curl_easy_setopt(pHandle, CURLOPT_CUSTOMREQUEST, pRequest->m_aRequest);

#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(pHandle, CURLOPT_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(pHandle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
	curl_easy_setopt(pHandle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(pHandle, CURLOPT_MAXREDIRS, 5L);

	curl_easy_setopt(pHandle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(pHandle, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(pHandle, CURLOPT_TIMEOUT, pRequest->m_TimeoutSeconds);

	curl_easy_setopt(pHandle, CURLOPT_POSTFIELDS, (const char *) pRequest->m_PostData.base_ptr());
	curl_easy_setopt(pHandle, CURLOPT_POSTFIELDSIZE, (long) maximum(0, pRequest->m_PostData.size()));

	// bail out early when the server announces an oversized body; the write
	// callback catches chunked responses that do not announce a size
	curl_easy_setopt(pHandle, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t) CHttpRequest::MAX_RESPONSE_SIZE);

	switch(pRequest->m_IPResolve)
	{
		case HTTP_IPRESOLVE_IPV4ONLY: curl_easy_setopt(pHandle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4); break;
		case HTTP_IPRESOLVE_IPV6ONLY: curl_easy_setopt(pHandle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6); break;
		default: curl_easy_setopt(pHandle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER); break;
	}

	curl_easy_setopt(pHandle, CURLOPT_USERAGENT, "TeeworldsArchive " GAME_VERSION " (" CONF_PLATFORM_STRING "; " CONF_ARCH_STRING ")");

	for(int i = 0; i < pRequest->m_lHeaders.size(); i++)
		pTransfer->m_pHeaders = curl_slist_append(pTransfer->m_pHeaders, pRequest->m_lHeaders[i]);
	curl_easy_setopt(pHandle, CURLOPT_HTTPHEADER, pTransfer->m_pHeaders);
	curl_easy_setopt(pHandle, CURLOPT_ACCEPT_ENCODING, "");

	curl_easy_setopt(pHandle, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(pHandle, CURLOPT_WRITEDATA, pTransfer);
	curl_easy_setopt(pHandle, CURLOPT_ERRORBUFFER, pTransfer->m_aError);

	// Cancelling needs the transfer to call back now and then; older libcurl
	// has no such callback object and requests simply run to their timeout.
#if LIBCURL_VERSION_NUM >= 0x072000
	curl_easy_setopt(pHandle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(pHandle, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
	curl_easy_setopt(pHandle, CURLOPT_XFERINFODATA, pTransfer);
#endif

	return pTransfer;
}

void CHttpManager::DropRequest(CHttpRequest *pRequest, int Result, int ResponseCode)
{
	pRequest->SetFinished(Result, ResponseCode);
	m_lLive.remove(pRequest);
}

void CHttpManager::StartPending()
{
	for(int i = 0; i < m_lPending.size(); i++)
	{
		CHttpRequest *pRequest = m_lPending[i];
		CTransfer *pTransfer = 0;

		if(!m_Shutdown && !pRequest->IsCancelled())
			pTransfer = CreateTransfer(pRequest);

		if(!pTransfer || curl_multi_add_handle(m_pMulti, pTransfer->m_pHandle) != CURLM_OK)
		{
			if(pTransfer)
			{
				curl_easy_cleanup(pTransfer->m_pHandle);
				curl_slist_free_all(pTransfer->m_pHeaders);
				delete pTransfer;
			}
			DropRequest(pRequest, -1, 0);
			continue;
		}

		pRequest->m_State = CHttpRequest::STATE_RUNNING;
		m_lActive.add(pTransfer);
	}

	m_lPending.clear();
}

void CHttpManager::CancelActive()
{
	// finish cancelled transfers right away instead of waiting for libcurl to
	// call the progress callback, so Abort() does not have to sit out a poll
	for(int i = m_lActive.size() - 1; i >= 0; i--)
	{
		if(m_lActive[i]->m_pRequest->IsCancelled())
			FinishTransfer(m_lActive[i], CURLE_ABORTED_BY_CALLBACK);
	}
}

void CHttpManager::FinishTransfer(CTransfer *pTransfer, CURLcode Result)
{
	CHttpRequest *pRequest = pTransfer->m_pRequest;

	long ResponseCode = 0;
	if(Result == CURLE_OK)
		curl_easy_getinfo(pTransfer->m_pHandle, CURLINFO_RESPONSE_CODE, &ResponseCode);

	if(Result != CURLE_OK && Result != CURLE_ABORTED_BY_CALLBACK && Result != CURLE_WRITE_ERROR)
	{
		dbg_msg("http", "%s failed: (%u) %s%s%s", pRequest->m_aUrl, (unsigned) Result,
			curl_easy_strerror(Result), pTransfer->m_aError[0] ? ": " : "", pTransfer->m_aError);
	}

	curl_multi_remove_handle(m_pMulti, pTransfer->m_pHandle);
	curl_easy_cleanup(pTransfer->m_pHandle);
	curl_slist_free_all(pTransfer->m_pHeaders);

	m_lActive.remove(pTransfer);
	delete pTransfer;

	scope_lock Guard(&m_Lock);
	DropRequest(pRequest, Result == CURLE_OK ? 0 : 1, (int) ResponseCode);
}

void CHttpManager::Run()
{
	while(1)
	{
		{
			scope_lock Guard(&m_Lock);
			StartPending();
			if(m_Shutdown && m_lLive.size() == 0)
				break;
		}

		CancelActive();

		int Running = 0;
		curl_multi_perform(m_pMulti, &Running);

		int MessagesLeft = 0;
		while(CURLMsg *pMsg = curl_multi_info_read(m_pMulti, &MessagesLeft))
		{
			if(pMsg->msg != CURLMSG_DONE)
				continue;

			CTransfer *pTransfer = 0;
			for(int i = 0; i < m_lActive.size(); i++)
			{
				if(m_lActive[i]->m_pHandle == pMsg->easy_handle)
				{
					pTransfer = m_lActive[i];
					break;
				}
			}
			if(pTransfer)
				FinishTransfer(pTransfer, (CURLcode) pMsg->data.result);
		}

		// wakes up at least every 100ms to pick up new requests and shutdown
		int NumFds = 0;
#if LIBCURL_VERSION_NUM >= 0x074200
		curl_multi_poll(m_pMulti, nullptr, 0, 100, &NumFds);
#elif LIBCURL_VERSION_NUM >= 0x071C00
		curl_multi_wait(m_pMulti, nullptr, 0, 100, &NumFds);
#else
		thread_sleep(100);
#endif
	}
}

IHttp *CreateHttp()
{
	return new CHttpManager();
}
