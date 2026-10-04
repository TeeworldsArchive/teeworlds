/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <base/system/base.h>
#include <base/system/debug.h>
#include <base/system/aio.h>
#include <base/system/io.h>
#include <base/system/string.h>
#if defined(CONF_FAMILY_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


typedef struct
{
	DBG_LOGGER logger;
	DBG_LOGGER_FINISH finish;
	void *user;
} DBG_LOGGER_DATA;

static DBG_LOGGER_DATA loggers[16];
static int num_loggers = 0;



static void dbg_logger_finish(void)
{
	int i;
	for(i = 0; i < num_loggers; i++)
	{
		if(loggers[i].finish)
		{
			loggers[i].finish(loggers[i].user);
		}
	}
}

void dbg_logger(DBG_LOGGER logger, DBG_LOGGER_FINISH finish, void *user)
{
	DBG_LOGGER_DATA data;
	if(num_loggers == 0)
	{
		atexit(dbg_logger_finish);
	}
	data.logger = logger;
	data.finish = finish;
	data.user = user;
	loggers[num_loggers] = data;
	num_loggers++;
}

void dbg_assert_imp(const char *filename, int line, int test, const char *msg)
{
	if(!test)
	{
		dbg_msg("assert", "%s(%d): %s", filename, line, msg);
		dbg_break();
	}
}

void dbg_break()
{
#ifdef __GNUC__
	__builtin_trap();
#else
	abort();
#endif
}

void dbg_msg(const char *sys, const char *fmt, ...)
{
	va_list args;
	char str[1024 * 4];
	char *msg;
	int i, len;

	char timestr[80];
	str_timestamp_format(timestr, sizeof(timestr), FORMAT_SPACE);

	str_format(str, sizeof(str), "[%s][%s]: ", timestr, sys);

	len = str_length(str);
	msg = (char *) str + len;

	va_start(args, fmt);
#if defined(CONF_FAMILY_WINDOWS) && !defined(__GNUC__)
	_vsprintf_p(msg, sizeof(str) - len, fmt, args);
#else
	vsnprintf(msg, sizeof(str) - len, fmt, args);
#endif
	va_end(args);

	for(i = 0; i < num_loggers; i++)
		loggers[i].logger(str, loggers[i].user);
}

#if defined(CONF_FAMILY_WINDOWS)
static void logger_win_console(const char *line, [[maybe_unused]] void *user)
{
#define MAX_LENGTH 1024
#define MAX_LENGTH_ERROR (MAX_LENGTH + 32)

	static const int UNICODE_REPLACEMENT_CHAR = 0xfffd;

	static const char *STR_TOO_LONG = "(str too long)";
	static const char *INVALID_UTF8 = "(invalid utf8)";

	wchar_t wline[MAX_LENGTH_ERROR];
	size_t len = 0;

	const char *read = line;
	const char *error = STR_TOO_LONG;
	while(len < MAX_LENGTH)
	{
		// Read a character. This also advances the read pointer
		int glyph = str_utf8_decode(&read);
		if(glyph < 0)
		{
			// If there was an error decoding the UTF-8 sequence,
			// emit a replacement character. Since the
			// str_utf8_decode function will not work after such
			// an error, end the string here.
			glyph = UNICODE_REPLACEMENT_CHAR;
			error = INVALID_UTF8;
			wline[len] = glyph;
			break;
		}
		else if(glyph == 0)
		{
			// A character code of 0 signals the end of the string.
			error = 0;
			break;
		}
		else if(glyph > 0xffff)
		{
			// Since the windows console does not really support
			// UTF-16, don't mind doing actual UTF-16 encoding,
			// but rather emit a replacement character.
			glyph = UNICODE_REPLACEMENT_CHAR;
		}
		else if(glyph == 0x2022)
		{
			// The 'bullet' character might get converted to a 'beep',
			// so it will be replaced by the 'bullet operator'.
			glyph = 0x2219;
		}

		// Again, since the windows console does not really support
		// UTF-16, but rather something along the lines of UCS-2,
		// simply put the character into the output.
		wline[len++] = glyph;
	}

	if(error)
	{
		read = error;
		while(1)
		{
			// Errors are simple ascii, no need for UTF-8
			// decoding
			char character = *read;
			if(character == 0)
				break;

			dbg_assert(len < MAX_LENGTH_ERROR, "str too short for error");
			wline[len++] = (unsigned char) character;
			read++;
		}
	}

	// Terminate the line
	dbg_assert(len < MAX_LENGTH_ERROR, "str too short for \\r");
	wline[len++] = '\r';
	dbg_assert(len < MAX_LENGTH_ERROR, "str too short for \\n");
	wline[len++] = '\n';

	// Ignore any error that might occur
	WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), wline, len, 0, 0);

#undef MAX_LENGTH
#undef MAX_LENGTH_ERROR
}
#endif

static void logger_stdout(const char *line, void *user)
{
	(void)user;
	printf("%s\n", line);
	fflush(stdout);
}

#if defined(CONF_FAMILY_WINDOWS)
static void logger_win_debugger(const char *line, [[maybe_unused]] void *user)
{
	WCHAR wBuffer[512];
	MultiByteToWideChar(CP_UTF8, 0, line, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
	OutputDebugStringW(wBuffer);
	OutputDebugStringW(L"\n");
}
#endif

static void logger_file(const char *line, void *user)
{
	ASYNCIO *logfile = (ASYNCIO *) user;
	aio_lock(logfile);
	aio_write_unlocked(logfile, line, strlen(line));
	aio_write_newline_unlocked(logfile);
	aio_unlock(logfile);
}

static void logger_stdout_finish(void *user)
{
	ASYNCIO *logfile = (ASYNCIO *) user;
	aio_wait(logfile);
	aio_free(logfile);
}

static void logger_file_finish(void *user)
{
	ASYNCIO *logfile = (ASYNCIO *) user;
	aio_close(logfile);
	logger_stdout_finish(user);
}

void dbg_logger_stdout()
{
#if defined(CONF_FAMILY_WINDOWS)
	if(GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR)
	{
		dbg_logger(logger_win_console, 0, 0);
		return;
	}
#endif
	dbg_logger(logger_stdout, 0, 0);
}

void dbg_logger_debugger()
{
#if defined(CONF_FAMILY_WINDOWS)
	dbg_logger(logger_win_debugger, 0, 0);
#endif
}

void dbg_logger_file(IOHANDLE logfile)
{
	dbg_logger(logger_file, logger_file_finish, aio_new(logfile));
}

#if defined(CONF_FAMILY_WINDOWS)
static DWORD old_console_mode;

void dbg_console_init()
{
	HANDLE handle;
	DWORD console_mode;

	handle = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(handle, &old_console_mode);
	console_mode = old_console_mode & (~ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);
	SetConsoleMode(handle, console_mode);
}
void dbg_console_cleanup()
{
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), old_console_mode);
}

void dbg_console_hide()
{
	FreeConsole();
}
#endif
/* */
