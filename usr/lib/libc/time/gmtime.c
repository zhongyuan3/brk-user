#include <stdint.h>
#include <time.h>

#include "civil.h"
#include "time_impl.h"

/*
 * Core UTC conversion shared by gmtime(), gmtime_r() and the
 * localtime variants.  Fills every struct tm field.
 */
struct tm *__brk_gmtime(time_t t, struct tm *tm)
{
	int64_t days = t / 86400;
	int64_t rem = t % 86400;
	unsigned m, d;
	unsigned weekday;

	if (rem < 0) {
		rem += 86400;
		days--;
	}
	brk_civil_from_days(days, &tm->tm_year, &m, &d);
	tm->tm_year -= 1900;
	tm->tm_mon = (int)m - 1;
	tm->tm_mday = (int)d;
	tm->tm_hour = (int)(rem / 3600);
	rem %= 3600;
	tm->tm_min = (int)(rem / 60);
	tm->tm_sec = (int)(rem % 60);

	/* weekday: 1970-01-01 was a Thursday (4). */
	weekday = (unsigned)((days + 4) % 7);
	tm->tm_wday = (int)weekday;

	/* day of year */
	{
		int doy = d;
		unsigned mm;

		for (mm = 0; mm + 1 < m; ++mm)
			doy += brk_mdays()[mm];
		if (m > 2 && brk_is_leap(tm->tm_year + 1900))
			doy++;
		tm->tm_yday = doy - 1;
	}
	tm->tm_isdst = 0;
	return tm;
}

struct tm *gmtime(const time_t *timep)
{
	static struct tm tm;

	return __brk_gmtime(*timep, &tm);
}

struct tm *localtime(const time_t *timep)
{
	time_t local = *timep + brk_tz_offset();

	/* localtime reuses the same static storage as gmtime in this
	 * implementation, as ISO C permits. */
	return gmtime(&local);
}

time_t mktime(struct tm *tm)
{
	int64_t days;
	int mon, year;
	time_t t;

	/* normalize inputs */
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
	t -= brk_tz_offset();

	/* refresh the derived fields from the normalized value */
	{
		struct tm *norm = gmtime(&t);

		tm->tm_wday = norm->tm_wday;
		tm->tm_yday = norm->tm_yday;
	}
	return t;
}

double difftime(time_t t1, time_t t0)
{
	return (double)t1 - (double)t0;
}
