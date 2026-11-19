#include <stdint.h>
#include <time.h>

/*
 * Civil calendar conversions between days-since-epoch and struct tm
 * fields, using Howard Hinnant's public-domain algorithms.  They are
 * exact over the whole proleptic Gregorian range tm_year can express.
 */

/* days from 1970-01-01 to y-m-d (civil_from_days inverse) */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
	int64_t era;
	unsigned yoe, doy, doe;

	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (unsigned)(y - era * 400);
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

/* y-m-d from days since epoch */
static void civil_from_days(int64_t z, int *yp, unsigned *mp, unsigned *dp)
{
	int64_t era;
	unsigned doe, yoe, y, doy, mp_, dp_;

	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = (unsigned)(z - era * 146097);
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = (unsigned)(yoe) + (unsigned)(era * 400);
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp_ = (5 * doy + 2) / 153;
	dp_ = doy - (153 * mp_ + 2) / 5 + 1;
	*yp = (int)y + (mp_ > 9 ? 1 : 0);
	*mp = mp_ + (mp_ < 10 ? 3 : -9);
	*dp = dp_;
}

static int is_leap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static const unsigned mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30,
				  31, 30, 31 };

struct tm *gmtime(const time_t *timep)
{
	static struct tm tm;
	int64_t days = *timep / 86400;
	int64_t rem = *timep % 86400;
	unsigned m, d;
	unsigned weekday;

	if (rem < 0) {
		rem += 86400;
		days--;
	}
	civil_from_days(days, &tm.tm_year, &m, &d);
	tm.tm_year -= 1900;
	tm.tm_mon = (int)m - 1;
	tm.tm_mday = (int)d;
	tm.tm_hour = (int)(rem / 3600);
	rem %= 3600;
	tm.tm_min = (int)(rem / 60);
	tm.tm_sec = (int)(rem % 60);

	/* weekday: 1970-01-01 was a Thursday (4). */
	weekday = (unsigned)((days + 4) % 7);
	tm.tm_wday = (int)weekday;

	/* day of year */
	{
		int doy = d;
		unsigned mm;

		for (mm = 0; mm + 1 < m; ++mm)
			doy += mdays[mm];
		if (m > 2 && is_leap(tm.tm_year + 1900))
			doy++;
		tm.tm_yday = doy - 1;
	}
	tm.tm_isdst = 0;
	return &tm;
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
	days = days_from_civil(year, (unsigned)mon + 1,
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
