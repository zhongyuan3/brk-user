#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/*
 * mkstemp()/mkdtemp(): replace the trailing X block of the template
 * with a value that does not name an existing file and create it
 * exclusively.  The search mixes the process id with a per-process
 * counter, so concurrent processes do not collide and repeated calls
 * advance deterministically.
 */
static int fill_template(const char *template, size_t *prefix_len,
			 size_t *x_count)
{
	size_t len = strlen(template);
	size_t n = 0;

	while (n < len && template[len - 1 - n] == 'X')
		n++;
	if (n < 6) {
		errno = EINVAL;
		return -1;
	}
	*prefix_len = len - n;
	*x_count = n;
	return 0;
}

static void write_suffix(char *template, size_t prefix_len, size_t x_count,
			 uint64_t value)
{
	size_t i;

	for (i = 0; i < x_count; i++) {
		template[prefix_len + i] = digits[value % 36];
		value /= 36;
	}
}

int mkstemp(char *template)
{
	static uint32_t counter;
	size_t prefix_len;
	size_t x_count;
	uint32_t base;
	uint32_t attempt;

	if (fill_template(template, &prefix_len, &x_count) < 0)
		return -1;

	base = (uint32_t)getpid() * 0x9e3779b9u + ++counter * 668265263u;
	for (attempt = 0; attempt < 4096; attempt++) {
		int fd;

		write_suffix(template, prefix_len, x_count,
			     (uint64_t)base + attempt);
		fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0)
			return fd;
		if (errno != EEXIST)
			return -1;
	}
	errno = EEXIST;
	return -1;
}

char *mkdtemp(char *template)
{
	static uint32_t counter;
	size_t prefix_len;
	size_t x_count;
	uint32_t base;
	uint32_t attempt;

	if (fill_template(template, &prefix_len, &x_count) < 0)
		return NULL;

	base = (uint32_t)getpid() * 0x9e3779b9u + ++counter * 668265263u;
	for (attempt = 0; attempt < 4096; attempt++) {
		write_suffix(template, prefix_len, x_count,
			     (uint64_t)base + attempt);
		if (mkdir(template, 0700) == 0)
			return template;
		if (errno != EEXIST)
			return NULL;
	}
	errno = EEXIST;
	return NULL;
}
