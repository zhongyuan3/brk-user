/*
 * Interposition probe: announce() deliberately goes through the
 * libc puts() symbol, so an executable that defines its own puts
 * overrides what this DSO prints.
 */
int puts(const char *s);

void announce(void)
{
	puts("from libdyn\n");
}
