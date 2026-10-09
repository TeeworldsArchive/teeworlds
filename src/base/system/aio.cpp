/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <stdlib.h>

#include <base/system/aio.h>
#include <base/system/base.h>
#include <base/system/lock.h>
#include <base/system/mem.h>
#include <base/system/semaphore.h>
#include <base/system/thread.h>

#define ASYNC_BUFSIZE 8 * 1024
#define ASYNC_LOCAL_BUFSIZE 64 * 1024

struct ASYNCIO
{
	LOCK lock;
	IOHANDLE io;
	SEMAPHORE sphore;
	void *thread;

	unsigned char *buffer;
	unsigned int buffer_size;
	unsigned int read_pos;
	unsigned int write_pos;

	int error;
	unsigned char finish;
	unsigned char refcount;
};

enum
{
	ASYNCIO_RUNNING,
	ASYNCIO_CLOSE,
	ASYNCIO_EXIT,
};

struct BUFFERS
{
	unsigned char *buf1;
	unsigned int len1;
	unsigned char *buf2;
	unsigned int len2;
};

static void buffer_ptrs(ASYNCIO *aio, struct BUFFERS *buffers)
{
	mem_zero(buffers, sizeof(*buffers));
	if(aio->read_pos < aio->write_pos)
	{
		buffers->buf1 = aio->buffer + aio->read_pos;
		buffers->len1 = aio->write_pos - aio->read_pos;
	}
	else if(aio->read_pos > aio->write_pos)
	{
		buffers->buf1 = aio->buffer + aio->read_pos;
		buffers->len1 = aio->buffer_size - aio->read_pos;
		buffers->buf2 = aio->buffer;
		buffers->len2 = aio->write_pos;
	}
}

static void aio_handle_free_and_unlock(ASYNCIO *aio)
{
	int do_free;
	aio->refcount--;

	do_free = aio->refcount == 0;
	lock_unlock(aio->lock);
	if(do_free)
	{
		free(aio->buffer);
		sphore_destroy(&aio->sphore);
		lock_destroy(aio->lock);
		free(aio);
	}
}

static void aio_thread(void *user)
{
	ASYNCIO *aio = (ASYNCIO *) user;

	lock_wait(aio->lock);
	while(1)
	{
		struct BUFFERS buffers;
		int result_io_error;
		unsigned char local_buffer[ASYNC_LOCAL_BUFSIZE];
		unsigned int local_buffer_len = 0;

		if(aio->read_pos == aio->write_pos)
		{
			if(aio->finish != ASYNCIO_RUNNING)
			{
				if(aio->finish == ASYNCIO_CLOSE)
				{
					io_close(aio->io);
				}
				aio_handle_free_and_unlock(aio);
				break;
			}
			lock_unlock(aio->lock);
			sphore_wait(&aio->sphore);
			lock_wait(aio->lock);
			continue;
		}

		buffer_ptrs(aio, &buffers);
		if(buffers.buf1)
		{
			if(buffers.len1 > sizeof(local_buffer) - local_buffer_len)
			{
				buffers.len1 = sizeof(local_buffer) - local_buffer_len;
			}
			mem_copy(local_buffer + local_buffer_len, buffers.buf1, buffers.len1);
			local_buffer_len += buffers.len1;
			if(buffers.buf2)
			{
				if(buffers.len2 > sizeof(local_buffer) - local_buffer_len)
				{
					buffers.len2 = sizeof(local_buffer) - local_buffer_len;
				}
				mem_copy(local_buffer + local_buffer_len, buffers.buf2, buffers.len2);
				local_buffer_len += buffers.len2;
			}
		}
		aio->read_pos = (aio->read_pos + buffers.len1 + buffers.len2) % aio->buffer_size;
		lock_unlock(aio->lock);

		io_write(aio->io, local_buffer, local_buffer_len);
		io_flush(aio->io);
		result_io_error = io_error(aio->io);

		lock_wait(aio->lock);
		aio->error = result_io_error;
	}
}

ASYNCIO *aio_new(IOHANDLE io)
{
	ASYNCIO *aio = (ASYNCIO *) malloc(sizeof(*aio));
	if(!aio)
	{
		return 0;
	}
	aio->io = io;
	aio->lock = lock_create();
	sphore_init(&aio->sphore);
	aio->thread = 0;

	aio->buffer = (unsigned char *) malloc(ASYNC_BUFSIZE);
	if(!aio->buffer)
	{
		sphore_destroy(&aio->sphore);
		lock_destroy(aio->lock);
		free(aio);
		return 0;
	}
	aio->buffer_size = ASYNC_BUFSIZE;
	aio->read_pos = 0;
	aio->write_pos = 0;
	aio->error = 0;
	aio->finish = ASYNCIO_RUNNING;
	aio->refcount = 2;

	aio->thread = thread_init(aio_thread, aio);
	if(!aio->thread)
	{
		free(aio->buffer);
		sphore_destroy(&aio->sphore);
		lock_destroy(aio->lock);
		free(aio);
		return 0;
	}
	return aio;
}

static unsigned int buffer_len(ASYNCIO *aio)
{
	if(aio->write_pos >= aio->read_pos)
	{
		return aio->write_pos - aio->read_pos;
	}
	else
	{
		return aio->buffer_size + aio->write_pos - aio->read_pos;
	}
}

static unsigned int next_buffer_size(unsigned int cur_size, unsigned int need_size)
{
	while(cur_size < need_size)
	{
		cur_size *= 2;
	}
	return cur_size;
}

void aio_lock(ASYNCIO *aio)
{
	lock_wait(aio->lock);
}

void aio_unlock(ASYNCIO *aio)
{
	lock_unlock(aio->lock);
	sphore_signal(&aio->sphore);
}

void aio_write_unlocked(ASYNCIO *aio, const void *buffer, unsigned size)
{
	unsigned int remaining;
	remaining = aio->buffer_size - buffer_len(aio);

	// Don't allow full queue to distinguish between empty and full queue.
	if(size < remaining)
	{
		unsigned int remaining_contiguous = aio->buffer_size - aio->write_pos;
		if(size > remaining_contiguous)
		{
			mem_copy(aio->buffer + aio->write_pos, buffer, remaining_contiguous);
			size -= remaining_contiguous;
			buffer = ((unsigned char *) buffer) + remaining_contiguous;
			aio->write_pos = 0;
		}
		mem_copy(aio->buffer + aio->write_pos, buffer, size);
		aio->write_pos = (aio->write_pos + size) % aio->buffer_size;
	}
	else
	{
		// Add 1 so the new buffer isn't completely filled.
		unsigned int new_written = buffer_len(aio) + size + 1;
		unsigned int next_size = next_buffer_size(aio->buffer_size, new_written);
		unsigned int next_len = 0;
		unsigned char *next_buffer = (unsigned char *) malloc(next_size);

		struct BUFFERS buffers;
		buffer_ptrs(aio, &buffers);
		if(buffers.buf1)
		{
			mem_copy(next_buffer + next_len, buffers.buf1, buffers.len1);
			next_len += buffers.len1;
			if(buffers.buf2)
			{
				mem_copy(next_buffer + next_len, buffers.buf2, buffers.len2);
				next_len += buffers.len2;
			}
		}
		mem_copy(next_buffer + next_len, buffer, size);
		next_len += size;

		free(aio->buffer);
		aio->buffer = next_buffer;
		aio->buffer_size = next_size;
		aio->read_pos = 0;
		aio->write_pos = next_len;
	}
}

void aio_write(ASYNCIO *aio, const void *buffer, unsigned size)
{
	aio_lock(aio);
	aio_write_unlocked(aio, buffer, size);
	aio_unlock(aio);
}

void aio_write_newline_unlocked(ASYNCIO *aio)
{
#if defined(CONF_FAMILY_WINDOWS)
	aio_write_unlocked(aio, "\r\n", 2);
#else
	aio_write_unlocked(aio, "\n", 1);
#endif
}

void aio_write_newline(ASYNCIO *aio)
{
	aio_lock(aio);
	aio_write_newline_unlocked(aio);
	aio_unlock(aio);
}

int aio_error(ASYNCIO *aio)
{
	int result;
	lock_wait(aio->lock);
	result = aio->error;
	lock_unlock(aio->lock);
	return result;
}

void aio_free(ASYNCIO *aio)
{
	lock_wait(aio->lock);
	if(aio->thread)
	{
		thread_detach(aio->thread);
		aio->thread = 0;
	}
	aio_handle_free_and_unlock(aio);
}

void aio_close(ASYNCIO *aio)
{
	lock_wait(aio->lock);
	aio->finish = ASYNCIO_CLOSE;
	lock_unlock(aio->lock);
	sphore_signal(&aio->sphore);
}

void aio_wait(ASYNCIO *aio)
{
	void *thread;
	lock_wait(aio->lock);
	thread = aio->thread;
	aio->thread = 0;
	if(aio->finish == ASYNCIO_RUNNING)
	{
		aio->finish = ASYNCIO_EXIT;
	}
	lock_unlock(aio->lock);
	sphore_signal(&aio->sphore);
	thread_wait(thread);
}
