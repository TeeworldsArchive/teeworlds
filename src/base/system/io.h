/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_IO_H
#define BASE_SYSTEM_IO_H

#ifdef __cplusplus
extern "C" {
#endif
/* Group: File IO */
enum
{
	IOFLAG_READ = 1,
	IOFLAG_WRITE = 2,
	IOFLAG_APPEND = 4,
	IOFLAG_SKIP_BOM = 8,

	IOSEEK_START = 0,
	IOSEEK_CUR = 1,
	IOSEEK_END = 2,

	IO_MAX_PATH_LENGTH = 512,
};

typedef struct IOINTERNAL *IOHANDLE;

/*
	Function: io_open
		Opens a file.

	Parameters:
		filename - File to open.
		flags - A set of flags. IOFLAG_READ, IOFLAG_WRITE, IOFLAG_APPEND, IOFLAG_SKIP_BOM.

	Returns:
		Returns a handle to the file on success and 0 on failure.

*/
IOHANDLE io_open(const char *filename, int flags);

/*
	Function: io_read
		Reads data into a buffer from a file.

	Parameters:
		io - Handle to the file to read data from.
		buffer - Pointer to the buffer that will receive the data.
		size - Number of bytes to read from the file.

	Returns:
		Number of bytes read.

*/
unsigned io_read(IOHANDLE io, void *buffer, unsigned size);

/*
	Function: io_read_all
		Reads the rest of the file into a buffer.

	Parameters:
		io - Handle to the file to read data from.
		result - Receives the file's remaining contents.
		result_len - Receives the file's remaining length.

	Remarks:
		- Does NOT guarantee that there are no internal null bytes.
		- The result must be freed after it has been used.
*/
void io_read_all(IOHANDLE io, void **result, unsigned *result_len);

/*
	Function: io_read_all_str
		Reads the rest of the file into a zero-terminated buffer with
		no internal null bytes.

	Parameters:
		io - Handle to the file to read data from.

	Returns:
		The file's remaining contents or null on failure.

	Remarks:
		- Guarantees that there are no internal null bytes.
		- Guarantees that result will contain zero-termination.
		- The result must be freed after it has been used.
*/
char *io_read_all_str(IOHANDLE io);

/*
	Function: io_unread_byte
		"Unreads" a single byte, making it available for future read
		operations.

	Parameters:
		io - Handle to the file to unread the byte from.
		byte - Byte to unread.

	Returns:
		Returns 0 on success and 1 on failure.

*/
unsigned io_unread_byte(IOHANDLE io, unsigned char byte);

/*
	Function: io_skip
		Skips data in a file.

	Parameters:
		io - Handle to the file.
		size - Number of bytes to skip.

	Returns:
		Number of bytes skipped.
*/
unsigned io_skip(IOHANDLE io, int size);

/*
	Function: io_write
		Writes data from a buffer to file.

	Parameters:
		io - Handle to the file.
		buffer - Pointer to the data that should be written.
		size - Number of bytes to write.

	Returns:
		Number of bytes written.
*/
unsigned io_write(IOHANDLE io, const void *buffer, unsigned size);

/*
	Function: io_write_newline
		Writes newline to file.

	Parameters:
		io - Handle to the file.

	Returns:
		Number of bytes written.
*/
unsigned io_write_newline(IOHANDLE io);

/*
	Function: io_seek
		Seeks to a specified offset in the file.

	Parameters:
		io - Handle to the file.
		offset - Offset from pos to stop.
		origin - Position to start searching from.

	Returns:
		Returns 0 on success.
*/
int io_seek(IOHANDLE io, int offset, int origin);

/*
	Function: io_tell
		Gets the current position in the file.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns the current position. -1L if an error occured.
*/
long int io_tell(IOHANDLE io);

/*
	Function: io_length
		Gets the total length of the file. Resetting cursor to the beginning

	Parameters:
		io - Handle to the file.

	Returns:
		Returns the total size. -1L if an error occured.
*/
long int io_length(IOHANDLE io);

/*
	Function: io_close
		Closes a file.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns 0 on success.
*/
int io_close(IOHANDLE io);

/*
	Function: io_flush
		Empties all buffers and writes all pending data.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns 0 on success.
*/
int io_flush(IOHANDLE io);

/*
	Function: io_error
		Checks whether an error occurred during I/O with the file.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns nonzero on error, 0 otherwise.
*/
int io_error(IOHANDLE io);

/*
	Function: io_stdin
		Returns an <IOHANDLE> to the standard input.
*/
IOHANDLE io_stdin();

/*
	Function: io_stdout
		Returns an <IOHANDLE> to the standard output.
*/
IOHANDLE io_stdout();

/*
	Function: io_stderr
		Returns an <IOHANDLE> to the standard error.
*/
IOHANDLE io_stderr();

#ifdef __cplusplus
}
#endif

#endif
