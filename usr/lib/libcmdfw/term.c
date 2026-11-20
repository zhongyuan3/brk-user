#include <libcmdfw.h>
#include <stdlib.h>
#include <string.h>

const char *cmdfw_term(void)
{
	return getenv("TERM");
}

int cmdfw_term_check(void)
{
	const char *term = cmdfw_term();

	if (!term || !term[0])
		return cmdfw_fail("TERM is not set");
	if (!strcmp(term, "dumb"))
		return cmdfw_fail("terminal type '%s' is not supported",
				    term);
	return 0;
}
