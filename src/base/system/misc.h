/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_MISC_H
#define BASE_SYSTEM_MISC_H

#ifdef __cplusplus
extern "C" {
#endif

int secure_random_init();

/*
	Function: secure_random_uninit
		Uninitializes the secure random module.

	Returns:
		0 - Uninitialization succeeded.
		1 - Uninitialization failed.
*/
int secure_random_uninit();

/*
	Function: secure_random_fill
		Fills the buffer with the specified amount of random bytes.

	Parameters:
		bytes - Pointer to the start of the buffer.
		length - Length of the buffer.
*/
void secure_random_fill(void *bytes, unsigned length);

/*
	Function: pid
		Gets the process ID of the current process

	Returns:
		The process ID of the current process.
*/
int pid();

/*
	Function: cmdline_fix
		Fixes the command line arguments to be encoded in UTF-8 on all
		systems.

	Parameters:
		argc - A pointer to the argc parameter that was passed to the main function.
		argv - A pointer to the argv parameter that was passed to the main function.

	Remarks:
		- You need to call cmdline_free once you're no longer using the
		results.
*/
void cmdline_fix(int *argc, const char ***argv);

/*
	Function: cmdline_free
		Frees memory that was allocated by cmdline_fix.

	Parameters:
		argc - The argc obtained from cmdline_fix.
		argv - The argv obtained from cmdline_fix.

*/
void cmdline_free(int argc, const char **argv);

/*
	Function: bytes_be_to_int
		Packs 4 big endian bytes into an int

	Returns:
		The packed int

	Remarks:
		- Assumes the passed array is 4 bytes
		- Assumes int is 4 bytes
*/
int bytes_be_to_int(const unsigned char *bytes);

/*
	Function: int_to_bytes_be
		Packs an int into 4 big endian bytes

	Remarks:
		- Assumes the passed array is 4 bytes
		- Assumes int is 4 bytes
*/
void int_to_bytes_be(unsigned char *bytes, int value);

/*
	Function: bytes_be_to_uint
		Packs 4 big endian bytes into an unsigned

	Returns:
		The packed unsigned

	Remarks:
		- Assumes the passed array is 4 bytes
		- Assumes unsigned is 4 bytes
*/
unsigned bytes_be_to_uint(const unsigned char *bytes);

/*
	Function: uint_to_bytes_be
		Packs an unsigned into 4 big endian bytes

	Remarks:
		- Assumes the passed array is 4 bytes
		- Assumes unsigned is 4 bytes
*/
void uint_to_bytes_be(unsigned char *bytes, unsigned value);

#ifdef __cplusplus
}
#endif

#endif
