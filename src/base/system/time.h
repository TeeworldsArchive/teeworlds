/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#ifndef BASE_SYSTEM_TIME_H
#define BASE_SYSTEM_TIME_H

#ifdef __cplusplus
extern "C" {
#endif
/* Group: Timer */
#ifdef __GNUC__
/* if compiled with -pedantic-errors it will complain about long
	not being a C90 thing.
*/
__extension__ typedef long long int64;
#else
typedef long long int64;
#endif
/*
	Function: time_get
		Fetches a sample from a high resolution timer.

	Returns:
		Current value of the timer.

	Remarks:
		To know how fast the timer is ticking, see <time_freq>.
*/
int64 time_get();

/*
	Function: time_freq
		Returns the frequency of the high resolution timer.

	Returns:
		Returns the frequency of the high resolution timer.
*/
inline int64 time_freq()
{
	return 1000000;
}

/*
	Function: time_timestamp
		Retrieves the current time as a UNIX timestamp

	Returns:
		The time as a UNIX timestamp
*/
int time_timestamp();

/*
	Function: time_houroftheday
		Retrieves the hours since midnight (0..23)

	Returns:
		The current hour of the day
*/
int time_houroftheday();

enum
{
	SEASON_SPRING = 0,
	SEASON_SUMMER,
	SEASON_AUTUMN,
	SEASON_WINTER,
	SEASON_NEWYEAR
};

/*
	Function: time_season
		Retrieves the current season of the year.

	Returns:
		one of the SEASON_* enum literals
*/
int time_season();

/*
	Function: time_isxmasday
		Checks if it's xmas

	Returns:
		1 - if it's a xmas day
		0 - if not
*/
int time_isxmasday();

/*
	Function: time_iseasterday
		Checks if today is in between Good Friday and Easter Monday (Gregorian calendar)

	Returns:
		1 - if it's egg time
		0 - if not
*/
int time_iseasterday();

#ifdef __cplusplus
}
#endif

#endif
