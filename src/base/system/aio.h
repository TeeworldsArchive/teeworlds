/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_AIO_H
#define BASE_SYSTEM_AIO_H

#include <base/system/io.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Asychronous I/O */

typedef struct ASYNCIO ASYNCIO;
/*
	Function: aio_new
		Wraps a <IOHANDLE> for asynchronous writing.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns the handle for asynchronous writing.

*/
ASYNCIO *aio_new(IOHANDLE io);

/*
	Function: aio_lock
		Locks the ASYNCIO structure so it can't be written into by
		other threads.

	Parameters:
		aio - Handle to the file.
*/
void aio_lock(ASYNCIO *aio);

/*
	Function: aio_unlock
		Unlocks the ASYNCIO structure after finishing the contiguous
		write.

	Parameters:
		aio - Handle to the file.
*/
void aio_unlock(ASYNCIO *aio);

/*
	Function: aio_write
		Queues a chunk of data for writing.

	Parameters:
		aio - Handle to the file.
		buffer - Pointer to the data that should be written.
		size - Number of bytes to write.

*/
void aio_write(ASYNCIO *aio, const void *buffer, unsigned size);

/*
	Function: aio_write_newline
		Queues a newline for writing.

	Parameters:
		aio - Handle to the file.

*/
void aio_write_newline(ASYNCIO *aio);

/*
	Function: aio_write_unlocked
		Queues a chunk of data for writing. The ASYNCIO struct must be
		locked using `aio_lock` first.

	Parameters:
		aio - Handle to the file.
		buffer - Pointer to the data that should be written.
		size - Number of bytes to write.

*/
void aio_write_unlocked(ASYNCIO *aio, const void *buffer, unsigned size);

/*
	Function: aio_write_newline_unlocked
		Queues a newline for writing. The ASYNCIO struct must be locked
		using `aio_lock` first.

	Parameters:
		aio - Handle to the file.

*/
void aio_write_newline_unlocked(ASYNCIO *aio);

/*
	Function: aio_error
		Checks whether errors have occurred during the asynchronous
		writing.

		Call this function regularly to see if there are errors. Call
		this function after <aio_wait> to see if the process of writing
		to the file succeeded.

	Parameters:
		aio - Handle to the file.

	Returns:
		Returns 0 if no error occurred, and nonzero on error.

*/
int aio_error(ASYNCIO *aio);

/*
	Function: aio_close
		Queues file closing.

	Parameters:
		aio - Handle to the file.

*/
void aio_close(ASYNCIO *aio);

/*
	Function: aio_wait
		Wait for the asynchronous operations to complete.

	Parameters:
		aio - Handle to the file.

*/
void aio_wait(ASYNCIO *aio);

/*
	Function: aio_free
		Frees the resources associated to the asynchronous file handle.

	Parameters:
		aio - Handle to the file.

*/
void aio_free(ASYNCIO *aio);

#ifdef __cplusplus
}
#endif

#endif
