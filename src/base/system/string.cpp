/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <base/system/base.h>
#include <base/system/debug.h>
#include <base/system/mem.h>
#include <base/system/string.h>
#if defined(CONF_FAMILY_WINDOWS)
#include <process.h> // _vsprintf_p
#endif

void str_append(char *dst, const char *src, int dst_size)
{
	dbg_assert(dst_size > 0, "dst_size invalid");
	strncat(dst, src, dst_size - str_length(dst) - 1);
}

void str_copy(char *dst, const char *src, int dst_size)
{
	dbg_assert(dst_size > 0, "dst_size invalid");
	dst[0] = '\0';
	strncat(dst, src, dst_size - 1);
}

void str_copy_fixed(char *dst, const char *src, int dst_size)
{
	dbg_assert(dst_size > 0, "dst_size invalid");
	int len = str_length(src);
	if(len > dst_size)
		len = dst_size;
	mem_copy(dst, src, len);
	if(len < dst_size)
		dst[len] = '\0';
}

void str_truncate(char *dst, int dst_size, const char *src, int truncation_len)
{
	int size = dst_size;
	if(truncation_len < size)
	{
		size = truncation_len + 1;
	}
	str_copy(dst, src, size);
}

int str_length(const char *str)
{
	return (int) strlen(str);
}

void str_format(char *buffer, int buffer_size, const char *format, ...)
{
	va_list ap;
	dbg_assert(buffer_size > 0, "buffer_size invalid");
	va_start(ap, format);

#if defined(CONF_FAMILY_WINDOWS)
	_vsprintf_p(buffer, buffer_size, format, ap);
#else
	vsnprintf(buffer, buffer_size, format, ap);
#endif

	va_end(ap);

	buffer[buffer_size - 1] = 0; /* assure null termination */
}

/* makes sure that the string only contains the characters between 32 and 127 */
void str_sanitize_strong(char *str_in)
{
	unsigned char *str = (unsigned char *) str_in;
	while(*str)
	{
		*str &= 0x7f;
		if(*str < 32)
			*str = 32;
		str++;
	}
}

/* makes sure that the string only contains the characters between 32 and 255 */
void str_sanitize_cc(char *str_in)
{
	unsigned char *str = (unsigned char *) str_in;
	while(*str)
	{
		if(*str < 32)
			*str = ' ';
		str++;
	}
}

/* check if the string contains '..' (parent directory) paths */
int str_path_unsafe(const char *str)
{
	// State machine. 0 means that we're at the beginning
	// of a new directory/filename, and a positive number represents the number of
	// dots ('.') we found. -1 means we encountered a different character
	// since the last path separator (or the beginning of the string).
	int parse_counter = 0;
	while(*str)
	{
		if(*str == '\\' || *str == '/')
		{
			// A path separator. Check how many dots we found since
			// the last path separator.
			//
			// Two dots => ".." contained in the path. Return an
			// error.
			if(parse_counter == 2)
				return -1;
			else
				parse_counter = 0;
		}
		else if(parse_counter >= 0)
		{
			// If we have not encountered a non-dot character since
			// the last path separator, count the dots.
			if(*str == '.')
				parse_counter++;
			else
				parse_counter = -1;
		}

		++str;
	}
	// If there's a ".." at the end, fail too.
	if(parse_counter == 2)
		return -1;
	return 0;
}

/* makes sure that the string only contains the characters between 32 and 255 + \r\n\t */
void str_sanitize(char *str_in)
{
	unsigned char *str = (unsigned char *) str_in;
	while(*str)
	{
		if(*str < 32 && !(*str == '\r') && !(*str == '\n') && !(*str == '\t'))
			*str = ' ';
		str++;
	}
}

/* removes all forbidden windows/unix characters in filenames*/
char *str_sanitize_filename(char *aName)
{
	char *str = (char *) aName;
	while(*str)
	{
		// replace forbidden characters with a whispace
		if(*str == '/' || *str == '<' || *str == '>' || *str == ':' || *str == '"' || *str == '\\' || *str == '|' || *str == '?' || *str == '*')
			*str = ' ';
		str++;
	}
	str_clean_whitespaces(aName);
	return aName;
}

/* removes leading and trailing spaces and limits the use of multiple spaces */
void str_clean_whitespaces(char *str_in)
{
	char *read = str_in;
	char *write = str_in;

	/* skip initial whitespace */
	while(*read == ' ')
		read++;

	/* end of read string is detected in the loop */
	while(1)
	{
		/* skip whitespace */
		int found_whitespace = 0;
		for(; *read == ' '; read++)
			found_whitespace = 1;
		/* if not at the end of the string, put a found whitespace here */
		if(*read)
		{
			if(found_whitespace)
				*write++ = ' ';
			*write++ = *read++;
		}
		else
		{
			*write = 0;
			break;
		}
	}
}

/* removes leading and trailing spaces */
void str_clean_whitespaces_simple(char *str_in)
{
	char *read = str_in;
	char *write = str_in;

	/* skip initial whitespace */
	while(*read == ' ')
		read++;

	/* end of read string is detected in the loop */
	while(1)
	{
		/* skip whitespace */
		int found_whitespace = 0;
		for(; *read == ' ' && !found_whitespace; read++)
			found_whitespace = 1;
		/* if not at the end of the string, put a found whitespace here */
		if(*read)
		{
			if(found_whitespace)
				*write++ = ' ';
			*write++ = *read++;
		}
		else
		{
			*write = 0;
			break;
		}
	}
}

char *str_skip_to_whitespace(char *str)
{
	while(*str && (*str != ' ' && *str != '\t' && *str != '\n'))
		str++;
	return str;
}

const char *str_skip_to_whitespace_const(const char *str)
{
	while(*str && (*str != ' ' && *str != '\t' && *str != '\n'))
		str++;
	return str;
}

char *str_skip_whitespaces(char *str)
{
	while(*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r'))
		str++;
	return str;
}

const char *str_skip_whitespaces_const(const char *str)
{
	while(*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r'))
		str++;
	return str;
}

/* case */
int str_comp_nocase(const char *a, const char *b)
{
#if defined(CONF_FAMILY_WINDOWS) && !defined(__GNUC__)
	return _stricmp(a, b);
#else
	return strcasecmp(a, b);
#endif
}

int str_comp_nocase_num(const char *a, const char *b, const int num)
{
#if defined(CONF_FAMILY_WINDOWS) && !defined(__GNUC__)
	return _strnicmp(a, b, num);
#else
	return strncasecmp(a, b, num);
#endif
}

int str_comp(const char *a, const char *b)
{
	return strcmp(a, b);
}

int str_comp_num(const char *a, const char *b, const int num)
{
	return strncmp(a, b, num);
}

int str_comp_filenames(const char *a, const char *b)
{
	int result;

	for(; *a && *b; ++a, ++b)
	{
		if(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9')
		{
			result = 0;
			do
			{
				if(!result)
					result = *a - *b;
				++a;
				++b;
			} while(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9');

			if(*a >= '0' && *a <= '9')
				return 1;
			else if(*b >= '0' && *b <= '9')
				return -1;
			else if(result || *a == '\0' || *b == '\0')
				return result;
		}

		result = tolower(*a) - tolower(*b);
		if(result)
			return result;
	}
	return *a - *b;
}

const char *str_startswith_nocase(const char *str, const char *prefix)
{
	int prefixl = str_length(prefix);
	if(str_comp_nocase_num(str, prefix, prefixl) == 0)
	{
		return str + prefixl;
	}
	else
	{
		return 0;
	}
}

const char *str_startswith(const char *str, const char *prefix)
{
	int prefixl = str_length(prefix);
	if(str_comp_num(str, prefix, prefixl) == 0)
	{
		return str + prefixl;
	}
	else
	{
		return 0;
	}
}

const char *str_endswith_nocase(const char *str, const char *suffix)
{
	int strl = str_length(str);
	int suffixl = str_length(suffix);
	const char *strsuffix;
	if(strl < suffixl)
	{
		return 0;
	}
	strsuffix = str + strl - suffixl;
	if(str_comp_nocase(strsuffix, suffix) == 0)
	{
		return strsuffix;
	}
	else
	{
		return 0;
	}
}

const char *str_endswith(const char *str, const char *suffix)
{
	int strl = str_length(str);
	int suffixl = str_length(suffix);
	const char *strsuffix;
	if(strl < suffixl)
	{
		return 0;
	}
	strsuffix = str + strl - suffixl;
	if(str_comp(strsuffix, suffix) == 0)
	{
		return strsuffix;
	}
	else
	{
		return 0;
	}
}

const char *str_find_nocase(const char *haystack, const char *needle)
{
	while(*haystack) /* native implementation */
	{
		const char *a = haystack;
		const char *b = needle;
		while(*a && *b && tolower(*a) == tolower(*b))
		{
			a++;
			b++;
		}
		if(!(*b))
			return haystack;
		haystack++;
	}

	return 0;
}

const char *str_find(const char *haystack, const char *needle)
{
	while(*haystack) /* native implementation */
	{
		const char *a = haystack;
		const char *b = needle;
		while(*a && *b && *a == *b)
		{
			a++;
			b++;
		}
		if(!(*b))
			return haystack;
		haystack++;
	}

	return 0;
}

static int hexval(char x)
{
	switch(x)
	{
		case '0': return 0;
		case '1': return 1;
		case '2': return 2;
		case '3': return 3;
		case '4': return 4;
		case '5': return 5;
		case '6': return 6;
		case '7': return 7;
		case '8': return 8;
		case '9': return 9;
		case 'a':
		case 'A': return 10;
		case 'b':
		case 'B': return 11;
		case 'c':
		case 'C': return 12;
		case 'd':
		case 'D': return 13;
		case 'e':
		case 'E': return 14;
		case 'f':
		case 'F': return 15;
		default: return -1;
	}
}

static int byteval(const char *hex, unsigned char *dst)
{
	int v1 = hexval(hex[0]);
	int v2 = hexval(hex[1]);

	if(v1 < 0 || v2 < 0)
		return 1;

	*dst = v1 * 16 + v2;
	return 0;
}

int str_hex_decode(void *dst, int dst_size, const char *src)
{
	unsigned char *cdst = (unsigned char *) dst;
	int slen = str_length(src);
	int len = slen / 2;
	int i;
	if(slen != dst_size * 2)
		return 2;

	for(i = 0; i < len && dst_size; i++, dst_size--)
	{
		if(byteval(src + i * 2, cdst++))
			return 1;
	}
	return 0;
}

void str_hex(char *dst, int dst_size, const void *data, int data_size)
{
	static const char hex[] = "0123456789ABCDEF";
	int data_index;
	int dst_index;
	for(data_index = 0, dst_index = 0; data_index < data_size && dst_index < dst_size - 3; data_index++)
	{
		dst[data_index * 3] = hex[((const unsigned char *) data)[data_index] >> 4];
		dst[data_index * 3 + 1] = hex[((const unsigned char *) data)[data_index] & 0xf];
		dst[data_index * 3 + 2] = ' ';
		dst_index += 3;
	}
	dst[dst_index] = '\0';
}

int str_is_number(const char *str)
{
	while(*str)
	{
		if(!(*str >= '0' && *str <= '9'))
			return -1;
		str++;
	}
	return 0;
}
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void str_timestamp_ex(time_t time_data, char *buffer, int buffer_size, const char *format)
{
	struct tm *time_info;
	dbg_assert(buffer_size > 0, "buffer_size invalid");
	time_info = localtime(&time_data);
	strftime(buffer, buffer_size, format, time_info);
	buffer[buffer_size - 1] = 0; /* assure null termination */
}

void str_timestamp_format(char *buffer, int buffer_size, const char *format)
{
	time_t time_data;
	time(&time_data);
	str_timestamp_ex(time_data, buffer, buffer_size, format);
}

void str_timestamp(char *buffer, int buffer_size)
{
	str_timestamp_format(buffer, buffer_size, FORMAT_NOSPACE);
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

int str_span(const char *str, const char *set)
{
	return strcspn(str, set);
}
