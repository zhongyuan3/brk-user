/*
 * Test DSO: two exported functions plus an init-array constructor,
 * so the loader exercises DT_NEEDED resolution, GLOB_DAT/JUMP_SLOT
 * relocations and DT_INIT_ARRAY execution order.
 */
int hello_inited;
int hello_magic = 0x1234;

__attribute__((constructor))
static void hello_init(void)
{
	hello_inited = 1;
}

int hello_value(void)
{
	return 42;
}

const char *hello_msg(void)
{
	return "dyn ok\n";
}
