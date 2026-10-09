/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */

#include <time.h>

#include <base/system/time.h>

/* -----  time ----- */
int64 time_get()
{
	struct timespec ts;
	if(timespec_get(&ts, TIME_UTC) == 0)
	{
		return 0;
	}
	return (int64) ts.tv_sec * 1000000 + (int64) ts.tv_nsec / 1000;
}

int time_timestamp()
{
	return time(0);
}

int time_houroftheday()
{
	time_t time_data;
	struct tm *time_info;

	time(&time_data);
	time_info = localtime(&time_data);
	return time_info->tm_hour;
}

int time_season()
{
	time_t time_data;
	struct tm *time_info;

	time(&time_data);
	time_info = localtime(&time_data);

	if((time_info->tm_mon == 11 && time_info->tm_mday == 31) || (time_info->tm_mon == 0 && time_info->tm_mday == 1))
	{
		return SEASON_NEWYEAR;
	}

	switch(time_info->tm_mon)
	{
		case 11:
		case 0:
		case 1:
			return SEASON_WINTER;
		case 2:
		case 3:
		case 4:
			return SEASON_SPRING;
		case 5:
		case 6:
		case 7:
			return SEASON_SUMMER;
		case 8:
		case 9:
		case 10:
			return SEASON_AUTUMN;
	}
	return SEASON_SPRING; // should never happen
}

int time_isxmasday()
{
	time_t time_data;
	struct tm *time_info;

	time(&time_data);
	time_info = localtime(&time_data);
	if(time_info->tm_mon == 11 && time_info->tm_mday >= 24 && time_info->tm_mday <= 26)
		return 1;
	return 0;
}

int time_iseasterday()
{
	time_t time_data_now, time_data;
	struct tm *time_info;
	int Y, a, b, c, d, e, f, g, h, i, k, L, m, month, day, day_offset;

	time(&time_data_now);
	time_info = localtime(&time_data_now);

	// compute Easter day (Sunday) using https://en.wikipedia.org/w/index.php?title=Computus&oldid=890710285#Anonymous_Gregorian_algorithm
	Y = time_info->tm_year + 1900;
	a = Y % 19;
	b = Y / 100;
	c = Y % 100;
	d = b / 4;
	e = b % 4;
	f = (b + 8) / 25;
	g = (b - f + 1) / 3;
	h = (19 * a + b - d - g + 15) % 30;
	i = c / 4;
	k = c % 4;
	L = (32 + 2 * e + 2 * i - h - k) % 7;
	m = (a + 11 * h + 22 * L) / 451;
	month = (h + L - 7 * m + 114) / 31;
	day = ((h + L - 7 * m + 114) % 31) + 1;

	// (now-1d ≤ easter ≤ now+2d) <=> (easter-2d ≤ now ≤ easter+1d) <=> (Good Friday ≤ now ≤ Easter Monday)
	for(day_offset = -1; day_offset <= 2; day_offset++)
	{
		time_data = time_data_now + day_offset * (60 * 60 * 24);
		time_info = localtime(&time_data);

		if(time_info->tm_mon == month - 1 && time_info->tm_mday == day)
			return 1;
	}
	return 0;
}
