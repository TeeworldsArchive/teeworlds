#ifndef BASE_TL_HASHTABLE_H
#define BASE_TL_HASHTABLE_H

#include <base/system.h>
#include "array.h"

class basic_table_function
{
public:
	static unsigned hash(int key) { return key; }
	static unsigned hash(const char *key) { return str_quickhash(key); }

	template<class T>
	static bool equal(T key1, T key2)
	{ return key1 == key2; }
};

class string_nocase_function
{
public:
	static unsigned hash(const char *key) { return str_quickhash(key); }
	static bool equal(const char *key1, const char *key2) { return str_comp_nocase(key1, key2) == 0; }
};

template<class KEY, class T, int TABLESIZE, class FUNCTION = basic_table_function>
class hash_table
{
	struct entry
	{
		T data;
		KEY key;
	};
	array<entry> tables[TABLESIZE];

	unsigned hash(KEY key) const
	{
		return FUNCTION::hash(key) % TABLESIZE;
	}

public:
	hash_table() {}

	void clear()
	{
		for(int i = 0; i < TABLESIZE; i++)
			tables[i].clear();
	}

	void clear_size()
	{
		for(int i = 0; i < TABLESIZE; i++)
			tables[i].clear_size();
	}
	/*
		Function: operator[]
	*/
	T *operator[](KEY target_key)
	{
		return get(target_key);
	}

	/*
		Function: const operator[]
	*/
	const T *operator[](KEY target_key) const
	{
		return get(target_key);
	}

	const T *get(KEY target_key) const
	{
		unsigned index = hash(target_key);
		for(int i = 0; i < tables[index].size(); i++)
		{
			if(FUNCTION::equal(target_key, tables[index][i].key))
				return &tables[index][i].data;
		}
		return 0;
	}

	T *get(KEY target_key)
	{
		unsigned index = hash(target_key);
		for(int i = 0; i < tables[index].size(); i++)
		{
			if(FUNCTION::equal(target_key, tables[index][i].key))
				return &tables[index][i].data;
		}
		return 0;
	}

	T *set(KEY target_key, const T &data)
	{
		unsigned index = hash(target_key);
		for(int i = 0; i < tables[index].size(); i++)
		{
			if(FUNCTION::equal(target_key, tables[index][i].key))
			{
				tables[index][i].data = data;
				return &tables[index][i].data;
			}
		}
		entry &new_entry = tables[index].emplace();
		new_entry.key = target_key;
		new_entry.data = data;
		return &new_entry.data;
	}

	void remove(KEY target_key)
	{
		unsigned index = hash(target_key);
		for(int i = 0; i < tables[index].size(); i++)
		{
			if(FUNCTION::equal(target_key, tables[index][i].key))
			{
				tables[index].remove_index_fast(i);
				break;
			}
		}
	}

	int size()
	{
		int total = 0;
		for(int i = 0; i < TABLESIZE; i++)
			total += tables[i].size();
		return total;
	}

	typedef void (*foreach_function)(T &data, void *user);
	void for_each(foreach_function func, void *user)
	{
		for(int i = 0; i < TABLESIZE; i++)
		{
			for(int j = 0; j < tables[i].size(); j++)
			{
				func(tables[i][j].data, user);
			}
		}
	}
};

#endif // BASE_TL_HASH_TABLE_H
