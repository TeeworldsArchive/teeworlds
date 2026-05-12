/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_LOCALIZATION_H
#define GAME_LOCALIZATION_H

#include <base/tl/hashtable.h>

#include <engine/shared/memheap.h>

class CLocalizationDatabase
{
	class CString
	{
	public:
		unsigned m_Hash;
		unsigned m_ContextHash;

		bool operator<(const CString &Other) const { return m_Hash < Other.m_Hash || (m_Hash == Other.m_Hash && m_ContextHash < Other.m_ContextHash); }
		bool operator<=(const CString &Other) const { return m_Hash < Other.m_Hash || (m_Hash == Other.m_Hash && m_ContextHash <= Other.m_ContextHash); }
		bool operator==(const CString &Other) const { return m_Hash == Other.m_Hash && m_ContextHash == Other.m_ContextHash; }
	};

	class CStringHashFunction : public CString
	{
	public:
		static unsigned hash(CString Key) { return Key.m_Hash; }
		static bool equal(CString Key1, CString Key2) { return Key1 == Key2; }
	};

	hash_table<CString, const char *, 64, CStringHashFunction> m_Strings;
	CHeap m_StringsHeap;
	int m_VersionCounter;
	int m_CurrentVersion;

public:
	CLocalizationDatabase();

	bool Load(const char *pFilename, class IStorage *pStorage, class IConsole *pConsole, bool Server);

	int Version() const { return m_CurrentVersion; }

	void AddString(const char *pOrgStr, const char *pNewStr, const char *pContext);
	const char *FindString(unsigned Hash, unsigned ContextHash) const;
};

extern CLocalizationDatabase g_Localization;

class CLocConstString
{
	const char *m_pDefaultStr;
	const char *m_pCurrentStr;
	unsigned m_Hash;
	unsigned m_ContextHash;
	int m_Version;

public:
	CLocConstString(const char *pStr, const char *pContext = "");
	void Reload();

	inline operator const char *()
	{
		if(m_Version != g_Localization.Version())
			Reload();
		return m_pCurrentStr;
	}
};

#endif
