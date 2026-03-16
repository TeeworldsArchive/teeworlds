/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/math.h>
#include <base/tl/algorithm.h>

#include <engine/storage.h>

#include "filecollection.h"

bool CFileCollection::IsFilenameValid(const char *pFilename, int64 *pTimestamp)
{
	if(!str_endswith(pFilename, m_aFileExt))
		return false;
	
	if(m_aFileDesc[0] == '\0')
	{
		int FilenameLength = str_length(pFilename);
		if(m_FileExtLength + TIMESTAMP_LENGTH > FilenameLength)
		{
			return false;
		}

		pFilename += FilenameLength - m_FileExtLength - TIMESTAMP_LENGTH;
	}
	else
	{
		if(str_length(pFilename) != m_FileDescLength + TIMESTAMP_LENGTH + m_FileExtLength ||
			str_startswith(pFilename, m_aFileDesc))
			return false;
		pFilename += m_FileDescLength;
	}
	if(pFilename[0] == '_' &&
		pFilename[1] >= '0' && pFilename[1] <= '9' &&
		pFilename[2] >= '0' && pFilename[2] <= '9' &&
		pFilename[3] >= '0' && pFilename[3] <= '9' &&
		pFilename[4] >= '0' && pFilename[4] <= '9' &&
		pFilename[5] == '-' &&
		pFilename[6] >= '0' && pFilename[6] <= '9' &&
		pFilename[7] >= '0' && pFilename[7] <= '9' &&
		pFilename[8] == '-' &&
		pFilename[9] >= '0' && pFilename[9] <= '9' &&
		pFilename[10] >= '0' && pFilename[10] <= '9' &&
		pFilename[11] == '_' &&
		pFilename[12] >= '0' && pFilename[12] <= '9' &&
		pFilename[13] >= '0' && pFilename[13] <= '9' &&
		pFilename[14] == '-' &&
		pFilename[15] >= '0' && pFilename[15] <= '9' &&
		pFilename[16] >= '0' && pFilename[16] <= '9' &&
		pFilename[17] == '-' &&
		pFilename[18] >= '0' && pFilename[18] <= '9' &&
		pFilename[19] >= '0' && pFilename[19] <= '9')
	{
		*pTimestamp = ExtractTimestamp(pFilename + 1);
		return true;
	}

	return false;
}

int64 CFileCollection::ExtractTimestamp(const char *pTimestring)
{
	int64 Timestamp = pTimestring[0] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[1] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[2] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[3] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[5] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[6] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[8] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[9] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[11] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[12] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[14] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[15] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[17] - '0';
	Timestamp <<= 4;
	Timestamp += pTimestring[18] - '0';

	return Timestamp;
}

void CFileCollection::BuildTimestring(int64 Timestamp, char *pTimestring)
{
	pTimestring[19] = 0;
	pTimestring[18] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[17] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[16] = '-';
	pTimestring[15] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[14] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[13] = '-';
	pTimestring[12] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[11] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[10] = '_';
	pTimestring[9] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[8] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[7] = '-';
	pTimestring[6] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[5] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[4] = '-';
	pTimestring[3] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[2] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[1] = (Timestamp & 0xF) + '0';
	Timestamp >>= 4;
	pTimestring[0] = (Timestamp & 0xF) + '0';
}

void CFileCollection::Init(IStorage *pStorage, const char *pPath, const char *pFileDesc, const char *pFileExt, int MaxEntries)
{
	m_aEntries.clear();
	m_MaxEntries = clamp(MaxEntries, 1, static_cast<int>(MAX_ENTRIES));
	str_copy(m_aFileDesc, pFileDesc, sizeof(m_aFileDesc));
	m_FileDescLength = str_length(m_aFileDesc);
	str_copy(m_aFileExt, pFileExt, sizeof(m_aFileExt));
	m_FileExtLength = str_length(m_aFileExt);
	str_copy(m_aPath, pPath, sizeof(m_aPath));
	m_pStorage = pStorage;

	m_pStorage->ListDirectory(IStorage::TYPE_SAVE, m_aPath, FilelistCallback, this);
	if(!m_aEntries.size())
		return;

	sort(m_aEntries.all());
	for(int i = 0; i < m_aEntries.size(); i++)
	{
		if(m_aEntries.size() - i <= m_MaxEntries)
			break;

		char aBuf[IO_MAX_PATH_LENGTH];
		if(m_aFileDesc[0] == '\0')
		{
			str_format(aBuf, sizeof(aBuf), "%s/%s", m_aPath, m_aEntries[i].m_aFilename);
		}
		else
		{
			char aTimestring[TIMESTAMP_LENGTH];
			BuildTimestring(m_aEntries[i].m_Timestamp, aTimestring);
			str_format(aBuf, sizeof(aBuf), "%s/%s_%s%s", m_aPath, m_aFileDesc, aTimestring, m_aFileExt);
		}
		m_pStorage->RemoveFile(aBuf, IStorage::TYPE_SAVE);
	}
}

int CFileCollection::FilelistCallback(const char *pFilename, int IsDir, int StorageType, void *pUser)
{
	CFileCollection *pThis = static_cast<CFileCollection *>(pUser);

	int64 Timestamp;
	// check for valid file name format and extract the timestamp
	if(IsDir || !pThis->IsFilenameValid(pFilename, &Timestamp))
		return 0;

	// add the entry
 	CEntry &Entry = pThis->m_aEntries.emplace();
	Entry.m_Timestamp = Timestamp;
	str_copy(Entry.m_aFilename, pFilename, sizeof(Entry.m_aFilename));

	return 0;
}
