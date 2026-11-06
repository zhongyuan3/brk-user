#include <arpa/inet.h>
#include <brk/limits.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <pwutil.h>
#include <shadow.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile int caught;

static void on_sigusr1(int sig)
{
	(void)sig;
	caught = 1;
}

static void on_sigwinch(int sig)
{
	(void)sig;
	caught = sig;
}

static int test_fork(void)
{
	puts("Creating process...");
	for (int i = 0; i < 5; ++i) {
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			continue;
		}
		if (pid == 0) {
			for (int j = 0; j < 300; ++j) {
				char c = 'A' + i;
				write(STDOUT_FILENO, &c, 1);
			}
			_exit(0);
		}
	}
	while (1) {
		if (wait(0) < 0)
			break;
	}
	putchar('\n');
	return 0;
}

static int test_exit_status(void)
{
	pid_t pid = fork();
	int status;

	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0)
		exit(42);

	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
		printf("exit status test failed: status=0x%x\n", status);
		return -1;
	}
	printf("exit status ok: %d\n", WEXITSTATUS(status));
	return 0;
}

static int test_kill_sig(int sig)
{
	pid_t pid = fork();
	int status;
	struct timespec ts = { .tv_sec = 1000, .tv_nsec = 0 };

	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		nanosleep(&ts, NULL);
		exit(0);
	}

	if (kill(pid, sig) != 0) {
		perror("kill");
		return -1;
	}

	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		return -1;
	}
	if (!WIFSIGNALED(status) || WTERMSIG(status) != sig) {
		printf("kill(%d) test failed: status=0x%x\n", sig, status);
		return -1;
	}
	printf("kill sig %d ok\n", sig);
	return 0;
}

static int test_kill(void)
{
	if (test_exit_status() != 0)
		return -1;
	if (test_kill_sig(SIGKILL) != 0)
		return -1;
	if (test_kill_sig(SIGTERM) != 0)
		return -1;
	return 0;
}

static int test_raise(void)
{
	struct sigaction sa = { .sa_handler = on_sigusr1 };

	caught = 0;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction");
		return -1;
	}
	if (raise(SIGUSR1) != 0) {
		perror("raise");
		return -1;
	}
	if (!caught) {
		printf("raise: handler did not run\n");
		return -1;
	}
	printf("raise ok\n");
	return 0;
}

static int test_sigwinch(void)
{
	struct sigaction sa = { .sa_handler = on_sigwinch };
	struct winsize ws;

	caught = 0;
	if (sigaction(SIGWINCH, &sa, NULL) != 0) {
		perror("sigaction SIGWINCH");
		return -1;
	}
	if (kill(getpid(), SIGWINCH) != 0) {
		perror("kill SIGWINCH");
		return -1;
	}
	if (caught != SIGWINCH) {
		printf("sigwinch kill: handler did not run (caught=%d)\n",
		       caught);
		return -1;
	}

	caught = 0;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
		perror("TIOCGWINSZ");
		return -1;
	}
	ws.ws_col = ws.ws_col == 100 ? 101 : 100;
	if (ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws) != 0) {
		perror("TIOCSWINSZ");
		return -1;
	}
	if (caught != SIGWINCH) {
		printf("sigwinch ioctl: handler did not run (caught=%d)\n",
		       caught);
		return -1;
	}
	printf("sigwinch ok\n");
	return 0;
}

static int test_sigaction_delivery(void)
{
	struct sigaction sa = { .sa_handler = on_sigusr1 };

	caught = 0;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction");
		return -1;
	}

	if (kill(getpid(), SIGUSR1) != 0) {
		perror("kill");
		return -1;
	}

	if (!caught) {
		printf("sigaction: handler did not run\n");
		return -1;
	}
	printf("sigaction delivery ok\n");
	return 0;
}

static int test_sig_ignore(void)
{
	struct sigaction sa = { .sa_handler = SIG_IGN };

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction ignore");
		return -1;
	}

	if (kill(getpid(), SIGUSR1) != 0) {
		perror("kill");
		return -1;
	}

	printf("sigignore ok\n");
	return 0;
}

static int test_sigprocmask(void)
{
	sigset_t set = (1ULL << SIGUSR1);
	sigset_t old;

	if (sigprocmask(SIG_BLOCK, &set, &old) != 0) {
		perror("sigprocmask block");
		return -1;
	}

	caught = 0;
	if (kill(getpid(), SIGUSR1) != 0) {
		perror("kill");
		return -1;
	}

	if (caught) {
		printf("sigprocmask: signal delivered while blocked\n");
		return -1;
	}

	if (sigprocmask(SIG_SETMASK, &old, NULL) != 0) {
		perror("sigprocmask restore");
		return -1;
	}

	if (!caught) {
		printf("sigprocmask: signal not delivered after unblock\n");
		return -1;
	}

	printf("sigprocmask ok\n");
	return 0;
}

static int test_signal(void)
{
	struct sigaction sa = { .sa_handler = on_sigusr1 };

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction setup");
		return -1;
	}

	if (test_sigaction_delivery() != 0)
		return -1;
	if (test_sig_ignore() != 0)
		return -1;

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction restore");
		return -1;
	}
	if (test_sigprocmask() != 0)
		return -1;
	if (test_raise() != 0)
		return -1;
	if (test_sigwinch() != 0)
		return -1;

	return 0;
}

static int test_meta(void)
{
	const char *dir = "/tmp/brktest_meta";
	const char *path = "/tmp/brktest_meta/file";
	struct stat st;
	struct statvfs sfs;
	struct utimbuf ub;
	struct timeval tv[2];
	struct timespec ts[2];
	int fd;

	mkdir("/tmp", 0755);
	mkdir(dir, 0755);
	unlink(path);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		perror("open");
		return -1;
	}
	close(fd);

	if (chmod(path, 0600) != 0 || stat(path, &st) != 0 ||
	    (st.st_mode & 07777) != 0600 || !S_ISREG(st.st_mode)) {
		perror("chmod");
		printf("chmod test failed: mode=%o\n", st.st_mode);
		return -1;
	}
	printf("chmod ok: %o\n", st.st_mode & 07777);

	fd = open(path, O_RDONLY);
	if (fd < 0 || fchmod(fd, 0640) != 0) {
		perror("fchmod");
		return -1;
	}
	close(fd);
	if (stat(path, &st) != 0 || (st.st_mode & 07777) != 0640) {
		printf("fchmod test failed: mode=%o\n", st.st_mode & 07777);
		return -1;
	}
	printf("fchmod ok: %o\n", st.st_mode & 07777);

	if (fchmodat(AT_FDCWD, path, 0601, 0) != 0 || stat(path, &st) != 0 ||
	    (st.st_mode & 07777) != 0601) {
		perror("fchmodat");
		return -1;
	}
	printf("fchmodat ok: %o\n", st.st_mode & 07777);

	if (mkdir("/tmp/brktest_meta/dir", 0750) != 0 ||
	    stat("/tmp/brktest_meta/dir", &st) != 0 ||
	    (st.st_mode & 07777) != 0750 || !S_ISDIR(st.st_mode)) {
		perror("mkdir mode");
		return -1;
	}
	printf("mkdir mode ok: %o\n", st.st_mode & 07777);

	ub.actime = 1234567890;
	ub.modtime = 1234567891;
	if (utime(path, &ub) != 0 || stat(path, &st) != 0 ||
	    st.st_atime != ub.actime || st.st_mtime != ub.modtime) {
		perror("utime");
		printf("utime test failed: atime=%ld mtime=%ld\n", st.st_atime,
		       st.st_mtime);
		return -1;
	}
	printf("utime ok\n");

	tv[0].tv_sec = 1000;
	tv[0].tv_usec = 500000;
	tv[1].tv_sec = 2000;
	tv[1].tv_usec = 250000;
	if (utimes(path, tv) != 0 || stat(path, &st) != 0 ||
	    st.st_atime != 1000 || st.st_atime_nsec != 500000000 ||
	    st.st_mtime != 2000 || st.st_mtime_nsec != 250000000) {
		perror("utimes");
		printf("utimes test failed: %ld.%lu %ld.%lu\n", st.st_atime,
		       st.st_atime_nsec, st.st_mtime, st.st_mtime_nsec);
		return -1;
	}
	printf("utimes ok\n");

	ts[0].tv_sec = 3000;
	ts[0].tv_nsec = 0;
	ts[1].tv_sec = 4000;
	ts[1].tv_nsec = 0;
	if (utimensat(AT_FDCWD, path, ts, 0) != 0 || stat(path, &st) != 0 ||
	    st.st_atime != 3000 || st.st_mtime != 4000) {
		perror("utimensat");
		return -1;
	}

	ts[0].tv_sec = 5000;
	ts[0].tv_nsec = 0;
	ts[1].tv_sec = 0;
	ts[1].tv_nsec = UTIME_OMIT;
	if (utimensat(AT_FDCWD, path, ts, 0) != 0 || stat(path, &st) != 0 ||
	    st.st_atime != 5000 || st.st_mtime != 4000) {
		perror("utimensat omit");
		return -1;
	}
	printf("utimensat ok\n");

	if (statvfs("/", &sfs) != 0 || sfs.f_bsize == 0 || sfs.f_blocks == 0) {
		perror("statvfs");
		return -1;
	}
	printf("statvfs ok: bsize=%lu blocks=%lu free=%lu\n",
	       (unsigned long)sfs.f_bsize, (unsigned long)sfs.f_blocks,
	       (unsigned long)sfs.f_bfree);

	unlink(path);
	rmdir("/tmp/brktest_meta/dir");
	rmdir(dir);
	return 0;
}

#define PERM_DIR_MAX 64

static char perm_dir[PERM_DIR_MAX];

static int perm_fail(const char *what)
{
	printf("perm: %s\n", what);
	return -1;
}

static void perm_join(char *buf, size_t size, const char *name)
{
	snprintf(buf, size, "%s/%s", perm_dir, name);
}

static int perm_copy(const char *src, const char *dst)
{
	char buf[4096];
	int in = open(src, O_RDONLY);
	int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	int ret = -1;

	if (in < 0 || out < 0)
		goto out;

	for (;;) {
		ssize_t rcnt = read(in, buf, sizeof(buf));

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			goto out;
		}
		if (rcnt == 0)
			break;

		char *p = buf;
		ssize_t left = rcnt;

		while (left > 0) {
			ssize_t n = write(out, p, (size_t)left);

			if (n < 0) {
				if (errno == EINTR)
					continue;
				goto out;
			}
			p += n;
			left -= n;
		}
	}
	ret = 0;

out:
	if (in >= 0)
		close(in);
	if (out >= 0)
		close(out);
	return ret;
}

static int perm_child(void)
{
	char secret[128], readable[128], privdir[128], sticky[128];
	char rootfile[128], ownsticky[128], noexec[128], execok[128];
	char own[128], umasked[128], newfile[128], subdir[128];
	char suidtest[128], sgidtest[128], suidwrite[128];
	struct stat st;
	int fd, status;
	pid_t pid;
	mode_t old;

	perm_join(secret, sizeof(secret), "secret");
	perm_join(readable, sizeof(readable), "readable");
	perm_join(privdir, sizeof(privdir), "privdir");
	perm_join(sticky, sizeof(sticky), "sticky");
	perm_join(rootfile, sizeof(rootfile), "sticky/rootfile");
	perm_join(ownsticky, sizeof(ownsticky), "sticky/ownsticky");
	perm_join(noexec, sizeof(noexec), "noexec");
	perm_join(execok, sizeof(execok), "execok");
	perm_join(own, sizeof(own), "owned");
	perm_join(umasked, sizeof(umasked), "umasked");
	perm_join(newfile, sizeof(newfile), "privdir/new");
	perm_join(subdir, sizeof(subdir), "privdir/sub");
	perm_join(suidtest, sizeof(suidtest), "suidtest");
	perm_join(sgidtest, sizeof(sgidtest), "sgidtest");
	perm_join(suidwrite, sizeof(suidwrite), "suidwrite");

	if (setgid(1000) != 0 || setuid(1000) != 0)
		return perm_fail("setuid/setgid failed");
	if (geteuid() != 1000 || getegid() != 1000)
		return perm_fail("bad effective ids after setuid/setgid");

	fd = open(secret, O_RDONLY);
	if (fd >= 0) {
		close(fd);
		return perm_fail("read of 0600 root file allowed");
	}
	if (errno != EACCES)
		return perm_fail("read of 0600 root file: wrong errno");

	fd = open(readable, O_RDONLY);
	if (fd < 0)
		return perm_fail("read of 0644 file denied");
	close(fd);

	fd = open(readable, O_WRONLY);
	if (fd >= 0) {
		close(fd);
		return perm_fail("write of 0644 file allowed");
	}
	if (errno != EACCES)
		return perm_fail("write of 0644 file: wrong errno");

	if (access(secret, R_OK) == 0)
		return perm_fail("access(R_OK) on 0600 root file allowed");
	if (access(readable, R_OK) != 0)
		return perm_fail("access(R_OK) on 0644 file denied");
	if (access(readable, W_OK) == 0)
		return perm_fail("access(W_OK) on 0644 file allowed");
	if (access(execok, X_OK) != 0)
		return perm_fail("access(X_OK) on 0755 file denied");
	if (access(noexec, X_OK) == 0)
		return perm_fail("access(X_OK) on 0644 file allowed");

	fd = open(newfile, O_WRONLY | O_CREAT, 0600);
	if (fd >= 0) {
		close(fd);
		return perm_fail("create in root 0755 directory allowed");
	}
	if (errno != EACCES)
		return perm_fail("create in root 0755 directory: wrong errno");

	if (mkdir(subdir, 0755) == 0)
		return perm_fail("mkdir in root 0755 directory allowed");

	if (chmod(readable, 0666) == 0)
		return perm_fail("chmod of root file allowed");
	if (errno != EPERM)
		return perm_fail("chmod of root file: wrong errno");

	if (chown(readable, 1000, 1000) == 0)
		return perm_fail("chown of root file allowed");
	if (errno != EPERM)
		return perm_fail("chown of root file: wrong errno");

	if (utimensat(AT_FDCWD, readable, NULL, 0) == 0)
		return perm_fail("utimensat of root file allowed");
	if (errno != EPERM)
		return perm_fail("utimensat of root file: wrong errno");

	if (unlink(rootfile) == 0)
		return perm_fail("unlink in sticky directory allowed");

	fd = open(ownsticky, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return perm_fail("create own file in sticky directory failed");
	close(fd);
	if (unlink(ownsticky) != 0)
		return perm_fail("unlink own file in sticky directory failed");

	fd = open(own, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return perm_fail("create file in 0777 directory failed");
	close(fd);

	if (utimensat(AT_FDCWD, own, NULL, 0) != 0)
		return perm_fail("utimensat of own file failed");

	old = umask(077);
	fd = open(umasked, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	umask(old);
	if (fd < 0)
		return perm_fail("create umask test file failed");
	close(fd);

	if (stat(umasked, &st) != 0 || (st.st_mode & 0777) != 0600)
		return perm_fail("umask was not applied to create mode");

	pid = fork();
	if (pid < 0)
		return perm_fail("fork for exec test failed");
	if (pid == 0) {
		char *argv[] = { "true", NULL };

		execve(execok, argv, NULL);
		_exit(111);
	}
	if (waitpid(pid, &status, 0) < 0)
		return perm_fail("waitpid for exec test failed");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return perm_fail("exec of 0755 file failed");

	pid = fork();
	if (pid < 0)
		return perm_fail("fork for setuid exec failed");
	if (pid == 0) {
		char *argv[] = { "brktest", "-check-euid", NULL };

		execve(suidtest, argv, NULL);
		_exit(112);
	}
	if (waitpid(pid, &status, 0) < 0)
		return perm_fail("waitpid for setuid exec failed");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return perm_fail("setuid exec did not raise euid to 0");
	printf("setuid exec ok\n");

	pid = fork();
	if (pid < 0)
		return perm_fail("fork for setgid exec failed");
	if (pid == 0) {
		char *argv[] = { "brktest", "-check-egid", NULL };

		execve(sgidtest, argv, NULL);
		_exit(113);
	}
	if (waitpid(pid, &status, 0) < 0)
		return perm_fail("waitpid for setgid exec failed");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return perm_fail("setgid exec did not raise egid to 2000");
	printf("setgid exec ok\n");

	fd = open(suidwrite, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd < 0)
		return perm_fail("create suid write test failed");
	close(fd);
	if (chmod(suidwrite, 04755) != 0)
		return perm_fail("chmod setuid on own file failed");
	if (stat(suidwrite, &st) != 0 || !(st.st_mode & S_ISUID))
		return perm_fail("setuid bit was not set");

	fd = open(suidwrite, O_WRONLY);
	if (fd < 0)
		return perm_fail("open own setuid file for write failed");
	close(fd);
	if (stat(suidwrite, &st) != 0)
		return perm_fail("stat after setuid write failed");
	if (st.st_mode & (S_ISUID | S_ISGID))
		return perm_fail("write did not clear setuid/setgid bits");
	printf("write clears suid/sgid ok\n");
	unlink(suidwrite);

	char *argv[] = { "noexec", NULL };

	if (execve(noexec, argv, NULL) != -1)
		return perm_fail("exec of 0644 file succeeded");
	if (errno != EACCES)
		return perm_fail("exec of 0644 file: wrong errno");
	if (geteuid() != 1000)
		return perm_fail("failed exec changed euid");

	return 0;
}

static int test_perm(void)
{
	char secret[128], readable[128], privdir[128], sticky[128];
	char rootfile[128], noexec[128], execok[128], own[128];
	char umasked[128], suidtest[128], sgidtest[128];
	struct stat st;
	mode_t old_umask;
	pid_t pid;
	int status, fd;

	snprintf(perm_dir, sizeof(perm_dir), "/tmp/brktest_perm_%ld",
		 (long)getpid());

	perm_join(secret, sizeof(secret), "secret");
	perm_join(readable, sizeof(readable), "readable");
	perm_join(privdir, sizeof(privdir), "privdir");
	perm_join(sticky, sizeof(sticky), "sticky");
	perm_join(rootfile, sizeof(rootfile), "sticky/rootfile");
	perm_join(noexec, sizeof(noexec), "noexec");
	perm_join(execok, sizeof(execok), "execok");
	perm_join(own, sizeof(own), "owned");
	perm_join(umasked, sizeof(umasked), "umasked");
	perm_join(suidtest, sizeof(suidtest), "suidtest");
	perm_join(sgidtest, sizeof(sgidtest), "sgidtest");

	unlink(secret);
	unlink(readable);
	unlink(noexec);
	unlink(execok);
	unlink(own);
	unlink(umasked);
	unlink(rootfile);
	unlink(suidtest);
	unlink(sgidtest);
	rmdir(sticky);
	rmdir(privdir);
	rmdir(perm_dir);

	mkdir("/tmp", 0755);
	if (mkdir(perm_dir, 0777) != 0)
		return perm_fail("mkdir test directory failed");
	chmod(perm_dir, 0777);

	old_umask = umask(0);

	fd = open(secret, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return perm_fail("create secret failed");
	close(fd);

	fd = open(readable, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return perm_fail("create readable failed");
	close(fd);

	fd = open(noexec, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return perm_fail("create noexec failed");
	close(fd);

	if (link("/bin/true", execok) != 0)
		return perm_fail("hardlink executable failed");

	if (perm_copy("/usr/bin/brktest", suidtest) != 0)
		return perm_fail("copy setuid test binary failed");
	if (chmod(suidtest, 04755) != 0)
		return perm_fail("chmod setuid test binary failed");

	if (perm_copy("/usr/bin/brktest", sgidtest) != 0)
		return perm_fail("copy setgid test binary failed");
	if (chown(sgidtest, 0, 2000) != 0)
		return perm_fail("chown setgid test binary failed");
	if (chmod(sgidtest, 02755) != 0)
		return perm_fail("chmod setgid test binary failed");

	if (mkdir(privdir, 0755) != 0)
		return perm_fail("mkdir privdir failed");
	if (mkdir(sticky, 0777) != 0)
		return perm_fail("mkdir sticky failed");
	if (chmod(sticky, 01777) != 0)
		return perm_fail("chmod sticky failed");

	fd = open(rootfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return perm_fail("create sticky root file failed");
	close(fd);

	if (stat(secret, &st) != 0 || st.st_uid != 0 || st.st_gid != 0)
		return perm_fail("stat did not report root ownership");

	pid = fork();
	if (pid < 0)
		return perm_fail("fork failed");
	if (pid == 0)
		_exit(perm_child() == 0 ? 0 : 1);

	if (waitpid(pid, &status, 0) < 0)
		return perm_fail("waitpid failed");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return perm_fail("non-root permission child failed");

	if (stat(own, &st) != 0 || st.st_uid != 1000 || st.st_gid != 1000)
		return perm_fail("new file did not inherit creator uid/gid");

	if (chown(readable, 1000, 1000) != 0)
		return perm_fail("root chown failed");
	if (stat(readable, &st) != 0 || st.st_uid != 1000 || st.st_gid != 1000)
		return perm_fail("chown did not update uid/gid");
	if (chown(readable, 0, 0) != 0)
		return perm_fail("root chown back failed");

	fd = open(secret, O_RDONLY);
	if (fd < 0)
		return perm_fail("root cannot read 0600 file");
	close(fd);

	umask(old_umask);

	unlink(secret);
	unlink(readable);
	unlink(noexec);
	unlink(execok);
	unlink(own);
	unlink(umasked);
	unlink(rootfile);
	unlink(suidtest);
	unlink(sgidtest);
	rmdir(sticky);
	rmdir(privdir);
	rmdir(perm_dir);

	printf("permission model ok\n");
	return 0;
}

#define GROUP_DIR_MAX 64

static char group_dir[GROUP_DIR_MAX];

static void group_join(char *buf, size_t size, const char *name)
{
	snprintf(buf, size, "%s/%s", group_dir, name);
}

static int test_group(void)
{
	char file[128];
	struct stat st;
	pid_t pid;
	int status, fd;

	snprintf(group_dir, sizeof(group_dir), "/tmp/brktest_group_%ld",
		 (long)getpid());
	group_join(file, sizeof(file), "file");

	unlink(file);
	rmdir(group_dir);

	mkdir("/tmp", 0755);
	if (mkdir(group_dir, 0777) != 0)
		return perm_fail("mkdir group test directory failed");
	chmod(group_dir, 0777);

	fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
	if (fd < 0)
		return perm_fail("create group test file failed");
	close(fd);

	if (chown(file, 0, 2000) != 0)
		return perm_fail("chown group test file failed");
	chmod(file, 0640);

	pid = fork();
	if (pid < 0)
		return perm_fail("fork for group test failed");
	if (pid == 0) {
		if (setgid(1000) != 0 || setuid(1000) != 0)
			_exit(1);
		if (getgroups(0, NULL) != 0)
			_exit(2);
		fd = open(file, O_RDONLY);
		if (fd >= 0) {
			close(fd);
			_exit(3);
		}
		if (errno != EACCES)
			_exit(4);
		fd = open(file, O_WRONLY);
		if (fd >= 0) {
			close(fd);
			_exit(5);
		}
		if (errno != EACCES)
			_exit(6);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) < 0)
		return perm_fail("waitpid for group test failed");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return perm_fail("group permission denial test failed");

	if (chown(file, 0, 3000) != 0)
		return perm_fail("chgrp test failed");
	if (stat(file, &st) != 0 || st.st_gid != 3000)
		return perm_fail("chgrp did not update group");

	unlink(file);
	rmdir(group_dir);
	printf("group permissions ok (supplementary groups empty)\n");
	return 0;
}

static int test_auth(void)
{
	static const char abc_hash[] =
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	unsigned char digest[32];
	char hex[65];
	struct passwd *pw;
	struct group *gr;
	struct spwd *sp;
	struct stat st;
	gid_t groups[NGROUPS_MAX];
	int ngroups = NGROUPS_MAX;

	sha256("abc", 3, digest);
	for (int i = 0; i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", digest[i]);
	if (strcmp(hex, abc_hash))
		return perm_fail("sha256 vector mismatch");

	pw = getpwnam("root");
	if (!pw || pw->pw_uid != 0 || pw->pw_gid != 0 ||
	    strcmp(pw->pw_shell, "/bin/sh"))
		return perm_fail("getpwnam(root) failed");

	pw = getpwnam("user");
	if (!pw || pw->pw_uid != 1000 || pw->pw_gid != 1000)
		return perm_fail("getpwnam(user) failed");

	pw = getpwuid(1000);
	if (!pw || strcmp(pw->pw_name, "user"))
		return perm_fail("getpwuid(1000) failed");

	gr = getgrnam("user");
	if (!gr || gr->gr_gid != 1000)
		return perm_fail("getgrnam(user) failed");

	gr = getgrgid(0);
	if (!gr || strcmp(gr->gr_name, "root"))
		return perm_fail("getgrgid(0) failed");

	if (getgrouplist("user", 1000, groups, &ngroups) != 1 ||
	    groups[0] != 1000)
		return perm_fail("getgrouplist failed");

	sp = getspnam("root");
	if (!sp || pw_verify("root", sp->sp_pwdp) != 0)
		return perm_fail("root password hash mismatch");
	if (pw_verify("wrong", sp->sp_pwdp) == 0)
		return perm_fail("wrong password accepted");

	sp = getspnam("user");
	if (!sp || pw_verify("user", sp->sp_pwdp) != 0)
		return perm_fail("user password hash mismatch");

	if (stat("/etc/shadow", &st) != 0 || st.st_uid != 0 ||
	    (st.st_mode & 0777) != 0600)
		return perm_fail("/etc/shadow ownership/mode is wrong");

	printf("password database and hashes ok\n");
	return 0;
}

static int test_posix(void)
{
	char dir[64], file[128], fifo[128], link[128];
	char wbuf1[] = "hello ";
	char wbuf2[] = "world";
	char rbuf1[8] = { 0 };
	char rbuf2[8] = { 0 };
	struct iovec iov[2];
	struct timespec ts[2], now;
	struct rusage ru;
	struct pollfd pfd;
	struct sigaction sa = { .sa_handler = on_sigusr1 };
	fd_set rfds;
	sigset_t set, pend;
	struct stat st;
	char *mem = NULL;
	int fd = -1, p[2] = { -1, -1 };
	int status;
	pid_t pid;
	int ret = -1;

	snprintf(dir, sizeof(dir), "/tmp/brktest_posix_%ld", (long)getpid());
	snprintf(file, sizeof(file), "%s/file", dir);
	snprintf(fifo, sizeof(fifo), "%s/fifo", dir);
	snprintf(link, sizeof(link), "%s/link", dir);

	unlink(file);
	unlink(fifo);
	unlink(link);
	rmdir(dir);

	mkdir("/tmp", 0755);
	if (mkdir(dir, 0755) != 0)
		return perm_fail("mkdir posix test directory failed");

	if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
	    clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
	    clock_getres(CLOCK_MONOTONIC, &now) != 0 || now.tv_nsec <= 0)
		goto out;
	(void)time(NULL);

	ts[0].tv_sec = 0;
	ts[0].tv_nsec = 1000000;
	ts[1].tv_sec = 0;
	ts[1].tv_nsec = 1000000;
	if (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts[0], NULL) != 0)
		goto out;

	if (getrusage(RUSAGE_SELF, &ru) != 0 ||
	    getrusage(RUSAGE_CHILDREN, &ru) != 0)
		goto out;

	fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		goto out;

	iov[0].iov_base = wbuf1;
	iov[0].iov_len = 6;
	iov[1].iov_base = wbuf2;
	iov[1].iov_len = 5;
	if (writev(fd, iov, 2) != 11 || lseek(fd, 0, SEEK_SET) != 0)
		goto out;

	iov[0].iov_base = rbuf1;
	iov[0].iov_len = 6;
	iov[1].iov_base = rbuf2;
	iov[1].iov_len = 5;
	if (readv(fd, iov, 2) != 11 || memcmp(rbuf1, "hello ", 6) ||
	    memcmp(rbuf2, "world", 5))
		goto out;

	ts[0].tv_sec = 1234;
	ts[0].tv_nsec = 0;
	ts[1].tv_sec = 5678;
	ts[1].tv_nsec = 0;
	if (futimens(fd, ts) != 0 || fstat(fd, &st) != 0 || st.st_mtime != 5678)
		goto out;

	if (fstatat(AT_FDCWD, file, &st, 0) != 0 || st.st_size != 11)
		goto out;

	if (mkfifo(fifo, 0644) != 0 || stat(fifo, &st) != 0 ||
	    !S_ISFIFO(st.st_mode))
		goto out;

	if (symlink(file, link) != 0 || lchown(link, 1000, 1000) != 0 ||
	    lstat(link, &st) != 0 || st.st_uid != 1000 || st.st_gid != 1000)
		goto out;

	if (dup3(fd, 10, O_CLOEXEC) != 10)
		goto out;
	close(10);
	close(fd);
	fd = -1;

	if (pipe(p) != 0 || write(p[1], "x", 1) != 1)
		goto out;

	FD_ZERO(&rfds);
	FD_SET(p[0], &rfds);
	ts[0].tv_sec = 0;
	ts[0].tv_nsec = 0;
	if (pselect(p[0] + 1, &rfds, NULL, NULL, &ts[0], NULL) != 1)
		goto out;

	pfd.fd = p[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (ppoll(&pfd, 1, &ts[0], NULL) != 1 || !(pfd.revents & POLLIN))
		goto out;

	mem = malloc(4096);
	if (!mem || mlock(mem, 4096) != 0 || munlock(mem, 4096) != 0 ||
	    mlockall(MCL_CURRENT) != 0 || munlockall() != 0)
		goto out;

	if (sigaction(SIGUSR1, &sa, NULL) != 0)
		goto out;
	set = (sigset_t)1 << SIGUSR1;
	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0 || raise(SIGUSR1) != 0 ||
	    sigpending(&pend) != 0 || !(pend & set))
		goto out;
	caught = 0;
	if (sigprocmask(SIG_UNBLOCK, &set, NULL) != 0 || !caught)
		goto out;

	pid = fork();
	if (pid < 0)
		goto out;
	if (pid == 0) {
		if (sigaction(SIGUSR1, &sa, NULL) != 0)
			_exit(1);
		pause();
		_exit(0);
	}

	ts[0].tv_sec = 0;
	ts[0].tv_nsec = 50000000;
	if (nanosleep(&ts[0], NULL) != 0 || kill(pid, SIGUSR1) != 0)
		goto out;
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		goto out;

	ret = 0;
out:
	if (fd >= 0)
		close(fd);
	if (p[0] >= 0)
		close(p[0]);
	if (p[1] >= 0)
		close(p[1]);
	free(mem);
	unlink(file);
	unlink(fifo);
	unlink(link);
	rmdir(dir);
	if (ret)
		return perm_fail("posix syscall test failed");
	printf("posix syscall batch ok\n");
	return 0;
}

static int test_symlink(void)
{
	char dir[64], target[128], abs_target[128], rel[128], abs[128];
	char dirlink[128], chain[128], dangling[128], subfile[128];
	char subup[128], loop_a[128], loop_b[128], exec_link[128];
	char sub[64];
	char buf[64];
	struct stat st;
	pid_t pid;
	int status, fd, n;

	snprintf(dir, sizeof(dir), "/tmp/brktest_symlink_%ld", (long)getpid());
	snprintf(target, sizeof(target), "%s/target", dir);
	snprintf(abs_target, sizeof(abs_target), "%s/abs_target", dir);
	snprintf(rel, sizeof(rel), "%s/rel", dir);
	snprintf(abs, sizeof(abs), "%s/abs", dir);
	snprintf(dirlink, sizeof(dirlink), "%s/dirlink", dir);
	snprintf(chain, sizeof(chain), "%s/chain", dir);
	snprintf(dangling, sizeof(dangling), "%s/dangling", dir);
	snprintf(subfile, sizeof(subfile), "%s/sub/file", dir);
	snprintf(subup, sizeof(subup), "%s/sub/up", dir);
	snprintf(loop_a, sizeof(loop_a), "%s/loop_a", dir);
	snprintf(loop_b, sizeof(loop_b), "%s/loop_b", dir);
	snprintf(exec_link, sizeof(exec_link), "%s/exec", dir);

	unlink(rel);
	unlink(abs);
	unlink(dirlink);
	unlink(chain);
	unlink(dangling);
	unlink(subup);
	unlink(loop_a);
	unlink(loop_b);
	unlink(exec_link);
	unlink(subfile);
	unlink(target);
	unlink(abs_target);
	rmdir(dir);

	mkdir("/tmp", 0755);
	if (mkdir(dir, 0755) != 0)
		return perm_fail("mkdir symlink test dir failed");

	snprintf(sub, sizeof(sub), "%s/sub", dir);
	if (mkdir(sub, 0755) != 0)
		return perm_fail("mkdir symlink sub dir failed");

	fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return perm_fail("create symlink target failed");
	write(fd, "hello", 5);
	close(fd);

	fd = open(subfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return perm_fail("create sub file failed");
	write(fd, "world", 5);
	close(fd);

	if (symlink("target", rel) != 0 || symlink(target, abs) != 0 ||
	    symlink("sub", dirlink) != 0 || symlink("rel", chain) != 0 ||
	    symlink("missing", dangling) != 0 ||
	    symlink("../target", subup) != 0 ||
	    symlink("loop_b", loop_a) != 0 || symlink("loop_a", loop_b) != 0 ||
	    symlink("/bin/true", exec_link) != 0)
		return perm_fail("create symlink failed");

	if (stat(rel, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 5)
		return perm_fail("stat did not follow symlink");
	if (lstat(rel, &st) != 0 || !S_ISLNK(st.st_mode))
		return perm_fail("lstat did not return symlink");

	n = (int)readlink(rel, buf, sizeof(buf) - 1);
	if (n < 0)
		return perm_fail("readlink failed");
	buf[n] = '\0';
	if (strcmp(buf, "target"))
		return perm_fail("readlink returned wrong target");

	fd = open(rel, O_RDONLY);
	if (fd < 0)
		return perm_fail("open symlink failed");
	n = (int)read(fd, buf, sizeof(buf));
	close(fd);
	if (n != 5 || strncmp(buf, "hello", 5))
		return perm_fail("read through symlink failed");

	fd = open(chain, O_RDONLY);
	if (fd < 0)
		return perm_fail("open symlink chain failed");
	close(fd);

	fd = open(abs, O_RDONLY);
	if (fd < 0)
		return perm_fail("open absolute symlink failed");
	close(fd);

	fd = open("dirlink/file", O_RDONLY);
	if (fd >= 0) {
		close(fd);
		return perm_fail("relative path was resolved against cwd");
	}

	if (chdir(dir) != 0)
		return perm_fail("chdir symlink dir failed");
	fd = open("dirlink/file", O_RDONLY);
	if (fd < 0)
		return perm_fail("intermediate symlink lookup failed");
	n = (int)read(fd, buf, sizeof(buf));
	close(fd);
	if (n != 5 || strncmp(buf, "world", 5))
		return perm_fail("read through directory symlink failed");

	fd = open("sub/up", O_RDONLY);
	if (fd < 0)
		return perm_fail("relative symlink with .. failed");
	close(fd);
	if (chdir("/") != 0)
		return perm_fail("chdir / failed");

	if (open(dangling, O_RDONLY) >= 0)
		return perm_fail("open dangling symlink succeeded");
	if (errno != ENOENT)
		return perm_fail("open dangling symlink: wrong errno");
	if (lstat(dangling, &st) != 0 || !S_ISLNK(st.st_mode))
		return perm_fail("lstat dangling symlink failed");

	if (stat(loop_a, &st) == 0)
		return perm_fail("symlink loop did not fail");
	if (errno != ELOOP)
		return perm_fail("symlink loop: wrong errno");

	fd = open(rel, O_RDONLY | O_NOFOLLOW);
	if (fd >= 0) {
		close(fd);
		return perm_fail("O_NOFOLLOW opened symlink");
	}
	if (errno != ELOOP)
		return perm_fail("O_NOFOLLOW: wrong errno");

	if (chmod(rel, 0600) != 0)
		return perm_fail("chmod through symlink failed");
	if (stat(target, &st) != 0 || (st.st_mode & 0777) != 0600)
		return perm_fail("chmod did not follow symlink");
	if (lstat(rel, &st) != 0 || (st.st_mode & 0777) != 0777)
		return perm_fail("chmod changed symlink mode");

	pid = fork();
	if (pid < 0)
		return perm_fail("fork for symlink exec failed");
	if (pid == 0) {
		char *argv[] = { "true", NULL };

		execve(exec_link, argv, NULL);
		_exit(77);
	}
	if (waitpid(pid, &status, 0) < 0)
		return perm_fail("waitpid for symlink exec failed");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return perm_fail("exec through symlink failed");

	if (unlink(rel) != 0)
		return perm_fail("unlink symlink failed");
	if (stat(target, &st) != 0 || !S_ISREG(st.st_mode))
		return perm_fail("unlink removed the target");

	unlink(abs);
	unlink(dirlink);
	unlink(chain);
	unlink(dangling);
	unlink(loop_a);
	unlink(loop_b);
	unlink(exec_link);
	unlink(subup);
	unlink(subfile);
	unlink(target);
	rmdir(sub);
	rmdir(dir);
	printf("symlink lookup ok\n");
	return 0;
}

static int test_net(void)
{
	struct in_addr in;
	char *text;
	struct hostent *he;

	if (!inet_aton("127.0.0.1", &in))
		return perm_fail("inet_aton failed");
	text = inet_ntoa(in);
	if (strcmp(text, "127.0.0.1"))
		return perm_fail("inet_ntoa failed");
	if (ntohs(htons(0x1234)) != 0x1234 ||
	    ntohl(htonl(0x12345678)) != 0x12345678)
		return perm_fail("byte-order helpers failed");

	he = gethostbyname("10.0.2.2");
	if (!he || he->h_length != 4)
		return perm_fail("gethostbyname numeric address failed");

	/* TCP: listener, connect, accept4, poll, send/recv, getsockname. */
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = 0,
		.sin_addr.s_addr = INADDR_LOOPBACK,
	};

	if (lfd < 0 || bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
	    listen(lfd, 1) != 0)
		return perm_fail("TCP listener setup failed");

	socklen_t alen = sizeof(sa);
	if (getsockname(lfd, (struct sockaddr *)&sa, &alen) != 0)
		return perm_fail("getsockname failed");
	unsigned short port = ntohs(sa.sin_port);
	if (!port)
		return perm_fail("getsockname returned port 0");

	pid_t pid = fork();
	if (pid < 0)
		return perm_fail("fork for TCP test failed");
	if (pid == 0) {
		int cfd = socket(AF_INET, SOCK_STREAM, 0);
		char buf[8];

		if (cfd < 0 ||
		    connect(cfd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
			_exit(1);
		if (send(cfd, "ping", 4, 0) != 4)
			_exit(2);
		if (recv(cfd, buf, 4, 0) != 4 || memcmp(buf, "pong", 4))
			_exit(3);
		close(cfd);
		_exit(0);
	}

	struct pollfd pfd = { .fd = lfd, .events = POLLIN };
	if (poll(&pfd, 1, 3000) != 1)
		return perm_fail("TCP listener poll failed");
	int cfd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK);
	if (cfd < 0)
		return perm_fail("accept4 failed");

	pfd.fd = cfd;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 3000) != 1)
		return perm_fail("TCP accept poll failed");
	char buf[8];
	if (recv(cfd, buf, 4, 0) != 4 || memcmp(buf, "ping", 4))
		return perm_fail("TCP server recv failed");
	if (send(cfd, "pong", 4, 0) != 4)
		return perm_fail("TCP server send failed");
	close(cfd);
	close(lfd);

	int status;
	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return perm_fail("TCP loopback test failed");

	/* UDP: select, sendto/recvfrom. */
	int ufd = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in usa = {
		.sin_family = AF_INET,
		.sin_port = 0,
		.sin_addr.s_addr = INADDR_LOOPBACK,
	};

	if (ufd < 0 || bind(ufd, (struct sockaddr *)&usa, sizeof(usa)) != 0)
		return perm_fail("UDP bind failed");
	alen = sizeof(usa);
	if (getsockname(ufd, (struct sockaddr *)&usa, &alen) != 0)
		return perm_fail("UDP getsockname failed");

	pid = fork();
	if (pid < 0)
		return perm_fail("fork for UDP test failed");
	if (pid == 0) {
		close(ufd);
		int cfd = socket(AF_INET, SOCK_DGRAM, 0);
		char rbuf[8];

		if (cfd < 0)
			_exit(1);
		if (sendto(cfd, "u", 1, 0, (struct sockaddr *)&usa,
			   sizeof(usa)) < 0) {
			printf("UDP sendto errno %d\n", errno);
			_exit(2);
		}
		if (recv(cfd, rbuf, 1, 0) != 1 || rbuf[0] != 'v')
			_exit(3);
		close(cfd);
		_exit(0);
	}

	fd_set rfds;
	struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
	struct sockaddr_in peer;
	socklen_t plen = sizeof(peer);

	FD_ZERO(&rfds);
	FD_SET(ufd, &rfds);
	if (select(ufd + 1, &rfds, NULL, NULL, &tv) != 1)
		return perm_fail("UDP select failed");
	if (recvfrom(ufd, buf, 1, 0, (struct sockaddr *)&peer, &plen) != 1 ||
	    buf[0] != 'u')
		return perm_fail("UDP recvfrom failed");
	if (sendto(ufd, "v", 1, 0, (struct sockaddr *)&peer, plen) != 1) {
		printf("UDP sendto errno %d plen %u\n", errno,
		       (unsigned int)plen);
		return perm_fail("UDP sendto failed");
	}
	close(ufd);

	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return perm_fail("UDP loopback test failed");

	printf("network sockets ok (tcp port %u)\n", port);
	return 0;
}

struct test_suite {
	const char *name;
	int (*run)(void);
};

static const struct test_suite suites[] = {
	{ "fork", test_fork },	     { "kill", test_kill },
	{ "sig", test_signal },	     { "meta", test_meta },
	{ "perm", test_perm },	     { "group", test_group },
	{ "auth", test_auth },	     { "posix", test_posix },
	{ "symlink", test_symlink }, { "net", test_net },
};

#define NR_SUITES (sizeof(suites) / sizeof(suites[0]))

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [suite|all]\n\n"
		"Suites:\n"
		"  fork   process creation and wait\n"
		"  kill   exit status and signal delivery\n"
		"  sig    signal handlers, masks and SIGWINCH\n"
		"  meta   chmod, utime, utimes, utimensat and statvfs\n"
		"  perm   uid/gid credentials, DAC checks and umask\n"
		"  group  supplementary groups and group permissions\n"
		"  auth   password hashes and user/group database\n"
		"  posix  clock, iovec, futimens, fstatat, mkfifo, pselect/ppoll\n"
		"  symlink symlink following, lstat, NOFOLLOW and loops\n"
		"  net    TCP/UDP loopback sockets and inet helpers\n"
		"  all    run every suite (default)\n",
		prog);
}

static int run_suite(const struct test_suite *suite)
{
	int ret;

	printf("\n=== %s ===\n", suite->name);
	ret = suite->run();
	if (ret == 0) {
		printf("=== %s: ok ===\n", suite->name);
		return 0;
	}
	printf("=== %s: FAILED ===\n", suite->name);
	return -1;
}

int main(int argc, char *argv[])
{
	const char *what = argc > 1 ? argv[1] : "all";
	int failures = 0;
	int total = 0;

	if (!strcmp(what, "-h") || !strcmp(what, "--help")) {
		usage(argv[0]);
		return 0;
	}

	if (!strcmp(what, "-check-euid"))
		return (getuid() == 1000 && geteuid() == 0) ? 0 : 1;
	if (!strcmp(what, "-check-egid"))
		return (getgid() == 1000 && getegid() == 2000) ? 0 : 1;
	if (!strcmp(what, "all")) {
		total = (int)NR_SUITES;
		for (size_t i = 0; i < NR_SUITES; i++)
			if (run_suite(&suites[i]) != 0)
				failures++;
	} else {
		for (size_t i = 0; i < NR_SUITES; i++) {
			if (!strcmp(what, suites[i].name)) {
				total = 1;
				if (run_suite(&suites[i]) != 0)
					failures++;
				goto done;
			}
		}
		fprintf(stderr, "%s: unknown suite: %s\n\n", argv[0], what);
		usage(argv[0]);
		return 1;
	}

done:
	printf("\n%d suite(s), %d failed\n", total, failures);
	return failures ? 1 : 0;
}
