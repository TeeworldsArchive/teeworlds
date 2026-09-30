/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_SHARED_PACKER_H
#define ENGINE_SHARED_PACKER_H

class CPacker
{
public:
	enum
	{
		PACKER_BUFFER_SIZE = 1024 * 2
	};

private:
	unsigned char m_aBuffer[PACKER_BUFFER_SIZE];
	unsigned char *m_pCurrent;
	unsigned char *m_pEnd;

protected:
	bool m_Error;

public:
	void Reset();
	void AddInt(int Integer);
	void AddString(const char *pStr, int Limit = 0);
	void AddRaw(const void *pData, int Size);

	int Size() const { return (int) (m_pCurrent - m_aBuffer); }
	int RemainingSize() const { return (int) (m_pEnd - m_pCurrent); }
	const unsigned char *Data() const { return m_aBuffer; }
	bool Error() const { return m_Error; }
};

class CUnpacker
{
	const unsigned char *m_pStart;
	const unsigned char *m_pCurrent;
	const unsigned char *m_pEnd;

protected:
	bool m_Error;

public:
	enum
	{
		SANITIZE = 1,
		SANITIZE_CC = 2,
		SKIP_START_WHITESPACES = 4
	};

	void Reset(const void *pData, int Size);
	int GetInt();
	int GetIntOrDefault(int Default);
	const char *GetString(int SanitizeType = SANITIZE);
	const unsigned char *GetRaw(int Size);

	int Size() const { return m_pCurrent - m_pStart; }
	int RemainingSize() const { return m_pEnd - m_pCurrent; }
	int CompleteSize() const { return m_pEnd - m_pStart; }
	const unsigned char *Data() const { return m_pStart; }
	bool Error() const { return m_Error; }
};

#endif
