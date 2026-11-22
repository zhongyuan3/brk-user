/*
 * dlopen test plugin: its plug_sum() reaches back into the calling
 * executable (base_value), so loading it exercises dlopen-time
 * resolution against the exe's own dynsym.
 */
extern int base_value(void);

int plug_inited;
int plug_value(void)
{
	return 7;
}

__attribute__((constructor))
static void plug_init(void)
{
	plug_inited = 1;
}

__attribute__((destructor))
static void plug_fini(void)
{
	plug_inited = 2;
}

int plug_sum(void)
{
	return base_value() + plug_value();
}
