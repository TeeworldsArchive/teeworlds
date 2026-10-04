/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_DEBUG_H
#define BASE_SYSTEM_DEBUG_H

#include <base/system/base.h>
#include <base/system/io.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Debug */
/*

	Function: dbg_assert
		Breaks into the debugger based on a test.

	Parameters:
		test - Result of the test.
		msg - Message that should be printed if the test fails.

	See Also:
		<dbg_break>
*/
void dbg_assert(int test, const char *msg);
#define dbg_assert(test, msg) dbg_assert_imp(__FILE__, __LINE__, test, msg)
void dbg_assert_imp(const char *filename, int line, int test, const char *msg);

#ifdef __clang_analyzer__
#include <assert.h>
#undef dbg_assert
#define dbg_assert(test, msg) assert(test)
#endif

/*
	Function: dbg_break
		Breaks into the debugger.

	See Also:
		<dbg_assert>
*/
void dbg_break();

/*
	Function: dbg_msg

	Prints a debug message.

	Parameters:
		sys - A string that describes what system the message belongs to
		fmt - A printf styled format string.

	See Also:
		<dbg_assert>
*/
void dbg_msg(const char *sys, const char *fmt, ...)
	GNUC_ATTRIBUTE((format(printf, 2, 3)));

typedef void (*DBG_LOGGER)(const char *line, void *user);
typedef void (*DBG_LOGGER_FINISH)(void *user);
void dbg_logger(DBG_LOGGER logger, DBG_LOGGER_FINISH finish, void *user);

void dbg_logger_stdout();
void dbg_logger_debugger();
void dbg_logger_file(IOHANDLE logfile);

#if defined(CONF_FAMILY_WINDOWS)
void dbg_console_init();
void dbg_console_cleanup();
void dbg_console_hide();
#endif

#ifdef __cplusplus
}
#endif

#endif
