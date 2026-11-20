#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

static const char *const category_names[] = {
	"LC_CTYPE",   "LC_NUMERIC",  "LC_TIME",
	"LC_COLLATE", "LC_MONETARY", "LC_MESSAGES",
};

static int valid_category(int category)
{
	return category >= LC_CTYPE && category <= LC_ALL;
}

static int is_c_locale(const char *locale)
{
	return !locale || !locale[0] || !strcmp(locale, "C") ||
	       !strcmp(locale, "POSIX");
}

static const char *environment_locale(int category)
{
	const char *value;

	value = getenv("LC_ALL");
	if (value && value[0])
		return value;

	if (category != LC_ALL) {
		value = getenv(category_names[category]);
		if (value && value[0])
			return value;
	}

	value = getenv("LANG");
	if (value && value[0])
		return value;
	return NULL;
}

char *setlocale(int category, const char *locale)
{
	if (!valid_category(category))
		return NULL;

	if (!locale)
		return (char *)"C";

	if (!locale[0])
		locale = environment_locale(category);
	if (!is_c_locale(locale))
		return NULL;

	return (char *)"C";
}

struct lconv *localeconv(void)
{
	static struct lconv c_locale = {
		.decimal_point = ".",
		.thousands_sep = "",
		.grouping = "",
		.int_curr_symbol = "",
		.currency_symbol = "",
		.mon_decimal_point = "",
		.mon_thousands_sep = "",
		.mon_grouping = "",
		.positive_sign = "",
		.negative_sign = "",
		.int_frac_digits = CHAR_MAX,
		.frac_digits = CHAR_MAX,
		.p_cs_precedes = CHAR_MAX,
		.p_sep_by_space = CHAR_MAX,
		.n_cs_precedes = CHAR_MAX,
		.n_sep_by_space = CHAR_MAX,
		.p_sign_posn = CHAR_MAX,
		.n_sign_posn = CHAR_MAX,
		.int_p_cs_precedes = CHAR_MAX,
		.int_p_sep_by_space = CHAR_MAX,
		.int_n_cs_precedes = CHAR_MAX,
		.int_n_sep_by_space = CHAR_MAX,
		.int_p_sign_posn = CHAR_MAX,
		.int_n_sign_posn = CHAR_MAX,
	};

	return &c_locale;
}
