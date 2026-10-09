/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef BASE_TL_ALLOCATOR_H
#define BASE_TL_ALLOCATOR_H

template<class T>
class allocator_default
{
public:
	static T *alloc() { return new T; }
	static void free(T *p) { delete p; }

	static T *alloc_array(int size) { return new T[size]; }
	static void free_array(T *p) { delete[] p; }
};

/*
	Allocator: allocator_non_owning

	Frees the array buffer but not the elements, for containers of pointers
	that must not take ownership of what they point at.
*/
template<class T>
class allocator_non_owning
{
public:
	static T *alloc() { return new T; }
	static void free(T *) {}

	static T *alloc_array(int size) { return new T[size]; }
	static void free_array(T *p) { delete[] p; }
};

#endif // BASE_TL_ALLOCATOR_H
