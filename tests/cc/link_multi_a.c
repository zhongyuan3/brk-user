/*
 * First unit of the multi-object linking test.  link_multi_b.c defines
 * main.  Both files define a static symbol named "local_value" and a
 * static string named "local_tag"; compiling them separately gives both
 * units the same ordinary symbol names, which the linker must keep
 * unit-local.
 */

int link_shared = 41;
int link_bss[4];
long long link_aligned = 9;

extern int link_from_b;
extern char link_msg_b[8];

int *link_cross_ptr = &link_from_b;
char *link_cross_str = link_msg_b;

static int local_value = 10;
static char local_tag[] = "alpha";

static int shared_helper(void)
{
	return 1;
}

int link_add(int a, int b)
{
	return a + b;
}

int link_call_helper_a(void)
{
	return shared_helper();
}

int link_local_a(void)
{
	return local_value;
}

int link_tag_a(void)
{
	return local_tag[0];
}
