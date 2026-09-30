/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <gtest/gtest.h>

#include <base/tl/sorted_array.h>

TEST(SortedArray, SortEmptyRange)
{
	sorted_array<int> x;
	x.sort_range();
}
