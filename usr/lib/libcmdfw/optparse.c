#include <libcmdfw.h>

#include <stdlib.h>
#include <string.h>

static const struct cmdfw_option *
find_short(const struct cmdfw_option *opts, char c)
{
	for (const struct cmdfw_option *o = opts; o->id; o++) {
		if (o->short_opt == c)
			return o;
	}
	return NULL;
}

static const struct cmdfw_option *find_long(const struct cmdfw_option *opts,
					      const char *name)
{
	for (const struct cmdfw_option *o = opts; o->id; o++) {
		if (o->long_opt && !strcmp(o->long_opt, name))
			return o;
	}
	return NULL;
}

static int bad_option(const char *opt)
{
	cmdfw_error("unrecognized option '%s'", opt);
	return '?';
}

static int missing_arg(const struct cmdfw_option *opt)
{
	if (opt->long_opt)
		cmdfw_error("option '--%s' requires an argument",
			      opt->long_opt);
	else
		cmdfw_error("option requires an argument -- '%c'",
			      opt->short_opt);
	return ':';
}

static int finish_option(struct cmdfw_optctx *ctx,
			 const struct cmdfw_option *opt,
			 const char *arg_embed)
{
	if (opt->arg == CMDFW_OPT_REQUIRED_ARG) {
		if (arg_embed) {
			ctx->arg = arg_embed;
			ctx->index++;
		} else {
			if (ctx->index + 1 >= ctx->argc)
				return missing_arg(opt);
			ctx->arg = ctx->argv[ctx->index + 1];
			ctx->index += 2;
		}
	} else {
		ctx->arg = NULL;
		ctx->index++;
	}

	return opt->id;
}

static int parse_long(struct cmdfw_optctx *ctx,
		      const struct cmdfw_option *opts, const char *arg)
{
	const char *eq = strchr(arg, '=');
	const struct cmdfw_option *opt;
	char name[128];
	size_t n;

	if (eq) {
		n = (size_t)(eq - arg);
		if (n >= sizeof(name))
			return bad_option(arg);
		memcpy(name, arg, n);
		name[n] = '\0';
		opt = find_long(opts, name);
		if (!opt)
			return bad_option(arg);
		if (opt->arg != CMDFW_OPT_REQUIRED_ARG)
			return bad_option(arg);
		return finish_option(ctx, opt, eq + 1);
	}

	opt = find_long(opts, arg);
	if (!opt)
		return bad_option(arg);
	return finish_option(ctx, opt, NULL);
}

static int parse_short(struct cmdfw_optctx *ctx,
		       const struct cmdfw_option *opts)
{
	const char *cluster = ctx->argv[ctx->index] + ctx->short_pos;
	char c = cluster[0];
	const struct cmdfw_option *opt;

	if (c == '\0') {
		ctx->short_pos = 0;
		ctx->index++;
		return cmdfw_optparse(ctx, opts);
	}

	opt = find_short(opts, c);
	if (!opt)
		return bad_option(ctx->argv[ctx->index]);

	ctx->short_pos++;
	if (opt->arg == CMDFW_OPT_REQUIRED_ARG) {
		const char *arg = cluster[1] ? cluster + 1 : NULL;

		ctx->short_pos = 0;
		return finish_option(ctx, opt, arg);
	}

	if (cluster[1] == '\0') {
		ctx->index++;
		ctx->short_pos = 0;
	}

	ctx->arg = NULL;
	return opt->id;
}

void cmdfw_optctx_init(struct cmdfw_optctx *ctx, int argc, char *argv[])
{
	ctx->argc = argc;
	ctx->argv = argv;
	ctx->index = 1;
	ctx->short_pos = 0;
	ctx->arg = NULL;
	ctx->end_of_opts = false;
}

int cmdfw_optparse(struct cmdfw_optctx *ctx,
		     const struct cmdfw_option *opts)
{
	const char *arg;

	if (ctx->short_pos)
		return parse_short(ctx, opts);

	while (ctx->index < ctx->argc) {
		arg = ctx->argv[ctx->index];

		if (ctx->end_of_opts || arg[0] != '-' || arg[1] == '\0')
			return 0;

		if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0') {
			ctx->end_of_opts = true;
			ctx->index++;
			continue;
		}

		if (arg[0] == '-' && arg[1] == '-')
			return parse_long(ctx, opts, arg + 2);

		ctx->short_pos = 1;
		return parse_short(ctx, opts);
	}

	return 0;
}

int cmdfw_operand_count(const struct cmdfw_optctx *ctx)
{
	if (ctx->index >= ctx->argc)
		return 0;
	return ctx->argc - ctx->index;
}

const char *cmdfw_operand(const struct cmdfw_optctx *ctx, int n)
{
	if (n < 0 || ctx->index + n >= ctx->argc)
		return NULL;
	return ctx->argv[ctx->index + n];
}

void cmdfw_require_operands(const struct cmdfw_optctx *ctx, int min,
			      const char *usage)
{
	if (cmdfw_operand_count(ctx) >= min)
		return;
	cmdfw_usage(usage);
}

void cmdfw_require_operand_count(const struct cmdfw_optctx *ctx, int exact,
				   const char *usage)
{
	if (cmdfw_operand_count(ctx) == exact)
		return;
	cmdfw_usage(usage);
}

void cmdfw_require_operand_range(const struct cmdfw_optctx *ctx, int min,
				   int max, const char *usage)
{
	int n = cmdfw_operand_count(ctx);

	if (n >= min && n <= max)
		return;
	cmdfw_usage(usage);
}
