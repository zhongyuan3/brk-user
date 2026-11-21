#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/*
 * strptime(): the parsing counterpart of strftime, filling the
 * caller's struct tm.  Fields the format does not mention are left
 * untouched; the return value points past the last consumed input
 * character, or NULL when the input does not match the format.
 *
 * Supported conversions: %a/%A weekday names, %b/%B/%h month
 * names, %c, %C century, %d/%e day, %D (%m/%d/%y), %F
 * (%Y-%m-%d), %H hour, %I 12-hour clock, %j day of year, %m month,
 * %M minute, %n/%t whitespace, %p am/pm marker, %r (%I:%M:%S %p),
 * %R (%H:%M), %S second, %T (%H:%M:%S), %u/%w weekday number, %x/%X
 * (date/time composites), %y two-digit year, %Y year, %z utc
 * offset (recorded, not applied), %Z zone name (skipped), %%.
 * Week numbers (%U/%V/%W) are not provided and fail the format.
 */

static const char *const wdays[] = {
	"sun", "mon", "tue", "wed", "thu", "fri", "sat",
};
static const char *const months[] = {
	"jan", "feb", "mar", "apr", "may", "jun",
	"jul", "aug", "sep", "oct", "nov", "dec",
};

/* match a name in names[] case-insensitively against the input;
   return the index or -1 */
static int match_name(const char **s, const char *const names[], int count)
{
	for (int i = 0; i < count; i++) {
		size_t n = strlen(names[i]);

		if (!strncasecmp(*s, names[i], n)) {
			*s += n;
			return i;
		}
	}
	return -1;
}

/* parse up to width digits into *out; return 0 or -1 */
static int take_digits(const char **s, int width, int *out)
{
	int v = 0;
	int n = 0;

	while (n < width && isdigit((unsigned char)**s)) {
		v = v * 10 + (**s - '0');
		(*s)++;
		n++;
	}
	if (!n)
		return -1;
	*out = v;
	return 0;
}

static int two_digit_year(int y)
{
	/* POSIX: 69-99 means 19xx, 0-68 means 20xx */
	if (y < 69)
		return y + 100;
	return y;
}

static const char *parse(struct tm *tm, const char *fmt, const char *s)
{
	for (; *fmt; fmt++) {
		if (isspace((unsigned char)*fmt)) {
			while (isspace((unsigned char)*s))
				s++;
			continue;
		}
		if (*fmt != '%') {
			if (*s++ != *fmt)
				return NULL;
			continue;
		}
		fmt++;
		switch (*fmt) {
		case '%':
			if (*s++ != '%')
				return NULL;
			break;
		case 'a':
		case 'A': {
			int w = match_name(&s, wdays, 7);

			if (w < 0)
				return NULL;
			tm->tm_wday = w;
			break;
		}
		case 'b':
		case 'B':
		case 'h': {
			int m = match_name(&s, months, 12);

			if (m < 0)
				return NULL;
			tm->tm_mon = m;
			break;
		}
		case 'c':
			s = parse(tm, "%a %b %e %H:%M:%S %Y", s);
			if (!s)
				return NULL;
			break;
		case 'C': {
			int c;

			if (take_digits(&s, 2, &c) < 0)
				return NULL;
			tm->tm_year = c * 100 - 1900;
			break;
		}
		case 'd':
		case 'e': {
			int d;

			while (isspace((unsigned char)*s))
				s++;
			if (take_digits(&s, 2, &d) < 0)
				return NULL;
			tm->tm_mday = d;
			break;
		}
		case 'D':
			s = parse(tm, "%m/%d/%y", s);
			if (!s)
				return NULL;
			break;
		case 'F':
			s = parse(tm, "%Y-%m-%d", s);
			if (!s)
				return NULL;
			break;
		case 'H': {
			int h;

			if (take_digits(&s, 2, &h) < 0)
				return NULL;
			tm->tm_hour = h;
			break;
		}
		case 'I': {
			int h;

			if (take_digits(&s, 2, &h) < 0)
				return NULL;
			tm->tm_hour = h % 12;
			break;
		}
		case 'j': {
			int j;

			if (take_digits(&s, 3, &j) < 0)
				return NULL;
			tm->tm_yday = j - 1;
			break;
		}
		case 'm': {
			int m;

			if (take_digits(&s, 2, &m) < 0)
				return NULL;
			tm->tm_mon = m - 1;
			break;
		}
		case 'M': {
			int m;

			if (take_digits(&s, 2, &m) < 0)
				return NULL;
			tm->tm_min = m;
			break;
		}
		case 'n':
		case 't':
			while (isspace((unsigned char)*s))
				s++;
			break;
		case 'p': {
			int pm = match_name(&s,
					    (const char *[]){ "pm", "am" }, 2);

			if (pm < 0)
				return NULL;
			if (pm == 0)
				tm->tm_hour += 12;
			break;
		}
		case 'r':
			s = parse(tm, "%I:%M:%S %p", s);
			if (!s)
				return NULL;
			break;
		case 'R':
			s = parse(tm, "%H:%M", s);
			if (!s)
				return NULL;
			break;
		case 'S': {
			int sec;

			if (take_digits(&s, 2, &sec) < 0)
				return NULL;
			tm->tm_sec = sec;
			break;
		}
		case 'T':
			s = parse(tm, "%H:%M:%S", s);
			if (!s)
				return NULL;
			break;
		case 'u': {
			int w;

			if (take_digits(&s, 1, &w) < 0)
				return NULL;
			tm->tm_wday = w % 7;
			break;
		}
		case 'w': {
			int w;

			if (take_digits(&s, 1, &w) < 0)
				return NULL;
			tm->tm_wday = w;
			break;
		}
		case 'x':
			s = parse(tm, "%m/%d/%y", s);
			if (!s)
				return NULL;
			break;
		case 'X':
			s = parse(tm, "%H:%M:%S", s);
			if (!s)
				return NULL;
			break;
		case 'y': {
			int y;

			if (take_digits(&s, 2, &y) < 0)
				return NULL;
			tm->tm_year = two_digit_year(y);
			break;
		}
		case 'Y': {
			int y;

			if (take_digits(&s, 4, &y) < 0)
				return NULL;
			tm->tm_year = y - 1900;
			break;
		}
		case 'z': {
			int sign = 1;

			if (*s == '+' || *s == '-') {
				sign = *s == '-' ? -1 : 1;
				s++;
			}
			{
				int hh = 0, mm = 0;

				if (take_digits(&s, 2, &hh) < 0)
					return NULL;
				if (*s == ':')
					s++;
				if (isdigit((unsigned char)*s))
					(void)take_digits(&s, 2, &mm);
				tm->tm_isdst = 0;
				/* the offset is recorded through tm_gmtoff
				   when the layout provides it; the BRK
				   struct tm has no such field, so the
				   value is validated and dropped */
				(void)sign;
				(void)mm;
			}
			break;
		}
		case 'Z':
			while (isalpha((unsigned char)*s))
				s++;
			break;
		default:
			return NULL;
		}
	}
	return s;
}

char *strptime(const char *s, const char *fmt, struct tm *tm)
{
	const char *rc = parse(tm, fmt, s);

	return rc ? (char *)rc : NULL;
}
