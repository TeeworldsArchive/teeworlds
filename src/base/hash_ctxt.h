/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef BASE_HASH_CTXT_H
#define BASE_HASH_CTXT_H

#include "hash.h"
#include <stdint.h>

#define OPENSSL_API_COMPAT 0x00908000L
#include <openssl/md5.h>
#include <openssl/sha.h>

#ifdef __cplusplus
extern "C" {
#endif

void sha256_init(SHA256_CTX *ctxt);
void sha256_update(SHA256_CTX *ctxt, const void *data, size_t data_len);
SHA256_DIGEST sha256_finish(SHA256_CTX *ctxt);

void md5_init(MD5_CTX *ctxt);
void md5_update(MD5_CTX *ctxt, const void *data, size_t data_len);
MD5_DIGEST md5_finish(MD5_CTX *ctxt);

#ifdef __cplusplus
}
#endif

#endif // BASE_HASH_CTXT_H
