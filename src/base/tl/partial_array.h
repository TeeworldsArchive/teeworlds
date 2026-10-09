/* (c) Teeworlds Archive Project Contributors. See license.txt. */
#ifndef BASE_TL_PARTIAL_ARRAY_H
#define BASE_TL_PARTIAL_ARRAY_H

#include <base/system/debug.h>
#include "allocator.h"

/*
	Class: partial_array
		A sparse array for large, mostly empty index spaces.

	Holds a PARTS-sized table of pointers to C arrays of PART_SIZE elements,
	covering the index space [0, PARTS * PART_SIZE). Only that table is
	allocated up front; a part is allocated the first time an index in it is
	touched. Clients using a small prefix of the space therefore allocate one
	part, however large PARTS is.

	Remarks:
		- Indices outside [0, PARTS * PART_SIZE) are not supported.
		- No sort(), add() or insert(): elements live at a fixed index.
		- remove_index() frees the whole part the index belongs to, because
		  elements are kept in C arrays and cannot be reclaimed individually.
*/
template<class T, int PARTS, int PART_SIZE, class ALLOCATOR = allocator_default<T>>
class partial_array : private ALLOCATOR
{
	void init()
	{
		for(int i = 0; i < PARTS; i++)
			parts[i] = 0x0;
	}

	void free_all()
	{
		for(int i = 0; i < PARTS; i++)
		{
			if(parts[i])
			{
				ALLOCATOR::free_array(parts[i]);
				parts[i] = 0x0;
			}
		}
	}

public:
	static const int CAPACITY = PARTS * PART_SIZE;

	/*
		Function: partial_array constructor
	*/
	partial_array()
	{
		init();
	}

	/*
		Function: partial_array destructor

		Remarks:
			- Destroys the elements of every allocated part.
	*/
	~partial_array()
	{
		free_all();
	}

	partial_array(const partial_array &other) = delete;
	partial_array &operator=(const partial_array &other) = delete;

	/*
		Function: size
			Returns the number of index slots, PARTS * PART_SIZE.
	*/
	int size() const
	{
		return CAPACITY;
	}

	/*
		Function: init_part
			Allocates the part holding index, unless it already is.

		Returns:
			1 on success, 0 when index is out of range or allocation failed.
	*/
	int init_part(int index)
	{
		if(index < 0 || index >= CAPACITY)
			return 0;

		T **part = &parts[index / PART_SIZE];
		if(!*part)
		{
			*part = ALLOCATOR::alloc_array(PART_SIZE);
			if(!*part)
				return 0;

			// alloc_array default-initialises, which leaves the members of a
			// plain struct indeterminate. Value-initialise instead, so reading
			// an index before writing it cannot see garbage.
			for(int i = 0; i < PART_SIZE; i++)
				(*part)[i] = T();
		}
		return 1;
	}

	/*
		Function: part
			Returns the part holding index, allocating it on demand.

		Returns:
			0 when index is out of range or allocation failed.
	*/
	T *part(int index)
	{
		if(!init_part(index))
			return 0;
		return parts[index / PART_SIZE];
	}

	/*
		Function: part
	*/
	const T *part(int index) const
	{
		if(index < 0 || index >= CAPACITY)
			return 0;
		return parts[index / PART_SIZE];
	}

	/*
		Function: exists
			Returns whether index is backed by allocated storage.

		Remarks:
			- Unlike array, an index has no storage until something is written
			  to it. Never allocates, so it is safe to ask about an index taken
			  from the network.
	*/
	bool exists(int index) const
	{
		return part(index) != 0;
	}

	/*
		Function: operator[]
			Accesses an element, allocating its part on demand.
	*/
	T &operator[](int index)
	{
		T *p = part(index);
		dbg_assert(p != 0, "partial_array index out of range");
		return p[index % PART_SIZE];
	}

	/*
		Function: operator[]
	*/
	const T &operator[](int index) const
	{
		const T *p = part(index);
		dbg_assert(p != 0, "partial_array index out of range");
		return p[index % PART_SIZE];
	}

	/*
		Function: get
			Returns a pointer to the element, or 0 when its part is not
			allocated or index is out of range.

		Remarks:
			- Unlike operator[], this never allocates, so it is safe to call
			  with an index taken from the network.
	*/
	T *get(int index)
	{
		return const_cast<T *>(static_cast<const partial_array *>(this)->get(index));
	}

	/*
		Function: get
	*/
	const T *get(int index) const
	{
		const T *p = part(index);
		return p ? &p[index % PART_SIZE] : 0;
	}

	/*
		Function: remove_index
			Frees the part holding index, destroying its elements.

		Remarks:
			- Invalidates every index in that part, not just index.
	*/
	void remove_index(int index)
	{
		if(index < 0 || index >= CAPACITY)
			return;

		T **part = &parts[index / PART_SIZE];
		if(*part)
		{
			ALLOCATOR::free_array(*part);
			*part = 0x0;
		}
	}

	/*
		Function: clear
			Frees every allocated part, destroying its elements.
	*/
	void clear()
	{
		free_all();
	}

	/*
		Function: clear_size
			Resets every element to a default-constructed value but keeps the
			parts allocated for reuse.
	*/
	void clear_size()
	{
		for(int p = 0; p < PARTS; p++)
		{
			if(!parts[p])
				continue;
			for(int i = 0; i < PART_SIZE; i++)
				parts[p][i] = T();
		}
	}

	/*
		Function: memusage
			Returns how many bytes this container uses, including its parts.
	*/
	int memusage() const
	{
		int usage = sizeof(partial_array);
		for(int i = 0; i < PARTS; i++)
		{
			if(parts[i])
				usage += sizeof(T) * PART_SIZE;
		}
		return usage;
	}

protected:
	T *parts[PARTS];
};

#endif // BASE_TL_PARTIAL_ARRAY_H
