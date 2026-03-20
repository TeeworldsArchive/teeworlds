/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef BASE_TL_INPLACE_ARRAY_H
#define BASE_TL_INPLACE_ARRAY_H

#include "range.h"

/*
	Class: inplace_array
		Normal static array class
*/
template<class T, int N>
class inplace_array
{
	void init()
	{
		clear();
	}

	void set_size(int new_size)
	{
		num_elements = new_size > N ? N : new_size;
	}

public:
	typedef plain_range<T> range;

	/*
		Function: array constructor
	*/
	inplace_array()
	{
		init();
	}

	/*
		Function: array copy constructor
	*/
	template<int N2>
	inplace_array(const inplace_array<T, N2> &other)
	{
		copy(other);
	}

	template<int N2>
	void copy(const inplace_array<T, N2> &other)
	{
		init();
		set_size(other.size());
		for(int i = 0; i < size(); i++)
			list[i] = other[i];
	}

	/*
		Function: clear

		Remarks:
			- Invalidates ranges
	*/
	void clear()
	{
		num_elements = 0;
	}

	/*
		Function: size
	*/
	int size() const
	{
		return num_elements;
	}

	/*
		Function: remove_index_fast

		Remarks:
			- Invalidates ranges
	*/
	void remove_index_fast(int index)
	{
		list[index] = list[num_elements - 1];
		set_size(size() - 1);
	}

	/*
		Function: remove_fast

		Remarks:
			- Invalidates ranges
	*/
	void remove_fast(const T &item)
	{
		for(int i = 0; i < size(); i++)
			if(list[i] == item)
			{
				remove_index_fast(i);
				return;
			}
	}

	/*
		Function: remove_index

		Remarks:
			- Invalidates ranges
	*/
	void remove_index(int index)
	{
		for(int i = index + 1; i < num_elements; i++)
			list[i - 1] = list[i];

		set_size(size() - 1);
	}

	/*
		Function: remove

		Remarks:
			- Invalidates ranges
	*/
	bool remove(const T &item)
	{
		for(int i = 0; i < size(); i++)
			if(list[i] == item)
			{
				remove_index(i);
				return true;
			}
		return false;
	}

	/*
		Function: add
			Adds an item to the array.

		Arguments:
			item - Item to add.

		Remarks:
			- Invalidates ranges
			- See remarks about <array> how the array grows.
	*/
	int add(const T &item)
	{
		if(size() + 1 >= N)
			return -1;
		set_size(size() + 1);
		list[num_elements - 1] = item;
		return num_elements - 1;
	}

	/*
		Function: insert
			Inserts an item into the array at a specified location.

		Arguments:
			item - Item to insert.
			r - Range where to insert the item

		Remarks:
			- Invalidates ranges
			- See remarks about <array> how the array grows.
	*/
	int insert(const T &item, range r)
	{
		if(size() + 1 >= N)
			return -1;

		if(r.empty())
			return add(item);

		int index = (int) (&r.front() - list);
		set_size(size() + 1);

		for(int i = num_elements - 1; i > index; i--)
			list[i] = list[i - 1];

		list[index] = item;

		return num_elements - 1;
	}

	/*
		Function: operator[]
	*/
	T &operator[](int index)
	{
		return list[index];
	}

	/*
		Function: const operator[]
	*/
	const T &operator[](int index) const
	{
		return list[index];
	}

	/*
		Function: emplace
	*/
	T &emplace()
	{
		if(size() + 1 >= N)
			return -1;
		set_size(size() + 1);
		return list[num_elements - 1];
	}

	/*
		Function: base_ptr
	*/
	T *base_ptr()
	{
		return list;
	}

	/*
		Function: base_ptr
	*/
	const T *base_ptr() const
	{
		return list;
	}

	/*
		Function: memusage
			Returns how much memory this array is using
	*/
	int memusage() const
	{
		return sizeof(inplace_array);
	}

	/*
		Function: operator=(inplace_array)

		Remarks:
			- Invalidates ranges
	*/
	template<int N2>
	inplace_array &operator=(const inplace_array<T, N2> &other)
	{
		copy(other);
		return *this;
	}

	/*
		Function: all
			Returns a range that contains the whole array.
	*/
	range all() const { return range(list, list + num_elements); }

protected:
	T list[N];
	int num_elements;
};

#endif // BASE_TL_INPLACE_ARRAY_H
