/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <stdio.h>
#include <stdlib.h>

#include <base/system/base.h>
#include <base/system/debug.h>
#include <base/system/io.h>
#include <base/system/mem.h>
#include <base/system/string.h>
#if defined(CONF_FAMILY_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <share.h>
#endif

IOHANDLE io_stdin() { return (IOHANDLE) stdin; }
IOHANDLE io_stdout() { return (IOHANDLE) stdout; }
IOHANDLE io_stderr() { return (IOHANDLE) stderr; }

static IOHANDLE io_open_impl(const char *filename, int flags)
{
	dbg_assert(flags == (IOFLAG_READ | IOFLAG_SKIP_BOM) || flags == IOFLAG_READ || flags == IOFLAG_WRITE || flags == IOFLAG_APPEND, "flags must be read, read+skipbom, write or append");
#if defined(CONF_FAMILY_WINDOWS)
	if((flags & IOFLAG_READ) != 0)
	{
		// check for filename case sensitive
		WIN32_FIND_DATAW finddata;
		HANDLE handle;
		WCHAR wBuffer[IO_MAX_PATH_LENGTH];
		char buffer[IO_MAX_PATH_LENGTH];

		int length = str_length(filename);
		if(!filename || !length || filename[length - 1] == '\\')
			return 0x0;
		MultiByteToWideChar(CP_UTF8, 0, filename, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
		handle = FindFirstFileW(wBuffer, &finddata);
		if(handle == INVALID_HANDLE_VALUE)
			return 0x0;
		WideCharToMultiByte(CP_UTF8, 0, finddata.cFileName, -1, buffer, sizeof(buffer), NULL, NULL);
		if(str_comp(filename + length - str_length(buffer), buffer) != 0)
		{
			FindClose(handle);
			return 0x0;
		}
		FindClose(handle);
		return (IOHANDLE) _wfsopen(wBuffer, L"rb", _SH_DENYNO);
	}
	if(flags == IOFLAG_WRITE)
	{
		WCHAR wBuffer[IO_MAX_PATH_LENGTH];
		MultiByteToWideChar(CP_UTF8, 0, filename, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
		return (IOHANDLE) _wfsopen(wBuffer, L"wb", _SH_DENYNO);
	}
	if(flags == IOFLAG_APPEND)
	{
		WCHAR wBuffer[IO_MAX_PATH_LENGTH];
		MultiByteToWideChar(CP_UTF8, 0, filename, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
		return (IOHANDLE) _wfsopen(wBuffer, L"ab", _SH_DENYNO);
	}
	return 0x0;
#else
	if((flags & IOFLAG_READ) != 0)
		return (IOHANDLE) fopen(filename, "rb");
	if(flags == IOFLAG_WRITE)
		return (IOHANDLE) fopen(filename, "wb");
	if(flags == IOFLAG_APPEND)
		return (IOHANDLE) fopen(filename, "ab");
	return 0x0;
#endif
}

IOHANDLE io_open(const char *filename, int flags)
{
	IOHANDLE result = io_open_impl(filename, flags);
	unsigned char buf[3];
	if((flags & IOFLAG_SKIP_BOM) == 0 || !result)
	{
		return result;
	}
	if(io_read(result, buf, sizeof(buf)) != 3 || buf[0] != 0xef || buf[1] != 0xbb || buf[2] != 0xbf)
	{
		io_seek(result, 0, IOSEEK_START);
	}
	return result;
}

unsigned io_read(IOHANDLE io, void *buffer, unsigned size)
{
	return fread(buffer, 1, size, (FILE *) io);
}

void io_read_all(IOHANDLE io, void **result, unsigned *result_len)
{
	unsigned len = (unsigned) io_length(io);
	char *buffer = (char *) mem_alloc(len + 1);
	unsigned read = io_read(io, buffer, len + 1); // +1 to check if the file size is larger than expected
	if(read < len)
	{
		buffer = (char *) realloc(buffer, read + 1);
		len = read;
	}
	else if(read > len)
	{
		unsigned cap = 2 * read;
		len = read;
		buffer = (char *) realloc(buffer, cap);
		while((read = io_read(io, buffer + len, cap - len)) != 0)
		{
			len += read;
			if(len == cap)
			{
				cap *= 2;
				buffer = (char *) realloc(buffer, cap);
			}
		}
		buffer = (char *) realloc(buffer, len + 1);
	}
	buffer[len] = 0;
	*result = buffer;
	*result_len = len;
}

char *io_read_all_str(IOHANDLE io)
{
	void *buffer;
	unsigned len;
	io_read_all(io, &buffer, &len);
	if(mem_has_null(buffer, len))
	{
		mem_free(buffer);
		return 0x0;
	}
	return (char *) buffer;
}

unsigned io_unread_byte(IOHANDLE io, unsigned char byte)
{
	return ungetc(byte, (FILE *) io) == EOF;
}

unsigned io_skip(IOHANDLE io, int size)
{
	fseek((FILE *) io, size, SEEK_CUR);
	return size;
}

int io_seek(IOHANDLE io, int offset, int origin)
{
	int real_origin;

	switch(origin)
	{
		case IOSEEK_START:
			real_origin = SEEK_SET;
			break;
		case IOSEEK_CUR:
			real_origin = SEEK_CUR;
			break;
		case IOSEEK_END:
			real_origin = SEEK_END;
			break;
		default:
			return -1;
	}

	return fseek((FILE *) io, offset, real_origin);
}

long int io_tell(IOHANDLE io)
{
	return ftell((FILE *) io);
}

long int io_length(IOHANDLE io)
{
	long int length;
	io_seek(io, 0, IOSEEK_END);
	length = io_tell(io);
	io_seek(io, 0, IOSEEK_START);
	return length;
}

unsigned io_write(IOHANDLE io, const void *buffer, unsigned size)
{
	return fwrite(buffer, 1, size, (FILE *) io);
}

unsigned io_write_newline(IOHANDLE io)
{
#if defined(CONF_FAMILY_WINDOWS)
	return fwrite("\r\n", 1, 2, (FILE *) io);
#else
	return fwrite("\n", 1, 1, (FILE *) io);
#endif
}

int io_close(IOHANDLE io)
{
	fclose((FILE *) io);
	return 0;
}

int io_flush(IOHANDLE io)
{
	fflush((FILE *) io);
	return 0;
}

int io_error(IOHANDLE io)
{
	return ferror((FILE *) io);
}
