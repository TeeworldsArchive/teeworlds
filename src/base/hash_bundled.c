/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#if !defined(CONF_OPENSSL)

#include "hash_ctxt.h"

#include <engine/external/md5/md5.h>

void md5_update(MD5_CTX *ctxt, const void *data, size_t data_len)
{
	md5_append(ctxt, data, data_len);
}

MD5_DIGEST md5_finish(MD5_CTX *ctxt)
{
	MD5_DIGEST result;
	md5_finish_(ctxt, result.data);
	return result;
}

#endif
