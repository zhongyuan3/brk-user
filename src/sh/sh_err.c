#include "sh_err.h"

const char *sh_err_default_msg(sh_err_t err)
{
	switch (err) {
	case SH_OK:
		return "success";
	case SH_ERR_NOMEM:
		return "out of memory";
	case SH_ERR_LEX:
		return "lexical error";
	case SH_ERR_PARSE:
		return "parse error";
	case SH_ERR_IO:
		return "input/output error";
	case SH_ERR_SYS:
		return "system call failed";
	}
	return "unknown error";
}

void sh_err_fprint(FILE *fp, sh_err_t err, const char *detail)
{
	if (detail && detail[0])
		fprintf(fp, "sh: %s\n", detail);
	else
		fprintf(fp, "sh: %s\n", sh_err_default_msg(err));
}

int sh_err_to_exit_status(sh_err_t err)
{
	switch (err) {
	case SH_OK:
		return 0;
	case SH_ERR_LEX:
	case SH_ERR_PARSE:
		return 2;
	case SH_ERR_NOMEM:
	case SH_ERR_IO:
	case SH_ERR_SYS:
	default:
		return 1;
	}
}
