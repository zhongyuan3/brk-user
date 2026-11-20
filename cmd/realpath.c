#include <brk/cmd.h>
#include <brk/limits.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char help[] = "Usage: realpath PATH...\n"
			   "Print the canonical absolute path for each PATH.";

static int append_component(char *out, size_t *out_len, const char *comp,
			    size_t comp_len, size_t cap)
{
	if (*out_len + 1 + comp_len >= cap) {
		errno = ENAMETOOLONG;
		return -1;
	}
	out[(*out_len)++] = '/';
	memcpy(out + *out_len, comp, comp_len);
	*out_len += comp_len;
	out[*out_len] = '\0';
	return 0;
}

static void pop_component(char *out, size_t *out_len)
{
	size_t i = *out_len;

	while (i > 0 && out[i - 1] != '/')
		i--;
	if (i > 0)
		i--;
	*out_len = i;
	out[i] = '\0';
}

static int resolve_path(const char *path, char *out, size_t cap)
{
	char work[PATH_MAX * 2];
	char newwork[PATH_MAX * 2];
	char target[PATH_MAX + 1];
	char comp[NAME_MAX + 1];
	char cwd[PATH_MAX];
	size_t out_len = 0;
	size_t pos = 0;
	int links = 0;

	if (path[0] == '/') {
		if (strlen(path) >= sizeof(work)) {
			errno = ENAMETOOLONG;
			return -1;
		}
		strcpy(work, path);
	} else {
		if (!getcwd(cwd, sizeof(cwd)))
			return -1;
		int n = snprintf(work, sizeof(work), "%s/%s", cwd, path);

		if (n < 0 || (size_t)n >= sizeof(work)) {
			errno = ENAMETOOLONG;
			return -1;
		}
	}

	out[0] = '\0';
	for (;;) {
		while (work[pos] == '/')
			pos++;
		if (!work[pos])
			break;

		size_t start = pos;

		while (work[pos] && work[pos] != '/')
			pos++;
		size_t comp_len = pos - start;

		if (comp_len > NAME_MAX) {
			errno = ENAMETOOLONG;
			return -1;
		}
		memcpy(comp, work + start, comp_len);
		comp[comp_len] = '\0';

		if (!strcmp(comp, "."))
			continue;
		if (!strcmp(comp, "..")) {
			pop_component(out, &out_len);
			continue;
		}

		size_t parent_len = out_len;

		if (append_component(out, &out_len, comp, comp_len, cap))
			return -1;

		struct stat st;

		if (lstat(out, &st) != 0)
			return -1;

		if (S_ISLNK(st.st_mode)) {
			ssize_t n;

			if (++links > 40) {
				errno = ELOOP;
				return -1;
			}
			n = readlink(out, target, sizeof(target) - 1);
			if (n < 0)
				return -1;
			target[n] = '\0';

			size_t tlen = (size_t)n;
			size_t rlen = strlen(work + pos);

			if (tlen + rlen + 2 > sizeof(newwork)) {
				errno = ENAMETOOLONG;
				return -1;
			}
			memcpy(newwork, target, tlen);
			size_t new_len = tlen;

			if (new_len > 0 && newwork[new_len - 1] != '/' &&
			    rlen > 0 && work[pos] != '/')
				newwork[new_len++] = '/';
			memcpy(newwork + new_len, work + pos, rlen);
			new_len += rlen;
			newwork[new_len] = '\0';

			if (target[0] == '/')
				out_len = 0;
			else
				out_len = parent_len;
			out[out_len] = '\0';

			if (strlen(newwork) >= sizeof(work)) {
				errno = ENAMETOOLONG;
				return -1;
			}
			strcpy(work, newwork);
			pos = 0;
		}
	}

	if (out_len == 0) {
		if (cap < 2) {
			errno = ENAMETOOLONG;
			return -1;
		}
		out[0] = '/';
		out[1] = '\0';
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(help);
	if (argc < 2)
		brk_cmd_usage(help);

	for (int i = 1; i < argc; i++) {
		char out[PATH_MAX];

		if (resolve_path(argv[i], out, sizeof(out)) != 0) {
			brk_cmd_error("%s: %s", argv[i], strerror(errno));
			ret = BRK_CMD_EXIT_FAIL;
			continue;
		}
		puts(out);
	}
	return ret;
}
