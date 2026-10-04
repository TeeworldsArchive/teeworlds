/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include <gtest/gtest.h>

#include <base/system/misc.h>
#include <game/version.h>

extern const char *GIT_SHORTREV_HASH;

TEST(GitRevision, ExistsOrNull)
{
	if(GIT_SHORTREV_HASH)
	{
		ASSERT_STRNE(GIT_SHORTREV_HASH, "");
	}
}
