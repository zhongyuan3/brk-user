#include <apputil.h>

#include <stdlib.h>
#include <string.h>

static const struct app_option *find_short(const struct app_option *opts,
					   char c)
{
	for (const struct app_option *o = opts; o->id; o++) {
		if (o->short_opt == c)
			return o;
	}
	return NULL;
}

static const struct app_option *find_long(const struct app_option *opts,
					  const char *name)
{
	for (const struct app_option *o = opts; o->id; o++) {
		if (o->long_opt && !strcmp(o->long_opt, name))
			return o;
	}
	return NULL;
}

static int bad_option(const char *opt)
{
	app_error("unrecognized option '%s'", opt);
	return '?';
}

static int missing_arg(const struct app_option *opt)
{
	if (opt->long_opt)
		app_error("option '--%s' requires an argument", opt->long_opt);
	else
		app_error("option requires an argument -- '%c'",
			  opt->short_opt);
	return ':';
}

static const char *next_arg(struct app_optctx *ctx, const char *embedded)
{
	if (embedded && embedded[0])
		return embedded;

	if (ctx->index + 1 >= ctx->argc)
		return NULL;

	ctx->index++;
	return ctx->argv[ctx->index];
}

static int finish_option(struct app_optctx *ctx, const struct app_option *opt,
			 const char *arg_embed)
{
	if (opt->arg == APP_OPT_REQUIRED_ARG) {
		ctx->arg = next_arg(ctx, arg_embed);
		if (!ctx->arg)
			return missing_arg(opt);
	} else {
		ctx->arg = NULL;
	}

	return opt->id;
}

static int parse_long(struct app_optctx *ctx, const struct app_option *opts,
		      const char *arg)
{
	const char *eq = strchr(arg, '=');
	const struct app_option *opt;
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
		if (opt->arg != APP_OPT_REQUIRED_ARG)
			return bad_option(arg);
		ctx->index++;
		return finish_option(ctx, opt, eq + 1);
	}

	opt = find_long(opts, arg);
	if (!opt)
		return bad_option(arg);
	ctx->index++;
	return finish_option(ctx, opt, NULL);
}

static int parse_short(struct app_optctx *ctx, const struct app_option *opts)
{
	const char *cluster = ctx->argv[ctx->index] + ctx->short_pos;
	char c = cluster[0];
	const struct app_option *opt;

	if (c == '\0') {
		ctx->short_pos = 0;
		ctx->index++;
		return app_optparse(ctx, opts);
	}

	opt = find_short(opts, c);
	if (!opt)
		return bad_option(ctx->argv[ctx->index]);

	ctx->short_pos++;
	if (opt->arg == APP_OPT_REQUIRED_ARG) {
		const char *arg = cluster + 1;

		ctx->index++;
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

void app_optctx_init(struct app_optctx *ctx, int argc, char *argv[])
{
	ctx->argc = argc;
	ctx->argv = argv;
	ctx->index = 1;
	ctx->short_pos = 0;
	ctx->arg = NULL;
	ctx->end_of_opts = false;
}

int app_optparse(struct app_optctx *ctx, const struct app_option *opts)
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

int app_operand_count(const struct app_optctx *ctx)
{
	if (ctx->index >= ctx->argc)
		return 0;
	return ctx->argc - ctx->index;
}

const char *app_operand(const struct app_optctx *ctx, int n)
{
	if (n < 0 || ctx->index + n >= ctx->argc)
		return NULL;
	return ctx->argv[ctx->index + n];
}

void app_require_operands(const struct app_optctx *ctx, int min,
			  const char *usage)
{
	if (app_operand_count(ctx) >= min)
		return;
	app_usage(usage);
}

void app_require_operand_count(const struct app_optctx *ctx, int exact,
			       const char *usage)
{
	if (app_operand_count(ctx) == exact)
		return;
	app_usage(usage);
}

void app_require_operand_range(const struct app_optctx *ctx, int min, int max,
			       const char *usage)
{
	int n = app_operand_count(ctx);

	if (n >= min && n <= max)
		return;
	app_usage(usage);
}
