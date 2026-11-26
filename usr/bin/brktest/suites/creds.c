#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int case_lookup(struct test_ctx *ctx)
{
	struct passwd *pw;
	struct group *gr;
	gid_t groups[8];
	int ngroups = 8;

	(void)ctx;
	pw = getpwnam("root");
	if (!pw || pw->pw_uid != 0 || pw->pw_gid != 0 ||
	    strcmp(pw->pw_dir, "/root"))
		return test_fail_msg("creds: root passwd entry is wrong");
	pw = getpwnam("user");
	if (!pw || pw->pw_uid != 1000 || pw->pw_gid != 1000 ||
	    strcmp(pw->pw_dir, "/home/user"))
		return test_fail_msg("creds: user passwd entry is wrong");
	pw = getpwuid(1000);
	if (!pw || strcmp(pw->pw_name, "user"))
		return test_fail_msg("creds: getpwuid failed");

	gr = getgrnam("root");
	if (!gr || gr->gr_gid != 0)
		return test_fail_msg("creds: root group entry is wrong");
	gr = getgrnam("user");
	if (!gr || gr->gr_gid != 1000)
		return test_fail_msg("creds: user group entry is wrong");
	gr = getgrgid(1000);
	if (!gr || strcmp(gr->gr_name, "user"))
		return test_fail_msg("creds: getgrgid failed");

	if (getgrouplist("user", 1000, groups, &ngroups) != 1 ||
	    ngroups != 1 || groups[0] != 1000)
		return test_fail_msg("creds: getgrouplist failed");
	return 0;
}

static int case_setid(struct test_ctx *ctx)
{
	int status;
	pid_t pid;

	(void)ctx;
	pid = fork();
	if (pid < 0)
		return test_fail_msg("creds: fork failed: %d", errno);
	if (pid == 0) {
		uid_t ruid, euid, suid;
		gid_t rgid, egid, sgid;

		if (setresgid(1000, 1000, 1000) != 0)
			_exit(10);
		if (setresuid(1000, 1000, 1000) != 0)
			_exit(11);
		if (getresuid(&ruid, &euid, &suid) != 0 ||
		    ruid != 1000 || euid != 1000 || suid != 1000)
			_exit(12);
		if (getresgid(&rgid, &egid, &sgid) != 0 ||
		    rgid != 1000 || egid != 1000 || sgid != 1000)
			_exit(13);
		errno = 0;
		if (setuid(0) != -1 || errno != EPERM)
			_exit(14);
		errno = 0;
		if (setgid(0) != -1 || errno != EPERM)
			_exit(15);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return test_fail_msg("creds: setid child status is 0x%x",
				     status);
	if (WEXITSTATUS(status) != 0)
		return test_fail_msg("creds: setid child failed at step %d",
				     WEXITSTATUS(status));
	return 0;
}

static int case_permissions(struct test_ctx *ctx)
{
	char dir[128], rootfile[160], userfile[160];
	int status;
	pid_t pid;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "creds") != 0)
		return test_fail_msg("creds: cannot create temp dir");
	if (snprintf(rootfile, sizeof(rootfile), "%s/root", dir) < 0 ||
	    snprintf(userfile, sizeof(userfile), "%s/user", dir) < 0)
		return -1;

	fd = open(rootfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return test_fail_msg("creds: open root file failed: %d", errno);
	close(fd);
	fd = open(userfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return test_fail_msg("creds: open user file failed: %d", errno);
	close(fd);
	if (chown(userfile, 1000, 1000) != 0)
		return test_fail_msg("creds: chown failed: %d", errno);

	pid = fork();
	if (pid < 0)
		return test_fail_msg("creds: fork failed: %d", errno);
	if (pid == 0) {
		if (setresgid(1000, 1000, 1000) != 0 ||
		    setresuid(1000, 1000, 1000) != 0)
			_exit(10);
		if (access(rootfile, R_OK) != -1 || errno != EACCES)
			_exit(11);
		if (access(userfile, R_OK) != 0)
			_exit(12);
		if (open(rootfile, O_RDONLY) != -1)
			_exit(13);
		int ufd = open(userfile, O_RDONLY);

		if (ufd < 0)
			_exit(14);
		close(ufd);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return test_fail_msg("creds: permission child status is 0x%x",
				     status);
	if (WEXITSTATUS(status) != 0)
		return test_fail_msg("creds: permission child failed at step %d",
				     WEXITSTATUS(status));
	unlink(rootfile);
	unlink(userfile);
	rmdir(dir);
	return 0;
}

static int case_umask(struct test_ctx *ctx)
{
	char dir[128], path[160];
	mode_t old;
	struct stat st;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "umask") != 0)
		return test_fail_msg("creds: cannot create temp dir");
	if (snprintf(path, sizeof(path), "%s/file", dir) < 0)
		return -1;
	old = umask(0022);
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0777);
	(void)umask(old);
	if (fd < 0)
		return test_fail_msg("creds: open failed: %d", errno);
	if (fstat(fd, &st) != 0 || (st.st_mode & 07777) != 0755)
		return test_fail_msg("creds: umask produced mode %o",
				     st.st_mode & 07777);
	close(fd);
	unlink(path);
	rmdir(dir);
	return 0;
}

static const struct test_case creds_cases[] = {
	{ "lookup", case_lookup, 0, 10000 },
	{ "setid", case_setid, TEST_REQUIRE_ROOT | TEST_OWN_PGRP, 10000 },
	{ "permissions", case_permissions,
	  TEST_REQUIRE_ROOT | TEST_OWN_PGRP, 10000 },
	{ "umask", case_umask, TEST_OWN_PGRP, 10000 },
};

const struct test_suite brktest_creds_suite = {
	.name = "creds",
	.description = "identity, credentials, permissions and auth databases",
	.cases = creds_cases,
	.nr_cases = sizeof(creds_cases) / sizeof(creds_cases[0]),
};
