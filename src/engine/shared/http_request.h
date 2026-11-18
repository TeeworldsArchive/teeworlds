/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#ifndef ENGINE_SHARED_HTTP_REQUEST_H
#define ENGINE_SHARED_HTTP_REQUEST_H

#include <base/tl/array.h>

extern "C" {
struct curl_slist;
}

enum
{
	HTTP_IPRESOLVE_BOTH = 0,
	HTTP_IPRESOLVE_IPV4ONLY,
	HTTP_IPRESOLVE_IPV6ONLY,
};

class IHttp;

// A single HTTP request. The object describes the request and stores the
// result; the transfer is driven by IHttp (CHttpManager), which runs all
// requests of the process on one thread with libcurl's multi interface and
// therefore never occupies a job thread of the engine.
class CHttpRequest
{
	friend class CHttpManager;

public:
	enum
	{
		STATE_IDLE = 0, // not started yet
		STATE_QUEUED, // handed to the http thread, no transfer yet
		STATE_RUNNING, // the transfer is in progress
		STATE_DONE, // finished, failed or cancelled; the result is final
	};

private:
	enum
	{
		// a guard against a server that never stops sending, not a real
		// limit: the largest download (the CJK font) is about 20 MiB
		MAX_RESPONSE_SIZE = 64 * 1024 * 1024,
	};

	char m_aRequest[16];
	char m_aUrl[256];
	long m_TimeoutSeconds;
	int m_IPResolve;

	array<unsigned char> m_PostData;
	array<char *> m_lHeaders;
	array<unsigned char> m_ReceivedData;

	// written by the http thread while the transfer runs, so the main thread
	// can report progress without touching the buffer; the buffer itself is
	// only used after Status() reports STATE_DONE
	volatile int m_ReceivedBytes;
	volatile int m_ResponseCode;
	volatile int m_Result;
	volatile int m_State;
	volatile bool m_Cancel;

	bool m_IsChecked;
	IHttp *m_pHttp; // set by Start(), cleared by Abort()

	bool AppendData(const void *pData, unsigned Size);
	void SetFinished(int Result, int ResponseCode);

public:
	CHttpRequest(const char *pRequest, const char *pUrl, long TimeoutSeconds, int IPResolve = HTTP_IPRESOLVE_BOTH);
	~CHttpRequest();

	CHttpRequest(const CHttpRequest &) = delete;
	CHttpRequest &operator=(const CHttpRequest &) = delete;

	void PostData(const unsigned char *pPost, int Size);
	void PostJson(const char *pJson);
	void AddHeader(const char *pHeader);

	// Aborts the transfer. The request can be deleted right afterwards; the
	// destructor waits until the http thread is done with the object.
	void Cancel() { m_Cancel = true; }
	bool IsCancelled() const { return m_Cancel; }

	void MarkAsChecked() { m_IsChecked = true; }

	bool IsChecked() const { return m_IsChecked; }
	int Status() const { return m_State; }
	int Result() const { return m_Result; }
	int ResponseCode() const { return m_ResponseCode; }
	const unsigned char *ReceivedData() const { return m_ReceivedData.base_ptr(); }
	int ReceivedDataSize() const { return m_ReceivedBytes; }
	// Hands the received data over without copying it; a GIF avatar can be a
	// few megabytes and the caller is done with the request anyway.
	void MoveReceivedData(array<unsigned char> *pOut);
};

void EscapeUrl(char *pBuf, int Size, const char *pStr);

#endif // ENGINE_SHARED_HTTP_REQUEST_H
