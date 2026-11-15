/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#include <base/system/io.h>
#include <base/system/mem.h>
#include <base/system/string.h>
#include <base/system/thread.h>
#include <base/system/time.h>
#include <engine/console.h>
#include <engine/http.h>
#include <engine/shared/http_request.h>
#include <engine/storage.h>

#include "font_download.h"

// Digest of a font that was already verified, so an unchanged file does not
// have to be hashed on every start. A file whose size or modification time
// differs from the record is hashed again.
class CFontVerificationCache
{
public:
	enum
	{
		MAGIC = 0x43465754, // "TWFC"
		VERSION = 1,
	};

	unsigned m_Magic;
	unsigned m_Version;
	unsigned m_Size;
	int64 m_Modified;
	SHA256_DIGEST m_Digest;
};

CFontDownload::CFontDownload()
{
	m_aName[0] = 0;
	m_aUrl[0] = 0;
	m_aLicenseUrl[0] = 0;
	m_HasSha256 = false;
	mem_zero(&m_Sha256, sizeof(m_Sha256));
	m_ExpectedSize = 0;

	m_State = STATE_IDLE;
	m_aError[0] = 0;

	m_pFontRequest = 0;
	m_pLicenseRequest = 0;
	m_LicenseWritten = false;

	m_pVerifiedData = 0;
	m_VerifiedDataSize = 0;
}

CFontDownload::~CFontDownload()
{
	delete m_pFontRequest;
	delete m_pLicenseRequest;
	ReleaseVerifiedData();
}

void CFontDownload::Init(const char *pName, const char *pUrl, const char *pLicenseUrl,
	const char *pSha256, unsigned ExpectedSize)
{
	str_copy(m_aName, pName, sizeof(m_aName));
	str_copy(m_aUrl, pUrl, sizeof(m_aUrl));
	str_copy(m_aLicenseUrl, pLicenseUrl ? pLicenseUrl : "", sizeof(m_aLicenseUrl));

	// an index without a checksum cannot be verified, so refuse it rather than trust the CDN
	if(pSha256 && pSha256[0] && sha256_from_str(&m_Sha256, pSha256) == 0)
		m_HasSha256 = true;
	else
		m_HasSha256 = false;

	m_ExpectedSize = ExpectedSize;
	m_State = STATE_IDLE;
	m_aError[0] = 0;
}

bool CFontDownload::Exists(IStorage *pStorage)
{
	// FindFile matches the base name and searches pPath, so pFilename has no directory
	char aFound[IO_MAX_PATH_LENGTH];
	if(!pStorage->FindFile(m_aName, "fonts", IStorage::TYPE_ALL, aFound, sizeof(aFound)))
		return false;

	void *pData = 0;
	unsigned Size = 0;
	if(!pStorage->ReadFile(aFound, IStorage::TYPE_ALL, &pData, &Size))
		return false;

	// a cached file failing the checksum counts as absent, so a bad download is repaired
	if(m_HasSha256)
	{
		if(m_ExpectedSize != 0 && Size != m_ExpectedSize)
		{
			mem_free(pData);
			return false;
		}

		time_t Created, Modified;
		const bool HaveTime = pStorage->GetFileTime(aFound, IStorage::TYPE_ALL, &Created, &Modified);
		if(!HaveTime || !VerificationCacheMatches(pStorage, Size, Modified))
		{
			if(!Verify(pData, Size))
			{
				mem_free(pData);
				return false;
			}
			if(HaveTime)
				WriteVerificationCache(pStorage, Size, Modified);
		}
	}

	ReleaseVerifiedData();
	m_pVerifiedData = pData;
	m_VerifiedDataSize = Size;
	return true;
}

bool CFontDownload::VerificationCacheMatches(IStorage *pStorage, unsigned Size, time_t Modified) const
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "fonts/%s.verified", m_aName);

	IOHANDLE File = pStorage->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	CFontVerificationCache Cache;
	const bool Ok = io_read(File, &Cache, sizeof(Cache)) == (unsigned) sizeof(Cache);
	io_close(File);

	return Ok && Cache.m_Magic == CFontVerificationCache::MAGIC &&
	       Cache.m_Version == CFontVerificationCache::VERSION &&
	       Cache.m_Size == Size && Cache.m_Modified == (int64) Modified &&
	       Cache.m_Digest == m_Sha256;
}

void CFontDownload::WriteVerificationCache(IStorage *pStorage, unsigned Size, time_t Modified) const
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "fonts/%s.verified", m_aName);

	CFontVerificationCache Cache;
	Cache.m_Magic = CFontVerificationCache::MAGIC;
	Cache.m_Version = CFontVerificationCache::VERSION;
	Cache.m_Size = Size;
	Cache.m_Modified = (int64) Modified;
	Cache.m_Digest = m_Sha256;

	IOHANDLE File = pStorage->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	io_write(File, &Cache, sizeof(Cache));
	io_close(File);
}

bool CFontDownload::TakeVerifiedData(void **ppData, unsigned *pSize)
{
	if(!m_pVerifiedData)
		return false;

	*ppData = m_pVerifiedData;
	*pSize = m_VerifiedDataSize;
	m_pVerifiedData = 0;
	m_VerifiedDataSize = 0;
	return true;
}

void CFontDownload::ReleaseVerifiedData()
{
	if(m_pVerifiedData)
		mem_free(m_pVerifiedData);
	m_pVerifiedData = 0;
	m_VerifiedDataSize = 0;
}

bool CFontDownload::Verify(const void *pData, unsigned Size) const
{
	if(!m_HasSha256)
		return false;

	if(m_ExpectedSize != 0 && Size != m_ExpectedSize)
		return false;

	SHA256_DIGEST Digest = sha256(pData, Size);
	return Digest == m_Sha256;
}

bool CFontDownload::WriteToStorage(IStorage *pStorage, const char *pFilename, const void *pData, unsigned Size) const
{
	IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	const bool Ok = io_write(File, pData, Size) == Size;
	io_close(File);
	return Ok;
}

void CFontDownload::Start(IHttp *pHttp, IStorage *pStorage, IConsole *pConsole)
{
	char aBuf[512];

	if(Exists(pStorage))
	{
		// already installed by the package or fetched on an earlier run
		m_State = STATE_DONE;
		return;
	}

	if(!m_HasSha256)
	{
		str_format(m_aError, sizeof(m_aError), "'%s' has no valid sha256 in index.json, refusing to download", m_aName);
		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fontdownload", m_aError);
		m_State = STATE_FAILED;
		return;
	}

	str_format(aBuf, sizeof(aBuf), "downloading '%s' (%.1f MiB)", m_aName, m_ExpectedSize / 1024.0f / 1024.0f);
	pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fontdownload", aBuf);

	// 0 means "no timeout" for the version check, so the font request always gets a finite one
	m_pFontRequest = new CHttpRequest("GET", m_aUrl, 120L);
	pHttp->Start(m_pFontRequest);

	if(m_aLicenseUrl[0])
	{
		m_pLicenseRequest = new CHttpRequest("GET", m_aLicenseUrl, 60L);
		pHttp->Start(m_pLicenseRequest);
	}

	m_State = STATE_RUNNING;
}

unsigned CFontDownload::DownloadedBytes() const
{
	if(!m_pFontRequest)
		return 0;
	const int Size = m_pFontRequest->ReceivedDataSize();
	return Size > 0 ? (unsigned) Size : 0;
}

bool CFontDownload::Update(IStorage *pStorage, IConsole *pConsole)
{
	if(m_State != STATE_RUNNING || !m_pFontRequest)
		return m_State != STATE_RUNNING;

	// the license is best effort; a failed or slow license never fails the font
	if(m_pLicenseRequest && !m_LicenseWritten && m_pLicenseRequest->Status() == CHttpRequest::STATE_DONE)
	{
		if(m_pLicenseRequest->Result() == 0 && m_pLicenseRequest->ResponseCode() == 200 &&
			m_pLicenseRequest->ReceivedDataSize() > 0)
		{
			// write as "<name>.license" so the OFL text travels with the font
			char aName[128];
			str_format(aName, sizeof(aName), "fonts/%s.license", m_aName);
			if(WriteToStorage(pStorage, aName, m_pLicenseRequest->ReceivedData(), m_pLicenseRequest->ReceivedDataSize()))
				m_LicenseWritten = true;
		}
		else
		{
			pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "fontdownload", "license download failed, font is kept anyway");
			m_LicenseWritten = true;
		}
	}

	if(m_pFontRequest->Status() != CHttpRequest::STATE_DONE)
		return false;

	const bool Ok = m_pFontRequest->Result() == 0 && m_pFontRequest->ResponseCode() == 200;
	if(!Ok)
	{
		str_format(m_aError, sizeof(m_aError), "download of '%s' failed (result=%d http=%d)",
			m_aName, m_pFontRequest->Result(), m_pFontRequest->ResponseCode());
		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fontdownload", m_aError);
		m_State = STATE_FAILED;
		return true;
	}

	const unsigned Size = (unsigned) m_pFontRequest->ReceivedDataSize();
	if(!Verify(m_pFontRequest->ReceivedData(), Size))
	{
		str_format(m_aError, sizeof(m_aError), "checksum mismatch for '%s', discarding download", m_aName);
		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fontdownload", m_aError);
		m_State = STATE_FAILED;
		return true;
	}

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "fonts/%s", m_aName);
	if(!WriteToStorage(pStorage, aPath, m_pFontRequest->ReceivedData(), Size))
	{
		str_format(m_aError, sizeof(m_aError), "could not write '%s' to the storage", m_aName);
		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fontdownload", m_aError);
		m_State = STATE_FAILED;
		return true;
	}

	// record the digest, so the next start does not have to hash the file
	time_t Created, Modified;
	if(pStorage->GetFileTime(aPath, IStorage::TYPE_SAVE, &Created, &Modified))
		WriteVerificationCache(pStorage, Size, Modified);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "'%s' downloaded and verified (%.1f MiB)", m_aName, Size / 1024.0f / 1024.0f);
	pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fontdownload", aBuf);

	m_State = STATE_DONE;
	return true;
}

bool CFontDownload::WaitForCompletion(IStorage *pStorage, IConsole *pConsole, int TimeoutSeconds)
{
	if(m_State != STATE_RUNNING)
		return m_State == STATE_DONE;

	const int64 Start = time_get();
	const double Freq = (double) time_freq();
	// the request timeout bounds the transfer; this is a safety net for a stalled connection
	const double Deadline = TimeoutSeconds > 0 ? (double) TimeoutSeconds : 0.0;

	while(!Update(pStorage, pConsole))
	{
		if(Deadline > 0.0 && (double) (time_get() - Start) / Freq > Deadline)
		{
			str_format(m_aError, sizeof(m_aError), "download of '%s' timed out", m_aName);
			pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fontdownload", m_aError);
			m_State = STATE_FAILED;
			return false;
		}
		thread_sleep(10);
	}

	return m_State == STATE_DONE;
}

CFontDownloader::CFontDownloader()
{
	m_Finished = false;
}

CFontDownloader::~CFontDownloader()
{
	Clear();
}

CFontDownload *CFontDownloader::Add()
{
	CFontDownload *pDownload = new CFontDownload();
	m_lDownloads.add(pDownload);
	m_Finished = false;
	return pDownload;
}

void CFontDownloader::Clear()
{
	for(int i = 0; i < m_lDownloads.size(); ++i)
		delete m_lDownloads[i];
	m_lDownloads.clear();
	m_Finished = false;
}

CFontDownload *CFontDownloader::Find(const char *pName) const
{
	for(int i = 0; i < m_lDownloads.size(); ++i)
	{
		if(str_comp(m_lDownloads[i]->Name(), pName) == 0)
			return m_lDownloads[i];
	}
	return 0;
}

void CFontDownloader::ReleaseVerifiedData()
{
	for(int i = 0; i < m_lDownloads.size(); ++i)
		m_lDownloads[i]->ReleaseVerifiedData();
}

bool CFontDownloader::Update(IStorage *pStorage, IConsole *pConsole)
{
	bool AllFinal = true;
	for(int i = 0; i < m_lDownloads.size(); ++i)
	{
		CFontDownload *pDownload = m_lDownloads[i];
		if(pDownload->State() == CFontDownload::STATE_RUNNING)
		{
			pDownload->Update(pStorage, pConsole);
			if(pDownload->State() == CFontDownload::STATE_RUNNING)
				AllFinal = false;
		}
		else if(pDownload->State() == CFontDownload::STATE_IDLE)
		{
			AllFinal = false;
		}
	}

	if(AllFinal)
		m_Finished = true;
	return AllFinal;
}

bool CFontDownloader::AnyRunning() const
{
	for(int i = 0; i < m_lDownloads.size(); ++i)
		if(m_lDownloads[i]->State() == CFontDownload::STATE_RUNNING)
			return true;
	return false;
}

unsigned CFontDownloader::TotalDownloadedBytes() const
{
	unsigned Total = 0;
	for(int i = 0; i < m_lDownloads.size(); ++i)
		Total += m_lDownloads[i]->DownloadedBytes();
	return Total;
}

unsigned CFontDownloader::TotalExpectedBytes() const
{
	unsigned Total = 0;
	for(int i = 0; i < m_lDownloads.size(); ++i)
		Total += m_lDownloads[i]->ExpectedSize();
	return Total;
}
