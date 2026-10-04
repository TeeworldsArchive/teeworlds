/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

/*
	Title: Base

	Common prelude for this directory: platform macros and GNUC_ATTRIBUTE.
*/

#ifndef BASE_SYSTEM_BASE_H
#define BASE_SYSTEM_BASE_H

#include <base/detect.h>

#ifdef __GNUC__
#define GNUC_ATTRIBUTE(x) __attribute__(x)
#else
#define GNUC_ATTRIBUTE(x)
#endif

#endif
