#include "sh_pipeline.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void sh_pipeline_init(struct sh_pipeline *p)
{
	p->cmds = NULL;
	p->nr_cmds = 0;
}

static void redir_list_release(struct sh_redir *r)
{
	while (r) {
		struct sh_redir *n = r->next;

		free(r->path);
		free(r);
		r = n;
	}
}

void sh_pipeline_release(struct sh_pipeline *p)
{
	size_t i;

	if (!p)
		return;

	for (i = 0; i < p->nr_cmds; i++) {
		struct sh_simple_cmd *c = &p->cmds[i];

		if (c->argv) {
			size_t j;

			for (j = 0; c->argv[j]; j++)
				free(c->argv[j]);
			free(c->argv);
		}
		redir_list_release(c->redir_list);
		c->argv = NULL;
		c->argc = 0;
		c->redir_list = NULL;
	}
	free(p->cmds);
	p->cmds = NULL;
	p->nr_cmds = 0;
}

static int apply_redirs_child(const struct sh_simple_cmd *c)
{
	struct sh_redir *r;

	for (r = c->redir_list; r; r = r->next) {
		int fd;
		mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

		if (r->kind == SH_REDIR_IN) {
			fd = open(r->path, O_RDONLY);
		} else if (r->kind == SH_REDIR_OUT) {
			fd = open(r->path, O_WRONLY | O_CREAT | O_TRUNC, mode);
		} else {
			fd = open(r->path, O_WRONLY | O_CREAT | O_APPEND, mode);
		}
		if (fd < 0) {
			fprintf(stderr, "sh: %s: %s\n", r->path,
				strerror(errno));
			return -1;
		}

		int target = (r->kind == SH_REDIR_IN) ? STDIN_FILENO :
							STDOUT_FILENO;

		if (dup2(fd, target) < 0) {
			fprintf(stderr, "sh: dup2: %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);
	}
	return 0;
}

static void run_child(const struct sh_simple_cmd *c)
{
	if (apply_redirs_child(c) < 0)
		_exit(127);
	execvp(c->argv[0], c->argv);
	fprintf(stderr, "sh: %s: %s\n", c->argv[0], strerror(errno));
	_exit(127);
}

sh_err_t sh_pipeline_exec(const struct sh_pipeline *p, int *exit_status)
{
	size_t n = p->nr_cmds;
	int (*pipes)[2] = NULL;
	pid_t *kids;
	size_t i;

	if (exit_status)
		*exit_status = 0;

	if (n == 0)
		return SH_OK;

	if (n > 1) {
		pipes = calloc(n - 1, sizeof(*pipes));
		if (!pipes)
			return SH_ERR_NOMEM;
		for (i = 0; i < n - 1; i++) {
			if (pipe(pipes[i]) < 0) {
				perror("sh: pipe");
				free(pipes);
				return SH_ERR_SYS;
			}
		}
	}

	kids = calloc(n, sizeof(*kids));
	if (!kids) {
		if (pipes) {
			for (i = 0; i < n - 1; i++) {
				close(pipes[i][0]);
				close(pipes[i][1]);
			}
			free(pipes);
		}
		return SH_ERR_NOMEM;
	}

	for (i = 0; i < n; i++) {
		pid_t pid = fork();

		if (pid < 0) {
			size_t j;

			perror("sh: fork");
			for (j = 0; j < i; j++)
				waitpid(kids[j], NULL, 0);
			if (pipes) {
				size_t k;

				for (k = 0; k < n - 1; k++) {
					close(pipes[k][0]);
					close(pipes[k][1]);
				}
				free(pipes);
			}
			free(kids);
			return SH_ERR_SYS;
		}
		if (pid == 0) {
			if (pipes) {
				size_t j;

				if (i > 0) {
					if (dup2(pipes[i - 1][0],
						 STDIN_FILENO) < 0) {
						perror("sh: dup2");
						_exit(127);
					}
				}
				if (i < n - 1) {
					if (dup2(pipes[i][1], STDOUT_FILENO) <
					    0) {
						perror("sh: dup2");
						_exit(127);
					}
				}
				for (j = 0; j < n - 1; j++) {
					close(pipes[j][0]);
					close(pipes[j][1]);
				}
			}
			run_child(&p->cmds[i]);
		}
		kids[i] = pid;
	}

	if (pipes) {
		for (i = 0; i < n - 1; i++) {
			close(pipes[i][0]);
			close(pipes[i][1]);
		}
		free(pipes);
	}

	int last_status = 0;
	sh_err_t err = SH_OK;

	for (i = 0; i < n; i++) {
		int st = 0;

		if (waitpid(kids[i], &st, 0) < 0) {
			perror("sh: waitpid");
			err = SH_ERR_SYS;
			for (i++; i < n; i++)
				waitpid(kids[i], NULL, 0);
			break;
		}
		if (i == n - 1)
			last_status = st;
	}
	free(kids);

	if (err != SH_OK) {
		if (exit_status)
			*exit_status = 1;
		return err;
	}

	if (exit_status) {
		if (WIFEXITED(last_status))
			*exit_status = WEXITSTATUS(last_status);
		else if (WIFSIGNALED(last_status))
			*exit_status = 128 + WTERMSIG(last_status);
		else
			*exit_status = 1;
	}
	return SH_OK;
}
