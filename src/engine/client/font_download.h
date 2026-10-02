/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#ifndef ENGINE_CLIENT_FONT_DOWNLOAD_H
#define ENGINE_CLIENT_FONT_DOWNLOAD_H

#include <base/hash.h>
#include <base/tl/array.h>

class IEngine;
class IStorage;
class IConsole;
class CHttpRequest;

// One downloadable entry of "fonts/index.json"; a file already in storage is used as is.
class CFontDownload
{
public:
	enum
	{
		STATE_IDLE = 0,
		STATE_RUNNING,
		STATE_DONE,
		STATE_FAILED,
	};

private:
	char m_aName[64]; // file name below "fonts/", e.g. "SourceHanSans.ttc"
	char m_aUrl[256];
	char m_aLicenseUrl[256];
	bool m_HasSha256;
	SHA256_DIGEST m_Sha256;
	unsigned m_ExpectedSize;

	int m_State;
	char m_aError[256];

	// two requests; the font matters, the license is fetched alongside it
	CHttpRequest *m_pFontRequest;
	CHttpRequest *m_pLicenseRequest;
	bool m_LicenseWritten;

	bool Verify(const void *pData, unsigned Size) const;
	bool WriteToStorage(IStorage *pStorage, const char *pFilename, const void *pData, unsigned Size) const;

public:
	CFontDownload();
	~CFontDownload();

	void Init(const char *pName, const char *pUrl, const char *pLicenseUrl,
		const char *pSha256, unsigned ExpectedSize);

	// starts the request; Storage/Console locate an existing file and report problems
	void Start(IEngine *pEngine, IStorage *pStorage, IConsole *pConsole);

	// non-blocking poll; true once the entry reached a final state
	bool Update(IStorage *pStorage, IConsole *pConsole);

	// blocks until the entry is finished or the timeout elapsed
	bool WaitForCompletion(IStorage *pStorage, IConsole *pConsole, int TimeoutSeconds);

	const char *Name() const { return m_aName; }
	const char *Error() const { return m_aError; }
	int State() const { return m_State; }
	unsigned ExpectedSize() const { return m_ExpectedSize; }
	// progress of the font body in bytes, 0 while the request is queued
	unsigned DownloadedBytes() const;
	bool Exists(IStorage *pStorage) const;
};

// Owns every downloadable font of the current index.json.
class CFontDownloader
{
	array<CFontDownload *> m_vDownloads;
	bool m_Finished;

public:
	CFontDownloader();
	~CFontDownloader();

	CFontDownload *Add();
	void Clear();

	int NumDownloads() const { return m_vDownloads.size(); }
	CFontDownload *Get(int Index) const { return Index >= 0 && Index < m_vDownloads.size() ? m_vDownloads[Index] : 0; }

	// true once every entry is in a final state
	bool Update(IStorage *pStorage, IConsole *pConsole);
	bool AnyRunning() const;
	unsigned TotalDownloadedBytes() const;
	unsigned TotalExpectedBytes() const;
	bool WasFinished() const { return m_Finished; }
};

#endif // ENGINE_CLIENT_FONT_DOWNLOAD_H
