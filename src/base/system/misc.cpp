/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#define _DEFAULT_SOURCE
#include <stdlib.h>

#include <base/system/base.h>
#include <base/system/misc.h>
#include <base/system/io.h>
#include <base/system/mem.h>
#include <base/system/net.h>
#include <base/system/net_internal.h>
#include <base/system/debug.h>
#include <base/system/string.h>
#if defined(CONF_FAMILY_UNIX)

#include <unistd.h>

#elif defined(CONF_FAMILY_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <shellapi.h>
#include <wincrypt.h>
#else
#error NOT IMPLEMENTED
#endif


void net_stats(NETSTATS *stats_inout)
{
	*stats_inout = network_stats_get();
}


int str_isspace(char c) { return c == ' ' || c == '\n' || c == '\t'; }

char str_uppercase(char c)
{
	if(c >= 'a' && c <= 'z')
		return 'A' + (c - 'a');
	return c;
}

int str_toint(const char *str) { return atoi(str); }
float str_tofloat(const char *str) { return atof(str); }


int str_utf8_is_whitespace(int code)
{
	// check if unicode is not empty
	if(code > 0x20 && code != 0xA0 && code != 0x034F && (code < 0x2000 || code > 0x200F) && (code < 0x2028 || code > 0x202F) &&
		(code < 0x205F || code > 0x2064) && (code < 0x206A || code > 0x206F) && code != 0x3000 && (code < 0xFE00 || code > 0xFE0F) &&
		code != 0xFEFF && (code < 0xFFF9 || code > 0xFFFC))
	{
		return 0;
	}
	return 1;
}

const char *str_utf8_skip_whitespaces(const char *str)
{
	const char *str_old;
	int code;

	while(*str)
	{
		str_old = str;
		code = str_utf8_decode(&str);

		if(!str_utf8_is_whitespace(code))
		{
			return str_old;
		}
	}

	return str;
}

void str_utf8_trim_whitespaces_right(char *str)
{
	int cursor = str_length(str);
	const char *last = str + cursor;
	while(str_utf8_is_whitespace(str_utf8_decode(&last)))
	{
		str[cursor] = 0;
		cursor = str_utf8_rewind(str, cursor);
		last = str + cursor;
		if(cursor == 0)
		{
			break;
		}
	}
}

static int str_utf8_isstart(char c)
{
	if((c & 0xC0) == 0x80) /* 10xxxxxx */
		return 0;
	return 1;
}

int str_utf8_rewind(const char *str, int cursor)
{
	while(cursor)
	{
		cursor--;
		if(str_utf8_isstart(*(str + cursor)))
			break;
	}
	return cursor;
}

int str_utf8_forward(const char *str, int cursor)
{
	const char *buf = str + cursor;
	if(!buf[0])
		return cursor;

	if((*buf & 0x80) == 0x0) /* 0xxxxxxx */
		return cursor + 1;
	else if((*buf & 0xE0) == 0xC0) /* 110xxxxx */
	{
		if(!buf[1])
			return cursor + 1;
		return cursor + 2;
	}
	else if((*buf & 0xF0) == 0xE0) /* 1110xxxx */
	{
		if(!buf[1])
			return cursor + 1;
		if(!buf[2])
			return cursor + 2;
		return cursor + 3;
	}
	else if((*buf & 0xF8) == 0xF0) /* 11110xxx */
	{
		if(!buf[1])
			return cursor + 1;
		if(!buf[2])
			return cursor + 2;
		if(!buf[3])
			return cursor + 3;
		return cursor + 4;
	}

	/* invalid */
	return cursor + 1;
}

int str_utf8_encode(char *ptr, int chr)
{
	/* encode */
	if(chr <= 0x7F)
	{
		ptr[0] = (char) chr;
		return 1;
	}
	else if(chr <= 0x7FF)
	{
		ptr[0] = 0xC0 | ((chr >> 6) & 0x1F);
		ptr[1] = 0x80 | (chr & 0x3F);
		return 2;
	}
	else if(chr <= 0xFFFF)
	{
		ptr[0] = 0xE0 | ((chr >> 12) & 0x0F);
		ptr[1] = 0x80 | ((chr >> 6) & 0x3F);
		ptr[2] = 0x80 | (chr & 0x3F);
		return 3;
	}
	else if(chr <= 0x10FFFF)
	{
		ptr[0] = 0xF0 | ((chr >> 18) & 0x07);
		ptr[1] = 0x80 | ((chr >> 12) & 0x3F);
		ptr[2] = 0x80 | ((chr >> 6) & 0x3F);
		ptr[3] = 0x80 | (chr & 0x3F);
		return 4;
	}

	return 0;
}

int str_utf8_decode(const char **ptr)
{
	const char *buf = *ptr;
	int ch = 0;

	do
	{
		if((*buf & 0x80) == 0x0) /* 0xxxxxxx */
		{
			ch = (unsigned char) *buf;
			buf++;
		}
		else if((*buf & 0xE0) == 0xC0) /* 110xxxxx */
		{
			ch = (*buf++ & 0x3F) << 6;
			if(!(*buf) || (*buf & 0xC0) != 0x80)
				break;
			ch += (*buf++ & 0x3F);
			if(ch < 0x80 || ch > 0x7FF)
				ch = -1;
		}
		else if((*buf & 0xF0) == 0xE0) /* 1110xxxx */
		{
			ch = (*buf++ & 0x1F) << 12;
			if(!(*buf) || (*buf & 0xC0) != 0x80)
				break;
			ch += (*buf++ & 0x3F) << 6;
			if(!(*buf) || (*buf & 0xC0) != 0x80)
				break;
			ch += (*buf++ & 0x3F);
			if(ch < 0x800 || ch > 0xFFFF)
				ch = -1;
		}
		else if((*buf & 0xF8) == 0xF0) /* 11110xxx */
		{
			ch = (*buf++ & 0x0F) << 18;
			if(!(*buf) || (*buf & 0xC0) != 0x80)
				break;
			ch += (*buf++ & 0x3F) << 12;
			if(!(*buf) || (*buf & 0xC0) != 0x80)
				break;
			ch += (*buf++ & 0x3F) << 6;
			if(!(*buf) || (*buf & 0xC0) != 0x80)
				break;
			ch += (*buf++ & 0x3F);
			if(ch < 0x10000 || ch > 0x10FFFF)
				ch = -1;
		}
		else
		{
			/* invalid */
			buf++;
			break;
		}

		*ptr = buf;
		return ch;
	} while(0);

	/* out of bounds */
	*ptr = buf;
	return -1;
}

int str_utf8_check(const char *str)
{
	while(*str)
	{
		if((*str & 0x80) == 0x0)
			str++;
		else if((*str & 0xE0) == 0xC0 && (*(str + 1) & 0xC0) == 0x80)
			str += 2;
		else if((*str & 0xF0) == 0xE0 && (*(str + 1) & 0xC0) == 0x80 && (*(str + 2) & 0xC0) == 0x80)
			str += 3;
		else if((*str & 0xF8) == 0xF0 && (*(str + 1) & 0xC0) == 0x80 && (*(str + 2) & 0xC0) == 0x80 && (*(str + 3) & 0xC0) == 0x80)
			str += 4;
		else
			return 0;
	}
	return 1;
}

void str_utf8_copy_num(char *dst, const char *src, int dst_size, int num)
{
	int new_cursor;
	int cursor = 0;
	dbg_assert(dst_size > 0, "dst_size invalid");

	while(src[cursor] && num > 0)
	{
		new_cursor = str_utf8_forward(src, cursor);
		if(new_cursor >= dst_size) // reserve 1 byte for the null termination
			break;
		else
			cursor = new_cursor;
		--num;
	}

	str_copy(dst, src, cursor < dst_size ? cursor + 1 : dst_size);
}

void str_utf8_stats(const char *str, int max_size, int max_count, int *size, int *count)
{
	*size = 0;
	*count = 0;
	while(*size < max_size && *count < max_count)
	{
		int new_size = str_utf8_forward(str, *size);
		if(new_size == *size || new_size >= max_size)
			break;
		*size = new_size;
		++(*count);
	}
}

unsigned str_quickhash(const char *str)
{
	unsigned hash = 5381;
	for(; *str; str++)
		hash = ((hash << 5) + hash) + (*str); /* hash * 33 + c */
	return hash;
}


struct SECURE_RANDOM_DATA
{
	int initialized;
#if defined(CONF_FAMILY_WINDOWS)
	HCRYPTPROV provider;
#else
	IOHANDLE urandom;
#endif
};

static struct SECURE_RANDOM_DATA secure_random_data = {0};

int secure_random_init()
{
	if(secure_random_data.initialized)
	{
		return 0;
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(CryptAcquireContext(&secure_random_data.provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		secure_random_data.initialized = 1;
		return 0;
	}
	else
	{
		return 1;
	}
#else
	secure_random_data.urandom = io_open("/dev/urandom", IOFLAG_READ);
	if(secure_random_data.urandom)
	{
		secure_random_data.initialized = 1;
		return 0;
	}
	else
	{
		return 1;
	}
#endif
}

int secure_random_uninit()
{
	if(!secure_random_data.initialized)
	{
		return 0;
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(CryptReleaseContext(secure_random_data.provider, 0))
	{
		secure_random_data.initialized = 0;
		return 0;
	}
	else
	{
		return 1;
	}
#else
	if(!io_close(secure_random_data.urandom))
	{
		secure_random_data.initialized = 0;
		return 0;
	}
	else
	{
		return 1;
	}
#endif
}

void secure_random_fill(void *bytes, unsigned length)
{
	if(!secure_random_data.initialized)
	{
		dbg_msg("secure", "called secure_random_fill before secure_random_init");
		dbg_break();
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(!CryptGenRandom(secure_random_data.provider, length, (BYTE *) bytes))
	{
		dbg_msg("secure", "CryptGenRandom failed, last_error=%lu", GetLastError());
		dbg_break();
	}
#else
	if(length != io_read(secure_random_data.urandom, bytes, length))
	{
		dbg_msg("secure", "io_read returned with a short read");
		dbg_break();
	}
#endif
}


int pid()
{
#if defined(CONF_FAMILY_WINDOWS)
	return _getpid();
#else
	return getpid();
#endif
}

void cmdline_fix(int *argc, const char ***argv)
{
#if defined(CONF_FAMILY_WINDOWS)
	int wide_argc = 0;
	int total_size = 0;
	int remaining_size;
	char **new_argv;
	WCHAR **wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
	dbg_assert(wide_argv != NULL, "CommandLineToArgvW failure");

	for(int i = 0; i < wide_argc; i++)
	{
		int size = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, NULL, 0, NULL, NULL);
		dbg_assert(size != 0, "WideCharToMultiByte failure");
		total_size += size;
	}

	new_argv = (char **) malloc((wide_argc + 1) * sizeof(*new_argv));
	new_argv[0] = (char *) malloc(total_size);
	mem_zero(new_argv[0], total_size);

	remaining_size = total_size;
	for(int i = 0; i < wide_argc; i++)
	{
		int size = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, new_argv[i], remaining_size, NULL, NULL);
		dbg_assert(size != 0, "WideCharToMultiByte failure");

		remaining_size -= size;
		new_argv[i + 1] = new_argv[i] + size;
	}

	new_argv[wide_argc] = 0;
	*argc = wide_argc;
	*argv = (const char **) new_argv;
#else
	(void)argc;
	(void)argv;
#endif
}

void cmdline_free([[maybe_unused]] int argc, const char **argv)
{
#if defined(CONF_FAMILY_WINDOWS)
	free((void *) *argv);
	free((char **) argv);
#else
	(void)argc;
	(void)argv;
#endif
}

int bytes_be_to_int(const unsigned char *bytes)
{
	int Result;
	unsigned char *pResult = (unsigned char *) &Result;
	for(unsigned i = 0; i < sizeof(int); i++)
	{
#if defined(CONF_ARCH_ENDIAN_BIG)
		pResult[i] = bytes[i];
#else
		pResult[i] = bytes[sizeof(int) - i - 1];
#endif
	}
	return Result;
}

void int_to_bytes_be(unsigned char *bytes, int value)
{
	const unsigned char *pValue = (const unsigned char *) &value;
	for(unsigned i = 0; i < sizeof(int); i++)
	{
#if defined(CONF_ARCH_ENDIAN_BIG)
		bytes[i] = pValue[i];
#else
		bytes[sizeof(int) - i - 1] = pValue[i];
#endif
	}
}

unsigned bytes_be_to_uint(const unsigned char *bytes)
{
	return ((bytes[0] & 0xffu) << 24u) | ((bytes[1] & 0xffu) << 16u) | ((bytes[2] & 0xffu) << 8u) | (bytes[3] & 0xffu);
}

void uint_to_bytes_be(unsigned char *bytes, unsigned value)
{
	bytes[0] = (value >> 24u) & 0xffu;
	bytes[1] = (value >> 16u) & 0xffu;
	bytes[2] = (value >> 8u) & 0xffu;
	bytes[3] = value & 0xffu;
}
