/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#include "test.h"
#include <gtest/gtest.h>

#include <base/system/aio.h>
#include <base/system/fs.h>

TEST(Filesystem, CreateCloseDelete)
{
	CTestInfo Info;

	IOHANDLE File = io_open(Info.m_aFilename, IOFLAG_WRITE);
	ASSERT_TRUE(File);
	EXPECT_FALSE(io_close(File));
	EXPECT_FALSE(fs_remove(Info.m_aFilename));
}
