#include <time.h>

#include "civil.h"

/*
 * timegm(): the inverse of gmtime(), without any timezone.  Like
 * mktime() it normalizes the broken-down fields (month and day roll
 * over) and refreshes tm_wday/tm_yday.
 */
time_t timegm(struct tm *tm)
{
	int64_t days;
	int mon, year;
	time_t t;

	mon = tm->tm_mon;
	year = tm->tm_year + 1900;
	if (mon < 0 || mon > 11) {
		int64_t m = mon;

		year += m / 12;
		m %= 12;
		if (m < 0) {
			m += 12;
			year--;
		}
		mon = (int)m;
		tm->tm_mon = mon;
		tm->tm_year = (int)(year - 1900);
	}
	days = brk_days_from_civil(year, (unsigned)mon + 1,
				   (unsigned)tm->tm_mday);
	t = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 +
	    tm->tm_sec;

	/* refresh the derived fields from the normalized value */
	{
		struct tm *norm = gmtime(&t);

		tm->tm_wday = norm->tm_wday;
		tm->tm_yday = norm->tm_yday;
	}
	return t;
}
