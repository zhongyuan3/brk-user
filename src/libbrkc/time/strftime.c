#include <stddef.h>
#include <stdio.h>
#include <time.h>

/*
 * strftime(): the common conversion set used by Lua's os.date and by
 * ordinary logs.  Width/E flags are accepted and ignored for numeric
 * fields (the maximum field width below covers %Y).
 */

static const char wday_name[7][10] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
	"Saturday"
};
static const char wday_abbr[7][4] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char mon_name[12][10] = {
	"January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December"
};
static const char mon_abbr[12][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
	"Oct", "Nov", "Dec"
};

static int put(char *s, size_t max, size_t *used, const char *str)
{
	size_t len = 0;

	while (str[len]) {
		if (*used + 1 >= max)
			return 0;
		s[(*used)++] = str[len++];
	}
	return 1;
}

static int putn(char *s, size_t max, size_t *used, long v, int pad,
		int width)
{
	char tmp[32];
	int n = 0;

	if (v < 0) {
		if (*used + 1 >= max)
			return 0;
		s[(*used)++] = '-';
		v = -v;
	}
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n < width && n < (int)sizeof(tmp))
		tmp[n++] = (char)pad;
	while (n > 0) {
		if (*used + 1 >= max)
			return 0;
		s[(*used)++] = tmp[--n];
	}
	return 1;
}

size_t strftime(char *s, size_t max, const char *format,
		const struct tm *tm)
{
	size_t used = 0;

	if (max == 0)
		return 0;
	s[0] = 0;
	for (; *format; ++format) {
		if (*format != '%') {
			if (used + 1 >= max)
				return 0;
			s[used++] = *format;
			continue;
		}
		++format;
		/* skip width/E/O modifiers: %3d, %Ey etc. behave like %d */
		while ((*format >= '0' && *format <= '9') || *format == 'E' ||
		       *format == 'O' || *format == '_' || *format == '-')
			++format;
		switch (*format) {
		case 'a':
			if (!put(s, max, &used, wday_abbr[tm->tm_wday]))
				return 0;
			break;
		case 'A':
			if (!put(s, max, &used, wday_name[tm->tm_wday]))
				return 0;
			break;
		case 'b': case 'h':
			if (!put(s, max, &used, mon_abbr[tm->tm_mon]))
				return 0;
			break;
		case 'B':
			if (!put(s, max, &used, mon_name[tm->tm_mon]))
				return 0;
			break;
		case 'c': {
			char buf[64];

			snprintf(buf, sizeof(buf),
				 "%04d-%02d-%02d %02d:%02d:%02d",
				 tm->tm_year + 1900, tm->tm_mon + 1,
				 tm->tm_mday, tm->tm_hour, tm->tm_min,
				 tm->tm_sec);
			if (!put(s, max, &used, buf))
				return 0;
			break;
		}
		case 'C':
			if (!putn(s, max, &used, (tm->tm_year + 1900) / 100,
				  '0', 2))
				return 0;
			break;
		case 'd':
			if (!putn(s, max, &used, tm->tm_mday, '0', 2))
				return 0;
			break;
		case 'D': {
			char buf[16];

			snprintf(buf, sizeof(buf), "%02d/%02d/%02d",
				 tm->tm_mon + 1, tm->tm_mday,
				 (tm->tm_year + 1900) % 100);
			if (!put(s, max, &used, buf))
				return 0;
			break;
		}
		case 'e':
			if (!putn(s, max, &used, tm->tm_mday, ' ', 2))
				return 0;
			break;
		case 'F': {
			char buf[24];

			snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
				 tm->tm_year + 1900, tm->tm_mon + 1,
				 tm->tm_mday);
			if (!put(s, max, &used, buf))
				return 0;
			break;
		}
		case 'H':
			if (!putn(s, max, &used, tm->tm_hour, '0', 2))
				return 0;
			break;
		case 'I': {
			int h = tm->tm_hour % 12;

			if (h == 0)
				h = 12;
			if (!putn(s, max, &used, h, '0', 2))
				return 0;
			break;
		}
		case 'j':
			if (!putn(s, max, &used, tm->tm_yday + 1, '0', 3))
				return 0;
			break;
		case 'm':
			if (!putn(s, max, &used, tm->tm_mon + 1, '0', 2))
				return 0;
			break;
		case 'M':
			if (!putn(s, max, &used, tm->tm_min, '0', 2))
				return 0;
			break;
		case 'n':
			if (!put(s, max, &used, "\n"))
				return 0;
			break;
		case 'p':
			if (!put(s, max, &used, tm->tm_hour < 12 ? "AM" : "PM"))
				return 0;
			break;
		case 'P':
			if (!put(s, max, &used, tm->tm_hour < 12 ? "am" : "pm"))
				return 0;
			break;
		case 'r': {
			char buf[16];
			int h = tm->tm_hour % 12;

			if (h == 0)
				h = 12;
			snprintf(buf, sizeof(buf), "%02d:%02d:%02d %s", h,
				 tm->tm_min, tm->tm_sec,
				 tm->tm_hour < 12 ? "AM" : "PM");
			if (!put(s, max, &used, buf))
				return 0;
			break;
		}
		case 'R': {
			char buf[8];

			snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour,
				 tm->tm_min);
			if (!put(s, max, &used, buf))
				return 0;
			break;
		}
		case 's': {
			time_t t = mktime((struct tm *)tm);

			if (!putn(s, max, &used, (long)t, ' ', 0))
				return 0;
			break;
		}
		case 'S':
			if (!putn(s, max, &used, tm->tm_sec, '0', 2))
				return 0;
			break;
		case 't':
			if (!put(s, max, &used, "\t"))
				return 0;
			break;
		case 'T': {
			char buf[16];

			snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
				 tm->tm_hour, tm->tm_min, tm->tm_sec);
			if (!put(s, max, &used, buf))
				return 0;
			break;
		}
		case 'u':
			if (!putn(s, max, &used, tm->tm_wday ? tm->tm_wday : 7,
				  '0', 1))
				return 0;
			break;
		case 'w':
			if (!putn(s, max, &used, tm->tm_wday, '0', 1))
				return 0;
			break;
		case 'x': {
			char buf[24];

			snprintf(buf, sizeof(buf), "%02d/%02d/%02d",
				 tm->tm_mon + 1, tm->tm_mday,
				 (tm->tm_year + 1900) % 100);
			if (!put(s, max, &used, buf))
				return 0;
			break;
		}
		case 'X': {
			char buf[16];

			snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
				 tm->tm_hour, tm->tm_min, tm->tm_sec);
			if (!put(s, max, &used, buf))
				return 0;
			break;
		}
		case 'y':
			if (!putn(s, max, &used, (tm->tm_year + 1900) % 100,
				  '0', 2))
				return 0;
			break;
		case 'Y':
			if (!putn(s, max, &used, tm->tm_year + 1900, '0', 0))
				return 0;
			break;
		case 'z': {
			long off = brk_tz_offset();

			if (!putn(s, max, &used, off / 60, '0', 0))
				return 0;
			break;
		}
		case 'Z':
			if (!put(s, max, &used, "UTC"))
				return 0;
			break;
		case '%':
			if (used + 1 >= max)
				return 0;
			s[used++] = '%';
			break;
		default:
			/* unknown conversion: emit it verbatim */
			if (used + 2 >= max)
				return 0;
			s[used++] = '%';
			if (*format)
				s[used++] = *format;
			break;
		}
	}
	s[used] = 0;
	return used;
}
