/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <stdlib.h>
#include <string.h>

#include <base/system/mem.h>

void *mem_alloc(unsigned size)
{
	return malloc(size);
}

void mem_free(void *p)
{
	free(p);
}

void mem_copy(void *dest, const void *source, unsigned size)
{
	memcpy(dest, source, size);
}

void mem_move(void *dest, const void *source, unsigned size)
{
	memmove(dest, source, size);
}

void mem_zero(void *block, unsigned size)
{
	memset(block, 0, size);
}

int mem_comp(const void *a, const void *b, int size)
{
	return memcmp(a, b, size);
}

int mem_has_null(const void *block, unsigned size)
{
	const unsigned char *bytes = (const unsigned char *) block;
	unsigned i;
	for(i = 0; i < size; i++)
	{
		if(bytes[i] == 0)
		{
			return 1;
		}
	}
	return 0;
}
