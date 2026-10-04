/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_SEMAPHORE_H
#define BASE_SYSTEM_SEMAPHORE_H

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Semaphores */
typedef struct SEMINTERNAL *SEMAPHORE;

void sphore_init(SEMAPHORE *sem);
void sphore_wait(SEMAPHORE *sem);
void sphore_signal(SEMAPHORE *sem);
void sphore_destroy(SEMAPHORE *sem);

#ifdef __cplusplus
}
#endif

#endif
