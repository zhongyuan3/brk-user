#include <brk/limits.h>
#include <grp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GR_LINE_MAX 512

static struct group gr;
static char linebuf[GR_LINE_MAX];
static char *members[NGROUPS_MAX];

static struct group *parse_line(char *line)
{
	char *name = line;
	char *colon = strchr(name, ':');
	if (!colon)
		return NULL;
	*colon = '\0';

	char *passwd = colon + 1;
	colon = strchr(passwd, ':');
	if (!colon)
		return NULL;
	*colon = '\0';

	char *gidstr = colon + 1;
	colon = strchr(gidstr, ':');
	if (!colon)
		return NULL;
	*colon = '\0';

	char *memstr = colon + 1;
	int n = 0;

	if (*memstr) {
		char *p = memstr;

		while (*p && n < NGROUPS_MAX) {
			char *comma = strchr(p, ',');

			members[n++] = p;
			if (!comma)
				break;
			*comma = '\0';
			p = comma + 1;
		}
	}
	members[n] = NULL;

	gr.gr_name = name;
	gr.gr_passwd = passwd;
	gr.gr_gid = (gid_t)strtoul(gidstr, NULL, 10);
	gr.gr_mem = members;
	return &gr;
}

static struct group *lookup(const char *name, gid_t gid, int by_gid)
{
	FILE *fp = fopen("/etc/group", "r");

	if (!fp)
		return NULL;

	while (fgets(linebuf, sizeof(linebuf), fp)) {
		size_t len = strlen(linebuf);
		struct group *g;

		while (len &&
		       (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r'))
			linebuf[--len] = '\0';
		if (!linebuf[0] || linebuf[0] == '#')
			continue;

		g = parse_line(linebuf);
		if (!g)
			continue;
		if (by_gid ? g->gr_gid == gid : !strcmp(g->gr_name, name)) {
			fclose(fp);
			return g;
		}
	}

	fclose(fp);
	return NULL;
}

struct group *getgrnam(const char *name)
{
	return lookup(name, 0, 0);
}

struct group *getgrgid(gid_t gid)
{
	return lookup(NULL, gid, 1);
}

static bool group_has_member(struct group *g, const char *user)
{
	for (int i = 0; g->gr_mem && g->gr_mem[i]; i++) {
		if (!strcmp(g->gr_mem[i], user))
			return true;
	}
	return false;
}

int getgrouplist(const char *user, gid_t gid, gid_t *groups, int *ngroups)
{
	FILE *fp = fopen("/etc/group", "r");
	int n = 0;

	if (!fp)
		return -1;

	if (n < *ngroups)
		groups[n] = gid;
	n++;

	while (fgets(linebuf, sizeof(linebuf), fp)) {
		size_t len = strlen(linebuf);
		struct group *g;

		while (len &&
		       (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r'))
			linebuf[--len] = '\0';
		if (!linebuf[0] || linebuf[0] == '#')
			continue;

		g = parse_line(linebuf);
		if (!g || !group_has_member(g, user))
			continue;

		bool dup = false;

		for (int i = 0; i < n && i < *ngroups; i++) {
			if (groups[i] == g->gr_gid)
				dup = true;
		}
		if (dup)
			continue;
		if (n < *ngroups)
			groups[n] = g->gr_gid;
		n++;
	}

	fclose(fp);

	if (n > *ngroups) {
		*ngroups = n;
		return -1;
	}
	*ngroups = n;
	return n;
}
