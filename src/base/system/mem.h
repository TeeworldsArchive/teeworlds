/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_MEM_H
#define BASE_SYSTEM_MEM_H

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Memory */

/*
	Function: mem_alloc
		Allocates memory.

	Parameters:
		size - Size of the needed block.

	Returns:
		Returns a pointer to the newly allocated block. Returns a
		null pointer if the memory couldn't be allocated.

	Remarks:
		- The behavior when passing 0 as size is unspecified.

	See Also:
		<mem_free>
*/
void *mem_alloc(unsigned size);

/*
	Function: mem_free
		Frees a block allocated through <mem_alloc>.

	See Also:
		<mem_alloc>
*/
void mem_free(void *block);

/*
	Function: mem_copy
		Copies a a memory block.

	Parameters:
		dest - Destination.
		source - Source to copy.
		size - Size of the block to copy.

	Remarks:
		- This functions DOES NOT handles cases where source and
		destination is overlapping.

	See Also:
		<mem_move>
*/
void mem_copy(void *dest, const void *source, unsigned size);

/*
	Function: mem_move
		Copies a a memory block

	Parameters:
		dest - Destination
		source - Source to copy
		size - Size of the block to copy

	Remarks:
		- This functions handles cases where source and destination
		is overlapping

	See Also:
		<mem_copy>
*/
void mem_move(void *dest, const void *source, unsigned size);

/*
	Function: mem_zero
		Sets a complete memory block to 0

	Parameters:
		block - Pointer to the block to zero out
		size - Size of the block
*/
void mem_zero(void *block, unsigned size);

/*
	Function: mem_comp
		Compares two blocks of memory

	Parameters:
		a - First block of data
		b - Second block of data
		size - Size of the data to compare

	Returns:
		<0 - Block a is lesser then block b
		0 - Block a is equal to block b
		>0 - Block a is greater then block b
*/
int mem_comp(const void *a, const void *b, int size);

/*
	Function: mem_has_null
		Checks whether a block of memory contains null bytes.

	Parameters:
		block - Pointer to the block to check for nulls.
		size - Size of the block.

	Returns:
		1 - The block has a null byte.
		0 - The block does not have a null byte.
*/
int mem_has_null(const void *block, unsigned size);

#ifdef __cplusplus
}
#endif

#endif
