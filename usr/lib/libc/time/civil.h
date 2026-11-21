#ifndef _CIVIL_H
#define _CIVIL_H

#include <stdint.h>

/*
 * Civil calendar conversions between days-since-epoch and y-m-d,
 * using Howard Hinnant's public-domain algorithms.  They are exact
 * over the whole proleptic Gregorian range tm_year can express.
 * Shared by gmtime.c, mktime.c and timegm.c so the three cannot
 * drift apart.
 */

static inline int64_t brk_days_from_civil(int64_t y, unsigned m, unsigned d)
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

static inline void brk_civil_from_days(int64_t z, int *yp, unsigned *mp,
					unsigned *dp)
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

static inline int brk_is_leap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static inline const unsigned *brk_mdays(void)
{
	static const unsigned mdays[] = { 31, 28, 31, 30, 31, 30, 31,
					  31, 30, 31, 30, 31 };

	return mdays;
}

#endif
