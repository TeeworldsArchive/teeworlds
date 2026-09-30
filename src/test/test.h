/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef TEST_TEST_H
#define TEST_TEST_H
class CTestInfo
{
public:
	CTestInfo();
	void Filename(char *pBuffer, int BufferLength, const char *pSuffix);
	char m_aFilenamePrefix[64];
	char m_aFilename[64];
};
#endif // TEST_TEST_H
