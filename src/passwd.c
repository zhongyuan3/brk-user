#include <apputil.h>
#include <pwd.h>
#include <pwutil.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] =
	"Usage: passwd [USER]\n"
	"Change the password of USER (default: current user).";

static int update_shadow(const char *user, const char *hash)
{
	FILE *in, *out;
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	int found = 0;

	in = fopen("/etc/shadow", "r");
	if (!in)
		return -1;
	out = fopen("/etc/shadow.tmp", "w");
	if (!out) {
		fclose(in);
		return -1;
	}

	while ((len = getline(&line, &cap, in)) != -1) {
		char *colon = strchr(line, ':');

		if (colon && (size_t)(colon - line) == strlen(user) &&
		    !strncmp(line, user, strlen(user))) {
			fprintf(out, "%s:%s\n", user, hash);
			found = 1;
		} else {
			if (fwrite(line, 1, (size_t)len, out) != (size_t)len) {
				free(line);
				fclose(in);
				fclose(out);
				unlink("/etc/shadow.tmp");
				return -1;
			}
		}
	}
	free(line);

	if (!found)
		fprintf(out, "%s:%s\n", user, hash);

	if (fclose(out) != 0 || fclose(in) != 0) {
		unlink("/etc/shadow.tmp");
		return -1;
	}
	if (chmod("/etc/shadow.tmp", 0600) != 0 ||
	    rename("/etc/shadow.tmp", "/etc/shadow") != 0) {
		unlink("/etc/shadow.tmp");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	const char *target;
	struct passwd *pw;
	struct spwd *sp;
	char oldpass[128], newpass[128], confirm[128];
	char hash[PW_HASH_BUF_LEN];
	uid_t real = getuid();

	app_init(argc, argv);

	if (argc > 2)
		app_usage(usage);

	if (argc > 1) {
		target = argv[1];
		pw = getpwnam(target);
	} else {
		pw = getpwuid(real);
		target = pw ? pw->pw_name : NULL;
	}

	if (!pw || !target)
		return app_fail("unknown user");

	if (real != 0) {
		if (pw->pw_uid != real)
			return app_fail("permission denied");
		sp = getspnam(target);
		if (!sp || !sp->sp_pwdp[0])
			return app_fail("no password entry for '%s'", target);
		fputs("Current password: ", stdout);
		fflush(stdout);
		if (read_password(oldpass, sizeof(oldpass)) < 0)
			return APP_EXIT_FAIL;
		if (pw_verify(oldpass, sp->sp_pwdp) != 0) {
			app_error("incorrect password");
			sleep(1);
			return APP_EXIT_FAIL;
		}
	}

	fputs("New password: ", stdout);
	fflush(stdout);
	if (read_password(newpass, sizeof(newpass)) < 0)
		return APP_EXIT_FAIL;
	if (!newpass[0])
		return app_fail("empty password not allowed");

	fputs("Retype new password: ", stdout);
	fflush(stdout);
	if (read_password(confirm, sizeof(confirm)) < 0)
		return APP_EXIT_FAIL;
	if (strcmp(newpass, confirm))
		return app_fail("passwords do not match");

	if (pw_generate(newpass, hash, sizeof(hash)) != 0)
		return app_fail("failed to hash password");
	if (update_shadow(target, hash) != 0)
		return app_fail_errno("failed to update /etc/shadow");

	printf("password updated for %s\n", target);
	return APP_EXIT_OK;
}
