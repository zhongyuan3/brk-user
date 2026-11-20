#include <shadow.h>
#include <stdio.h>
#include <string.h>

#define SP_LINE_MAX 512

static struct spwd sp;
static char linebuf[SP_LINE_MAX];

struct spwd *getspnam(const char *name)
{
	FILE *fp = fopen("/etc/shadow", "r");

	if (!fp)
		return NULL;

	while (fgets(linebuf, sizeof(linebuf), fp)) {
		size_t len = strlen(linebuf);
		char *colon;

		while (len &&
		       (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r'))
			linebuf[--len] = '\0';
		if (!linebuf[0] || linebuf[0] == '#')
			continue;

		colon = strchr(linebuf, ':');
		if (!colon)
			continue;
		*colon = '\0';
		if (strcmp(linebuf, name))
			continue;

		sp.sp_namp = linebuf;
		sp.sp_pwdp = colon + 1;
		colon = strchr(sp.sp_pwdp, ':');
		if (colon)
			*colon = '\0';
		fclose(fp);
		return &sp;
	}

	fclose(fp);
	return NULL;
}
