#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * asctime()/ctime() and their reentrant forms.  The fixed 26-byte
 * "Www Mmm dd hh:mm:ss yyyy\n" layout comes from ISO C; the _r
 * variants write into caller storage and return it, the plain forms
 * share one static buffer (they may not share it with each other).
 */
static char *format_tm(char *buf, const struct tm *tm)
{
	static const char wdays[] = "SunMonTueWedThuFriSat";
	static const char mons[] =
		"JanFebMarAprMayJunJulAugSepOctNovDec";
	int year = tm->tm_year + 1900;

	memcpy(buf, wdays + tm->tm_wday * 3, 3);
	buf[3] = ' ';
	memcpy(buf + 4, mons + tm->tm_mon * 3, 3);
	buf[7] = ' ';
	/* ISO: day-of-month is right aligned with a blank fill. */
	buf[8] = tm->tm_mday < 10 ? ' ' : (char)('0' + tm->tm_mday / 10);
	buf[9] = (char)('0' + tm->tm_mday % 10);
	buf[10] = ' ';
	snprintf(buf + 11, 9, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min,
		 tm->tm_sec);
	buf[19] = ' ';
	if (year < 0 || year > 9999) {
		/* ISO reserves the four wide slots for the year. */
		memset(buf + 20, '?', 4);
	} else {
		buf[20] = (char)('0' + year / 1000 % 10);
		buf[21] = (char)('0' + year / 100 % 10);
		buf[22] = (char)('0' + year / 10 % 10);
		buf[23] = (char)('0' + year % 10);
	}
	buf[24] = '\n';
	buf[25] = '\0';
	return buf;
}

char *asctime_r(const struct tm *tm, char *buf)
{
	if (!tm || !buf)
		return NULL;
	if (tm->tm_wday < 0 || tm->tm_wday > 6 ||
	    tm->tm_mon < 0 || tm->tm_mon > 11) {
		errno = EINVAL;
		return NULL;
	}
	return format_tm(buf, tm);
}

char *asctime(const struct tm *tm)
{
	static char buf[26];

	return asctime_r(tm, buf);
}

char *ctime_r(const time_t *timep, char *buf)
{
	struct tm tmb;
	time_t t = *timep + brk_tz_offset();
	struct tm *tm = gmtime(&t);

	tmb = *tm;
	return asctime_r(&tmb, buf);
}

char *ctime(const time_t *timep)
{
	static char buf[26];

	return ctime_r(timep, buf);
}
