#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PW_LINE_MAX 512

static struct passwd pw;
static char linebuf[PW_LINE_MAX];

static int split(char *line, char **fields, int max)
{
	int n = 0;
	char *p = line;

	while (n < max) {
		fields[n++] = p;
		char *colon = strchr(p, ':');
		if (!colon)
			break;
		*colon = '\0';
		p = colon + 1;
	}
	return n;
}

static struct passwd *parse_line(char *line)
{
	char *f[7];
	int n = split(line, f, 7);

	if (n < 7)
		return NULL;

	pw.pw_name = f[0];
	pw.pw_passwd = f[1];
	pw.pw_uid = (uid_t)strtoul(f[2], NULL, 10);
	pw.pw_gid = (gid_t)strtoul(f[3], NULL, 10);
	pw.pw_gecos = f[4];
	pw.pw_dir = f[5];
	pw.pw_shell = f[6];
	return &pw;
}

static struct passwd *lookup(const char *name, uid_t uid, int by_uid)
{
	FILE *fp = fopen("/etc/passwd", "r");

	if (!fp)
		return NULL;

	while (fgets(linebuf, sizeof(linebuf), fp)) {
		size_t len = strlen(linebuf);
		struct passwd *p;

		while (len &&
		       (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r'))
			linebuf[--len] = '\0';
		if (!linebuf[0] || linebuf[0] == '#')
			continue;

		p = parse_line(linebuf);
		if (!p)
			continue;
		if (by_uid ? p->pw_uid == uid : !strcmp(p->pw_name, name)) {
			fclose(fp);
			return p;
		}
	}

	fclose(fp);
	return NULL;
}

struct passwd *getpwnam(const char *name)
{
	return lookup(name, 0, 0);
}

struct passwd *getpwuid(uid_t uid)
{
	return lookup(NULL, uid, 1);
}
