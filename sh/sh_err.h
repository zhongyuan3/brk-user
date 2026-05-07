#ifndef SH_ERR_H
#define SH_ERR_H

#include <stdio.h>

typedef enum sh_err {
	SH_OK = 0,
	SH_ERR_NOMEM,
	SH_ERR_LEX,
	SH_ERR_PARSE,
	SH_ERR_IO,
	SH_ERR_SYS,
} sh_err_t;

const char *sh_err_default_msg(sh_err_t err);

void sh_err_fprint(FILE *fp, sh_err_t err, const char *detail);

int sh_err_to_exit_status(sh_err_t err);

#endif
