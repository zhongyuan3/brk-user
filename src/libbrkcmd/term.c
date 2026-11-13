#include <brk/cmd.h>
#include <stdlib.h>
#include <string.h>

const char *brk_cmd_term(void)
{
	return getenv("TERM");
}

int brk_cmd_term_check(void)
{
	const char *term = brk_cmd_term();

	if (!term || !term[0])
		return brk_cmd_fail("TERM is not set");
	if (!strcmp(term, "dumb"))
		return brk_cmd_fail("terminal type '%s' is not supported",
				    term);
	return 0;
}
