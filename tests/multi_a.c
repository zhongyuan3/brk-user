int shared = 100;

int add(int a, int b)
{
	return a + b;
}

int get_shared(void)
{
	return shared;
}

static int helper(void)
{
	return 1;
}

int a_helper(void)
{
	return helper();
}
