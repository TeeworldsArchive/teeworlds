/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_SHARED_LINEREADER_H
#define ENGINE_SHARED_LINEREADER_H
#include <base/system/debug.h>
#include <base/system/io.h>

// buffered stream for reading lines, should perhaps be something smaller
class CLineReader
{
	char m_aBuffer[4 * 1024 + 1]; // 1 additional byte for null termination
	unsigned m_BufferPos;
	unsigned m_BufferSize;
	unsigned m_BufferMaxSize;
	IOHANDLE m_IO;

public:
	void Init(IOHANDLE IoHandle);
	const char *Get();
};
#endif
