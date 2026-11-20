#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define STRESS_MAX_WORKERS 16

static unsigned int workers(struct test_ctx *ctx)
{
	unsigned int n = ctx->workers ? ctx->workers : 1;

	return n > STRESS_MAX_WORKERS ? STRESS_MAX_WORKERS : n;
}

static unsigned int iterations(struct test_ctx *ctx)
{
	return ctx->iterations ? ctx->iterations : 1;
}

static int case_process_churn(struct test_ctx *ctx)
{
	unsigned int iters = iterations(ctx);
	unsigned int nworkers = workers(ctx);

	for (unsigned int iter = 0; iter < iters; iter++) {
		pid_t pids[STRESS_MAX_WORKERS];

		for (unsigned int w = 0; w < nworkers; w++) {
			pids[w] = fork();
			if (pids[w] < 0)
				return test_fail_msg("stress_process: fork failed");
			if (pids[w] == 0)
				_exit((int)((w + iter) & 0x7f));
		}

		for (unsigned int w = 0; w < nworkers; w++) {
			int status;

			if (waitpid(pids[w], &status, 0) != pids[w] ||
			    !WIFEXITED(status) ||
			    WEXITSTATUS(status) != (int)((w + iter) & 0x7f))
				return test_fail_msg(
					"stress_process: child status mismatch");
		}
	}
	return 0;
}

static int case_exec_churn(struct test_ctx *ctx)
{
	unsigned int iters = iterations(ctx);

	for (unsigned int i = 0; i < iters; i++) {
		pid_t pid = fork();
		int status;

		if (pid < 0)
			return test_fail_msg("stress_process: fork failed");
		if (pid == 0) {
			char *argv[] = { "true", NULL };
			char *envp[] = { NULL };

			execve("/bin/true", argv, envp);
			_exit(127);
		}
		if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
		    WEXITSTATUS(status) != 0)
			return test_fail_msg("stress_process: exec/exit failed");
	}
	return 0;
}

static int case_fd_churn(struct test_ctx *ctx)
{
	char dir[128], path[160];
	unsigned int iters = iterations(ctx);
	int fd;

	if (test_tmpdir(dir, sizeof(dir), "stress_fd") != 0)
		return test_fail_msg("stress_fd: cannot create temp dir");
	int n = snprintf(path, sizeof(path), "%s/file", dir);

	if (n < 0 || (size_t)n >= sizeof(path))
		return -1;
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("stress_fd: open failed: %d", errno);
	close(fd);

	for (unsigned int i = 0; i < iters; i++) {
		int fds[8];
		int dups[8];

		for (unsigned int j = 0; j < 8; j++) {
			fds[j] = open(path, O_RDWR);
			if (fds[j] < 0)
				return test_fail_msg("stress_fd: open loop failed");
			dups[j] = dup(fds[j]);
			if (dups[j] < 0)
				return test_fail_msg("stress_fd: dup failed");
		}
		for (unsigned int j = 0; j < 8; j++) {
			close(dups[j]);
			close(fds[j]);
		}

		int p[2];

		if (pipe(p) != 0)
			return test_fail_msg("stress_fd: pipe failed");
		close(p[0]);
		close(p[1]);
	}

	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_fs_churn(struct test_ctx *ctx)
{
	char dir[128];
	unsigned int iters = iterations(ctx);
	unsigned int nworkers = workers(ctx);
	pid_t pids[STRESS_MAX_WORKERS];

	if (test_tmpdir(dir, sizeof(dir), "stress_fs") != 0)
		return test_fail_msg("stress_fs: cannot create temp dir");

	for (unsigned int w = 0; w < nworkers; w++) {
		pids[w] = fork();
		if (pids[w] < 0)
			return test_fail_msg("stress_fs: fork failed");
		if (pids[w] == 0) {
			char path[192], newpath[192];
			char data[256];

			for (unsigned int i = 0; i < iters; i++) {
				int fd;

				snprintf(path, sizeof(path), "%s/w%u_%u", dir, w,
					 i);
				snprintf(newpath, sizeof(newpath),
					 "%s/r%u_%u", dir, w, i);
				for (size_t k = 0; k < sizeof(data); k++)
					data[k] = (char)(w + i + k);

				fd = open(path,
					  O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (fd < 0)
					_exit(2);
				if (write(fd, data, sizeof(data)) !=
				    (ssize_t)sizeof(data))
					_exit(3);
				close(fd);
				if (rename(path, newpath) != 0)
					_exit(4);
				struct stat st;
				if (stat(newpath, &st) != 0 ||
				    st.st_size != (off_t)sizeof(data))
					_exit(5);
				if (unlink(newpath) != 0)
					_exit(6);
			}
			_exit(0);
		}
	}

	for (unsigned int w = 0; w < nworkers; w++) {
		int status;

		if (waitpid(pids[w], &status, 0) != pids[w] ||
		    !WIFEXITED(status) || WEXITSTATUS(status) != 0)
			return test_fail_msg("stress_fs: worker failed");
	}
	if (rmdir(dir) != 0)
		return test_fail_msg("stress_fs: directory is not empty");
	return 0;
}

static int case_io_transfer(struct test_ctx *ctx)
{
	unsigned int iters = iterations(ctx);

	for (unsigned int i = 0; i < iters; i++) {
		int p[2];
		pid_t pid;
		int status;
		unsigned char buf[4096];
		size_t total = 0;

		if (pipe(p) != 0)
			return test_fail_msg("stress_io: pipe failed");
		pid = fork();
		if (pid < 0)
			return test_fail_msg("stress_io: fork failed");
		if (pid == 0) {
			unsigned char rbuf[4096];
			size_t nread = 0;

			close(p[1]);
			for (;;) {
				ssize_t n = read(p[0], rbuf, sizeof(rbuf));

				if (n < 0) {
					if (errno == EINTR)
						continue;
					_exit(2);
				}
				if (n == 0)
					break;
				for (ssize_t k = 0; k < n; k++) {
					if (rbuf[k] !=
					    (unsigned char)(nread + k))
						_exit(3);
				}
				nread += (size_t)n;
			}
			close(p[0]);
			_exit(nread == 64 * 1024 ? 0 : 4);
		}

		close(p[0]);
		while (total < 64 * 1024) {
			size_t chunk = sizeof(buf);

			if (chunk > 64 * 1024 - total)
				chunk = 64 * 1024 - total;
			for (size_t k = 0; k < chunk; k++)
				buf[k] = (unsigned char)(total + k);
			ssize_t n = write(p[1], buf, chunk);

			if (n < 0) {
				if (errno == EINTR)
					continue;
				return test_fail_msg(
					"stress_io: write failed: %d", errno);
			}
			total += (size_t)n;
		}
		close(p[1]);
		if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
		    WEXITSTATUS(status) != 0)
			return test_fail_msg("stress_io: reader failed");
	}
	return 0;
}

static int case_memory_churn(struct test_ctx *ctx)
{
	unsigned int iters = iterations(ctx);

	for (unsigned int i = 0; i < iters; i++) {
		unsigned char *p = malloc(64 * 1024);

		if (!p)
			return test_fail_msg("stress_memory: malloc failed");
		for (size_t k = 0; k < 64 * 1024; k++)
			p[k] = (unsigned char)(i + k);
		for (size_t k = 0; k < 64 * 1024; k++) {
			if (p[k] != (unsigned char)(i + k))
				return test_fail_msg(
					"stress_memory: malloc data mismatch");
		}
		free(p);

		p = mmap(NULL, 64 * 1024, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED)
			return test_fail_msg("stress_memory: mmap failed");
		p[0] = (unsigned char)i;
		p[64 * 1024 - 1] = (unsigned char)(i + 1);
		if (munmap(p, 64 * 1024) != 0)
			return test_fail_msg("stress_memory: munmap failed");
	}
	return 0;
}

static int case_signal_churn(struct test_ctx *ctx)
{
	unsigned int iters = iterations(ctx);

	for (unsigned int i = 0; i < iters; i++) {
		pid_t pid = fork();
		int status;

		if (pid < 0)
			return test_fail_msg("stress_signal: fork failed");
		if (pid == 0) {
			for (;;)
				pause();
			_exit(0);
		}
		if (kill(pid, SIGTERM) != 0)
			return test_fail_msg("stress_signal: kill failed");
		if (waitpid(pid, &status, 0) != pid ||
		    !WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM)
			return test_fail_msg("stress_signal: status mismatch");
	}
	return 0;
}

static int case_net_churn(struct test_ctx *ctx)
{
	unsigned int iters = iterations(ctx);

	for (unsigned int i = 0; i < iters; i++) {
		int lfd = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in sa = {
			.sin_family = AF_INET,
			.sin_port = 0,
			.sin_addr.s_addr = INADDR_LOOPBACK,
		};
		socklen_t alen = sizeof(sa);
		pid_t pid;
		int status;
		char buf[8];

		if (lfd < 0 || bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
		    listen(lfd, 4) != 0 ||
		    getsockname(lfd, (struct sockaddr *)&sa, &alen) != 0)
			return test_fail_msg("stress_net: listener setup failed");

		pid = fork();
		if (pid < 0)
			return test_fail_msg("stress_net: fork failed");
		if (pid == 0) {
			int cfd = socket(AF_INET, SOCK_STREAM, 0);

			if (cfd < 0 ||
			    connect(cfd, (struct sockaddr *)&sa, sizeof(sa)) !=
				    0) {
				_exit(2);
			}
			if (send(cfd, "ping", 4, 0) != 4)
				_exit(3);
			if (recv(cfd, buf, 4, 0) != 4 || memcmp(buf, "pong", 4))
				_exit(4);
			close(cfd);
			_exit(0);
		}

		int cfd = accept(lfd, NULL, NULL);

		if (cfd < 0)
			return test_fail_msg("stress_net: accept failed");
		if (recv(cfd, buf, 4, 0) != 4 || memcmp(buf, "ping", 4))
			return test_fail_msg("stress_net: server recv failed");
		if (send(cfd, "pong", 4, 0) != 4)
			return test_fail_msg("stress_net: server send failed");
		close(cfd);
		close(lfd);
		if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
		    WEXITSTATUS(status) != 0)
			return test_fail_msg("stress_net: client failed");
	}
	return 0;
}

static int case_mixed(struct test_ctx *ctx)
{
	if (case_fd_churn(ctx) != 0)
		return -1;
	if (case_io_transfer(ctx) != 0)
		return -1;
	if (case_memory_churn(ctx) != 0)
		return -1;
	return 0;
}

static const struct test_case stress_process_cases[] = {
	{ "churn", case_process_churn, TEST_OWN_PGRP, 20000 },
	{ "exec", case_exec_churn, TEST_OWN_PGRP, 20000 },
};

static const struct test_case stress_fd_cases[] = {
	{ "churn", case_fd_churn, TEST_OWN_PGRP, 20000 },
};

static const struct test_case stress_fs_cases[] = {
	{ "churn", case_fs_churn, TEST_OWN_PGRP, 30000 },
};

static const struct test_case stress_io_cases[] = {
	{ "transfer", case_io_transfer, TEST_OWN_PGRP, 30000 },
};

static const struct test_case stress_memory_cases[] = {
	{ "churn", case_memory_churn, TEST_OWN_PGRP, 20000 },
};

static const struct test_case stress_net_cases[] = {
	{ "churn", case_net_churn, TEST_OWN_PGRP, 30000 },
};

static const struct test_case stress_signal_cases[] = {
	{ "churn", case_signal_churn, TEST_OWN_PGRP, 20000 },
};

static const struct test_case stress_mixed_cases[] = {
	{ "mixed", case_mixed, TEST_OWN_PGRP, 60000 },
};

const struct test_suite brktest_stress_process_suite = {
	.name = "stress_process",
	.description = "process fork/wait and exec churn",
	.cases = stress_process_cases,
	.nr_cases = sizeof(stress_process_cases) /
		    sizeof(stress_process_cases[0]),
	.flags = TEST_SUITE_STRESS,
};

const struct test_suite brktest_stress_fd_suite = {
	.name = "stress_fd",
	.description = "file descriptor and pipe churn",
	.cases = stress_fd_cases,
	.nr_cases = sizeof(stress_fd_cases) / sizeof(stress_fd_cases[0]),
	.flags = TEST_SUITE_STRESS,
};

const struct test_suite brktest_stress_fs_suite = {
	.name = "stress_fs",
	.description = "concurrent file create/rename/unlink churn",
	.cases = stress_fs_cases,
	.nr_cases = sizeof(stress_fs_cases) / sizeof(stress_fs_cases[0]),
	.flags = TEST_SUITE_STRESS,
};

const struct test_suite brktest_stress_io_suite = {
	.name = "stress_io",
	.description = "repeated pipe transport and checksums",
	.cases = stress_io_cases,
	.nr_cases = sizeof(stress_io_cases) / sizeof(stress_io_cases[0]),
	.flags = TEST_SUITE_STRESS,
};

const struct test_suite brktest_stress_memory_suite = {
	.name = "stress_memory",
	.description = "malloc/mmap allocation churn",
	.cases = stress_memory_cases,
	.nr_cases = sizeof(stress_memory_cases) / sizeof(stress_memory_cases[0]),
	.flags = TEST_SUITE_STRESS,
};

const struct test_suite brktest_stress_net_suite = {
	.name = "stress_net",
	.description = "short-lived TCP connection churn",
	.cases = stress_net_cases,
	.nr_cases = sizeof(stress_net_cases) / sizeof(stress_net_cases[0]),
	.flags = TEST_SUITE_STRESS,
};

const struct test_suite brktest_stress_signal_suite = {
	.name = "stress_signal",
	.description = "SIGTERM/kill/wait churn",
	.cases = stress_signal_cases,
	.nr_cases = sizeof(stress_signal_cases) / sizeof(stress_signal_cases[0]),
	.flags = TEST_SUITE_STRESS,
};

const struct test_suite brktest_stress_mixed_suite = {
	.name = "stress_mixed",
	.description = "mixed fd, pipe and memory load",
	.cases = stress_mixed_cases,
	.nr_cases = sizeof(stress_mixed_cases) / sizeof(stress_mixed_cases[0]),
	.flags = TEST_SUITE_STRESS,
};
