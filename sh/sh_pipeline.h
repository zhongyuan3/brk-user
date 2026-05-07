#ifndef SH_PIPELINE_H
#define SH_PIPELINE_H

#include "sh_err.h"
#include <stddef.h>

enum sh_redir_kind {
	SH_REDIR_IN,
	SH_REDIR_OUT,
	SH_REDIR_APPEND,
};

struct sh_redir {
	enum sh_redir_kind kind;
	char *path;
	struct sh_redir *next;
};

struct sh_simple_cmd {
	char **argv; /* NULL-terminated */
	size_t argc;
	struct sh_redir *redir_list;
};

struct sh_pipeline {
	struct sh_simple_cmd *cmds;
	size_t nr_cmds;
};

void sh_pipeline_init(struct sh_pipeline *p);
void sh_pipeline_release(struct sh_pipeline *p);
sh_err_t sh_pipeline_exec(const struct sh_pipeline *p, int *exit_status);

#endif
