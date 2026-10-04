/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_STRING_H
#define BASE_SYSTEM_STRING_H

#include <time.h>
#include <base/system/base.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Strings */

/*
	Function: str_append
		Appends a string to another.

	Parameters:
		dst - Pointer to a buffer that contains a string.
		src - String to append.
		dst_size - Size of the buffer of the dst string.

	Remarks:
		- The strings are treated as zero-terminated strings.
		- Garantees that dst string will contain zero-termination.
*/
void str_append(char *dst, const char *src, int dst_size);

/*
	Function: str_copy
		Copies a string to another.

	Parameters:
		dst - Pointer to a buffer that shall receive the string.
		src - String to be copied.
		dst_size - Size of the buffer dst.

	Remarks:
		- The strings are treated as zero-terminated strings.
		- Garantees that dst string will contain zero-termination.
*/
void str_copy(char *dst, const char *src, int dst_size);

/*
	Function: str_copy_fixed
		Copies a string into a fixed size field without a terminator, so the
		field can carry its full content capacity.

	Parameters:
		dst - Pointer to the field that shall receive the bytes.
		src - Zero-terminated string to be copied.
		dst_size - Size of the field dst in bytes.

	Remarks:
		- At most dst_size bytes are copied.
		- The remaining bytes of dst are zeroed, but dst is not
		  zero-terminated unless the string is shorter than the field.
*/
void str_copy_fixed(char *dst, const char *src, int dst_size);

/*
	Function: str_truncate
		Truncates a string to a given length.

	Parameters:
		dst - Pointer to a buffer that shall receive the string.
		dst_size - Size of the buffer dst.
		src - String to be truncated.
		truncation_len - Maximum length of the returned string (not
		counting the zero termination).

	Remarks:
		- The strings are treated as zero-terminated strings.
		- Garantees that dst string will contain zero-termination.
*/
void str_truncate(char *dst, int dst_size, const char *src, int truncation_len);

/*
	Function: str_length
		Returns the length of a zero terminated string.

	Parameters:
		str - Pointer to the string.

	Returns:
		Length of string in bytes excluding the zero termination.
*/
int str_length(const char *str);

/*
	Function: str_format
		Performs printf formatting into a buffer.

	Parameters:
		buffer - Pointer to the buffer to receive the formatted string.
		buffer_size - Size of the buffer.
		format - printf formatting string.
		... - Parameters for the formatting.

	Remarks:
		- See the C manual for syntax for the printf formatting string.
		- The strings are treated as zero-termineted strings.
		- Guarantees that dst string will contain zero-termination.
*/
void str_format(char *buffer, int buffer_size, const char *format, ...)
	GNUC_ATTRIBUTE((format(printf, 3, 4)));

/*
	Function: str_sanitize_strong
		Replaces all characters below 32 and above 127 with whitespace.

	Parameters:
		str - String to sanitize.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_sanitize_strong(char *str);

/*
	Function: str_sanitize_cc
		Replaces all characters below 32 with whitespace.

	Parameters:
		str - String to sanitize.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_sanitize_cc(char *str);

/*
	Function: str_sanitize
		Replaces all characters below 32 with whitespace with
		exception to \t, \n and \r.

	Parameters:
		str - String to sanitize.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_sanitize(char *str);

/*
	Function: str_sanitize_filename
		Replaces all forbidden Windows/Unix characters with whitespace
		or nothing if leading or trailing.

	Parameters:
		str - String to sanitize.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
char *str_sanitize_filename(char *name);

/*
	Function: str_path_unsafe
		Check if the string contains '..' (parent directory) paths.

	Parameters:
		str - String to check.

	Returns:
		Returns 0 if the path is safe, -1 otherwise.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_path_unsafe(const char *str);

/*
	Function: str_clean_whitespaces
		Removes leading and trailing spaces and limits the use of multiple spaces.

	Parameters:
		str - String to clean up

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_clean_whitespaces(char *str);

/*
	Function: str_clean_whitespaces_simple
		Removes leading and trailing spaces

	Parameters:
		str - String to clean up

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_clean_whitespaces_simple(char *str);

/*
	Function: str_skip_to_whitespace
		Skips leading non-whitespace characters(all but ' ', '\t', '\n', '\r').

	Parameters:
		str - Pointer to the string.

	Returns:
		Pointer to the first whitespace character found
		within the string.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
char *str_skip_to_whitespace(char *str);

/*
	Function: str_skip_to_whitespace_const
		See str_skip_to_whitespace.
*/
const char *str_skip_to_whitespace_const(const char *str);

/*
	Function: str_skip_whitespaces
		Skips leading whitespace characters(' ', '\t', '\n', '\r').

	Parameters:
		str - Pointer to the string.

	Returns:
		Pointer to the first non-whitespace character found
		within the string.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
char *str_skip_whitespaces(char *str);

/*
	Function: str_skip_whitespaces_const
		See str_skip_whitespaces.
*/
const char *str_skip_whitespaces_const(const char *str);

/*
	Function: str_comp_nocase
		Compares to strings case insensitive.

	Parameters:
		a - String to compare.
		b - String to compare.

	Returns:
		<0 - String a is lesser then string b
		0 - String a is equal to string b
		>0 - String a is greater then string b

	Remarks:
		- Only garanted to work with a-z/A-Z.
		- The strings are treated as zero-terminated strings.
*/
int str_comp_nocase(const char *a, const char *b);

/*
	Function: str_comp_nocase_num
		Compares up to num characters of two strings case insensitive.

	Parameters:
		a - String to compare.
		b - String to compare.
		num - Maximum characters to compare

	Returns:
		<0 - String a is lesser than string b
		0 - String a is equal to string b
		>0 - String a is greater than string b

	Remarks:
		- Only garanted to work with a-z/A-Z.
		- The strings are treated as zero-terminated strings.
*/
int str_comp_nocase_num(const char *a, const char *b, const int num);

/*
	Function: str_comp
		Compares to strings case sensitive.

	Parameters:
		a - String to compare.
		b - String to compare.

	Returns:
		<0 - String a is lesser then string b
		0 - String a is equal to string b
		>0 - String a is greater then string b

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_comp(const char *a, const char *b);

/*
	Function: str_comp_num
		Compares up to num characters of two strings case sensitive.

	Parameters:
		a - String to compare.
		b - String to compare.
		num - Maximum characters to compare

	Returns:
		<0 - String a is lesser then string b
		0 - String a is equal to string b
		>0 - String a is greater then string b

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_comp_num(const char *a, const char *b, const int num);

/*
	Function: str_comp_filenames
		Compares two strings case sensitive, digit chars will be compared as numbers.

	Parameters:
		a - String to compare.
		b - String to compare.

	Returns:
		<0 - String a is lesser then string b
		0 - String a is equal to string b
		>0 - String a is greater then string b

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_comp_filenames(const char *a, const char *b);

/*
	Function: str_startswith_nocase
		Checks case insensitive whether the string begins with a certain prefix.

	Parameter:
		str - String to check.
		prefix - Prefix to look for.

	Returns:
		A pointer to the string str after the string prefix, or 0 if
		the string prefix isn't a prefix of the string str.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_startswith_nocase(const char *str, const char *prefix);

/*
	Function: str_startswith
		Checks case sensitive whether the string begins with a certain prefix.

	Parameter:
		str - String to check.
		prefix - Prefix to look for.

	Returns:
		A pointer to the string str after the string prefix, or 0 if
		the string prefix isn't a prefix of the string str.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_startswith(const char *str, const char *prefix);

/*
	Function: str_endswith_nocase
		Checks case insensitive whether the string ends with a certain suffix.

	Parameter:
		str - String to check.
		suffix - Suffix to look for.

	Returns:
		A pointer to the beginning of the suffix in the string str, or
		0 if the string suffix isn't a suffix of the string str.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_endswith_nocase(const char *str, const char *suffix);

/*
	Function: str_endswith
		Checks case sensitive whether the string ends with a certain suffix.

	Parameter:
		str - String to check.
		suffix - Suffix to look for.

	Returns:
		A pointer to the beginning of the suffix in the string str, or
		0 if the string suffix isn't a suffix of the string str.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_endswith(const char *str, const char *suffix);

/*
	Function: str_find_nocase
		Finds a string inside another string case insensitive.

	Parameters:
		haystack - String to search in
		needle - String to search for

	Returns:
		A pointer into haystack where the needle was found.
		Returns NULL of needle could not be found.

	Remarks:
		- Only garanted to work with a-z/A-Z.
		- The strings are treated as zero-terminated strings.
*/
const char *str_find_nocase(const char *haystack, const char *needle);

/*
	Function: str_find
		Finds a string inside another string case sensitive.

	Parameters:
		haystack - String to search in
		needle - String to search for

	Returns:
		A pointer into haystack where the needle was found.
		Returns NULL of needle could not be found.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_find(const char *haystack, const char *needle);

/*
	Function: str_hex_decode
		Takes a hex string *without spaces between bytes* and returns a
		byte array.

	Parameters:
		dst - Buffer for the byte array
		dst_size - size of the buffer
		data - String to decode

	Returns:
		2 - String doesn't exactly fit the buffer
		1 - Invalid character in string
		0 - Success

	Remarks:
		- The contents of the buffer is only valid on success
*/
int str_hex_decode(void *dst, int dst_size, const char *src);

/*
	Function: str_hex
		Takes a datablock and generates a hexstring of it.

	Parameters:
		dst - Buffer to fill with hex data
		dst_size - size of the buffer
		data - Data to turn into hex
		data - Size of the data

	Remarks:
		- The destination buffer will be zero-terminated
*/
void str_hex(char *dst, int dst_size, const void *data, int data_size);

/*
	Function: str_is_number
		Check if the string contains only digits.

	Parameters:
		str - String to check.

	Returns:
		Returns 0 if it's a number, -1 otherwise.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_is_number(const char *pstr);

/*
	Function: str_timestamp
		Copies a time stamp in the format year-month-day_hour-minute-second to the string.

	Parameters:
		buffer - Pointer to a buffer that shall receive the time stamp string.
		buffer_size - Size of the buffer.

	Remarks:
		- Guarantees that buffer string will contain zero-termination.
*/
void str_timestamp(char *buffer, int buffer_size);
void str_timestamp_format(char *buffer, int buffer_size, const char *format)
	GNUC_ATTRIBUTE((format(strftime, 3, 0)));
void str_timestamp_ex(time_t time, char *buffer, int buffer_size, const char *format)
	GNUC_ATTRIBUTE((format(strftime, 4, 0)));

/*
	Function: str_span
		Returns the length of the minimum initial segment that doesn't contain characters in set

	Parameters:
		str - String to search in
		set - Set of characters to stop on

	Remarks:
		- Also stops on '\0'
*/
int str_span(const char *str, const char *set);

#define FORMAT_TIME "%H:%M:%S"
#define FORMAT_SPACE "%Y-%m-%d %H:%M:%S"
#define FORMAT_NOSPACE "%Y-%m-%d_%H-%M-%S"

int str_toint(const char *str);
float str_tofloat(const char *str);
int str_isspace(char c);
char str_uppercase(char c);
unsigned str_quickhash(const char *str);

enum
{
	UTF8_BYTE_LENGTH = 4
};
/*
	Function: str_utf8_is_whitespace
		Check if the unicode is an utf8 whitespace.

	Parameters:
		code - unicode.

	Returns:
		Returns 1 on success, 0 on failure.
*/
int str_utf8_is_whitespace(int code);

/*
	Function: str_utf8_skip_whitespaces
		Skips leading utf8 whitespace characters.

	Parameters:
		str - Pointer to the string.

	Returns:
		Pointer to the first non-whitespace character found
		within the string.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_utf8_skip_whitespaces(const char *str);

/*
	Function: str_utf8_trim_whitespaces_right
		Clears trailing utf8 whitespace characters from a string.

	Parameters:
		str - Pointer to the string.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_utf8_trim_whitespaces_right(char *str);

/*
	Function: str_utf8_rewind
		Moves a cursor backwards in an utf8 string

	Parameters:
		str - utf8 string
		cursor - position in the string

	Returns:
		New cursor position.

	Remarks:
		- Won't move the cursor less then 0
*/
int str_utf8_rewind(const char *str, int cursor);

/*
	Function: str_utf8_forward
		Moves a cursor forwards in an utf8 string

	Parameters:
		str - utf8 string
		cursor - position in the string

	Returns:
		New cursor position.

	Remarks:
		- Won't move the cursor beyond the zero termination marker
*/
int str_utf8_forward(const char *str, int cursor);

/*
	Function: str_utf8_decode
		Decodes an utf8 character

	Parameters:
		ptr - pointer to an utf8 string. this pointer will be moved forward

	Returns:
		Unicode value for the character. -1 for invalid characters and 0 for end of string.

	Remarks:
		- This function will also move the pointer forward.
*/
int str_utf8_decode(const char **ptr);

/*
	Function: str_utf8_encode
		Encode an utf8 character

	Parameters:
		ptr - Pointer to a buffer that should recive the data. Should be able to hold at least 4 bytes.

	Returns:
		Number of bytes put into the buffer.

	Remarks:
		- Does not do zero termination of the string.
*/
int str_utf8_encode(char *ptr, int chr);

/*
	Function: str_utf8_check
		Checks if a strings contains just valid utf8 characters.

	Parameters:
		str - Pointer to a possible utf8 string.

	Returns:
		0 - invalid characters found.
		1 - only valid characters found.

	Remarks:
		- The string is treated as zero-terminated utf8 string.
*/
int str_utf8_check(const char *str);

/*
	Function: str_utf8_copy_num
		Copies a number of utf8 characters from one string to another.

	Parameters:
		dst - Pointer to a buffer that shall receive the string.
		src - String to be copied.
		dst_size - Size of the buffer dst.
		num - maximum number of utf8 characters to be copied.

	Remarks:
		- The strings are treated as zero-terminated strings.
		- Garantees that dst string will contain zero-termination.
*/
void str_utf8_copy_num(char *dst, const char *src, int dst_size, int num);

/*
	Function: str_utf8_stats
		Determines the byte size and utf8 character count of a utf8 string.

	Parameters:
		str - Pointer to the string.
		max_size - Maximum number of bytes to count.
		max_count - Maximum number of utf8 characters to count.
		size - Pointer to store size (number of non-zero bytes) of the string.
		count - Pointer to store count of utf8 characters of the string.

	Remarks:
		- The string is treated as zero-terminated utf8 string.
		- It's the user's responsibility to make sure the bounds are aligned.
*/
void str_utf8_stats(const char *str, int max_size, int max_count, int *size, int *count);

/*
	Function: secure_random_init
		Initializes the secure random module.
		You *MUST* check the return value of this function.

	Returns:
		0 - Initialization succeeded.
		1 - Initialization failed.
*/
#ifdef __cplusplus
}
#endif

#endif
