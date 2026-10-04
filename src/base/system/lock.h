/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_LOCK_H
#define BASE_SYSTEM_LOCK_H

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Locks */
typedef void *LOCK;

LOCK lock_create();
void lock_destroy(LOCK lock);

int lock_trylock(LOCK lock);
int lock_wait(LOCK lock);
int lock_unlock(LOCK lock);

#ifdef __cplusplus
}
#endif

#endif
