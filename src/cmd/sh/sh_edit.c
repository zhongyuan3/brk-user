#include "sh.h"

/* ---- interactive line editing, completion and command history ---- */

/* Bounded, per-session history: nothing is written to disk. */
#define SH_HISTORY_MAX 128

enum sh_tty_status {
	SH_TTY_EOF = -1,
	SH_TTY_ERR = -2,
	SH_TTY_TIMEOUT = -3,
};

struct sh_history {
	char **items;
	size_t len;
	size_t cap;
};

struct sh_line {
	char *data;
	size_t len;
	size_t cap;
	size_t pos;
};

struct sh_edit {
	const char *prompt;
	size_t prompt_len;
	struct sh_line line;
	size_t hist_index;
	char *draft;
	int tab_armed;
};

static struct sh_history sh_history;
static struct termios sh_tty_saved;
static volatile int sh_tty_is_raw;

static int sh_write_all(int fd, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, buf, len);

		if (n < 0) {
			if (errno == EINTR || errno == EBUSY)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

void sh_history_add(const char *line)
{
	const char *p;
	char *copy;

	for (p = line; *p; p++)
		if (!isspace((unsigned char)*p))
			break;
	if (!*p)
		return;

	copy = strdup(line);
	if (!copy)
		return;

	if (sh_history.len == SH_HISTORY_MAX) {
		free(sh_history.items[0]);
		memmove(sh_history.items, sh_history.items + 1,
			(sh_history.len - 1) * sizeof(*sh_history.items));
		sh_history.len--;
	}

	if (sh_history.len == sh_history.cap) {
		size_t cap = sh_history.cap ? sh_history.cap * 2 : 16;
		char **items;

		if (cap > SH_HISTORY_MAX)
			cap = SH_HISTORY_MAX;
		items = realloc(sh_history.items, cap * sizeof(*items));
		if (!items) {
			free(copy);
			return;
		}
		sh_history.items = items;
		sh_history.cap = cap;
	}

	sh_history.items[sh_history.len++] = copy;
}

static int sh_line_reserve(struct sh_line *line, size_t need)
{
	size_t cap;
	char *data;

	if (need <= line->cap)
		return 0;

	cap = line->cap ? line->cap : 64;
	while (cap < need) {
		if (cap > (size_t)-1 / 2) {
			cap = need;
			break;
		}
		cap *= 2;
	}

	data = realloc(line->data, cap);
	if (!data)
		return -1;
	line->data = data;
	line->cap = cap;
	return 0;
}

static int sh_line_set(struct sh_line *line, const char *s)
{
	size_t len = strlen(s);

	if (sh_line_reserve(line, len + 1))
		return -1;
	memcpy(line->data, s, len + 1);
	line->len = len;
	line->pos = len;
	return 0;
}

static int sh_line_insert(struct sh_line *line, char c)
{
	if (sh_line_reserve(line, line->len + 2))
		return -1;

	memmove(line->data + line->pos + 1, line->data + line->pos,
		line->len - line->pos);
	line->data[line->pos++] = c;
	line->len++;
	line->data[line->len] = '\0';
	return 0;
}

static void sh_line_delete_before(struct sh_line *line)
{
	if (line->pos == 0)
		return;
	memmove(line->data + line->pos - 1, line->data + line->pos,
		line->len - line->pos + 1);
	line->pos--;
	line->len--;
}

static void sh_line_delete_at(struct sh_line *line)
{
	if (line->pos >= line->len)
		return;
	memmove(line->data + line->pos, line->data + line->pos + 1,
		line->len - line->pos);
	line->len--;
}

static void sh_line_kill_to_end(struct sh_line *line)
{
	line->len = line->pos;
	line->data[line->len] = '\0';
}

static void sh_line_kill_to_start(struct sh_line *line)
{
	if (line->pos == 0)
		return;
	memmove(line->data, line->data + line->pos, line->len - line->pos + 1);
	line->len -= line->pos;
	line->pos = 0;
}

static void sh_line_erase_word(struct sh_line *line)
{
	size_t start = line->pos;

	while (start > 0 && isspace((unsigned char)line->data[start - 1]))
		start--;
	while (start > 0 && !isspace((unsigned char)line->data[start - 1]))
		start--;
	memmove(line->data + start, line->data + line->pos,
		line->len - line->pos + 1);
	line->len -= line->pos - start;
	line->pos = start;
}

static int sh_edit_history_move(struct sh_edit *ed, int direction)
{
	char *draft;

	if (sh_history.len == 0)
		return 0;

	if (direction < 0) {
		if (ed->hist_index == 0)
			return 0;
		if (ed->hist_index == sh_history.len) {
			draft = strdup(ed->line.data ? ed->line.data : "");
			if (!draft)
				return -1;
			free(ed->draft);
			ed->draft = draft;
		}
		ed->hist_index--;
		return sh_line_set(&ed->line, sh_history.items[ed->hist_index]);
	}

	if (ed->hist_index >= sh_history.len)
		return 0;
	ed->hist_index++;
	if (ed->hist_index == sh_history.len)
		return sh_line_set(&ed->line, ed->draft ? ed->draft : "");
	return sh_line_set(&ed->line, sh_history.items[ed->hist_index]);
}

#define SH_COMPLETE_PATH_MAX 1024

struct sh_match {
	char *name;
	int is_dir;
};

struct sh_match_list {
	struct sh_match *items;
	size_t len;
	size_t cap;
};

struct sh_complete_ctx {
	size_t start;
	int cmd_pos;
	int valid;
};

static void sh_match_list_free(struct sh_match_list *list)
{
	size_t i;

	for (i = 0; i < list->len; i++)
		free(list->items[i].name);
	free(list->items);
	list->items = NULL;
	list->len = 0;
	list->cap = 0;
}

static int sh_match_add(struct sh_match_list *list, const char *name,
			int is_dir)
{
	size_t i;
	char *copy;

	for (i = 0; i < list->len; i++)
		if (!strcmp(list->items[i].name, name))
			return 0;

	if (list->len == list->cap) {
		size_t cap = list->cap ? list->cap * 2 : 16;
		struct sh_match *items =
			realloc(list->items, cap * sizeof(*items));

		if (!items)
			return -1;
		list->items = items;
		list->cap = cap;
	}

	copy = strdup(name);
	if (!copy)
		return -1;
	list->items[list->len].name = copy;
	list->items[list->len].is_dir = is_dir;
	list->len++;
	return 0;
}

static int sh_complete_cmp(const void *a, const void *b)
{
	const struct sh_match *ma = a;
	const struct sh_match *mb = b;

	return strcmp(ma->name, mb->name);
}

static int sh_join_path(char *out, size_t cap, const char *dir,
			const char *name)
{
	int n;

	if (!dir[0])
		n = snprintf(out, cap, "%s", name);
	else if (dir[strlen(dir) - 1] == '/')
		n = snprintf(out, cap, "%s%s", dir, name);
	else
		n = snprintf(out, cap, "%s/%s", dir, name);
	return n < 0 || (size_t)n >= cap ? -1 : 0;
}

static int sh_complete_dir_name(const char *dirpart, char *out, size_t cap)
{
	size_t len = strlen(dirpart);

	if (len == 0) {
		if (cap < 2)
			return -1;
		out[0] = '.';
		out[1] = '\0';
		return 0;
	}

	while (len > 1 && dirpart[len - 1] == '/')
		len--;
	if (len >= cap)
		return -1;
	memcpy(out, dirpart, len);
	out[len] = '\0';
	return 0;
}

static int sh_complete_scan_dir(struct sh_match_list *list, const char *dir,
				const char *name_prefix, const char *base,
				int command_mode)
{
	char dbuf[4096];
	size_t base_len = strlen(base);
	int fd;

	fd = open(dir, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;

	for (;;) {
		ssize_t n = posix_getdents(fd, dbuf, sizeof(dbuf), 0);
		size_t pos;

		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;

		for (pos = 0; pos < (size_t)n;) {
			struct posix_dent *d =
				(struct posix_dent *)(dbuf + pos);
			const char *name;
			char full[SH_COMPLETE_PATH_MAX];
			char match[SH_COMPLETE_PATH_MAX];
			struct stat st;

			if (d->d_reclen == 0)
				break;
			pos += d->d_reclen;

			name = d->d_name;
			if (!strcmp(name, ".") || !strcmp(name, ".."))
				continue;
			if (base[0] != '.' && name[0] == '.')
				continue;
			if (strncmp(name, base, base_len))
				continue;
			if (sh_join_path(full, sizeof(full), dir, name))
				continue;
			if (stat(full, &st))
				continue;

			if (command_mode) {
				if (!S_ISREG(st.st_mode) ||
				    access(full, X_OK) != 0)
					continue;
				if (sh_match_add(list, name, 0))
					goto fail;
			} else {
				if (sh_join_path(match, sizeof(match),
						 name_prefix, name))
					continue;
				if (sh_match_add(list, match,
						 S_ISDIR(st.st_mode)))
					goto fail;
			}
		}
	}

	close(fd);
	return 0;
fail:
	close(fd);
	return -1;
}

static int sh_complete_context(const struct sh_line *line,
			       struct sh_complete_ctx *ctx)
{
	size_t pos = line->pos;
	size_t start = pos;
	size_t i = 0;
	int at_cmd = 1;
	int in_word = 0;
	int word_cmd = 0;
	int quoted = 0;
	char quote = 0;

	while (i < pos) {
		char c = line->data[i];

		if (quote) {
			if (quote == '"' && c == '\\' && i + 1 < pos) {
				i += 2;
				continue;
			}
			if (c == quote)
				quote = 0;
			i++;
			continue;
		}

		if (c == '\\') {
			if (i + 1 < pos)
				i += 2;
			else
				i++;
			continue;
		}

		if (c == '\'' || c == '"') {
			if (!in_word) {
				in_word = 1;
				start = i;
				word_cmd = at_cmd;
			}
			quoted = 1;
			quote = c;
			i++;
			continue;
		}

		if (isspace((unsigned char)c)) {
			if (in_word) {
				in_word = 0;
				quoted = 0;
				at_cmd = 0;
			}
			i++;
			continue;
		}

		if (c == '|') {
			in_word = 0;
			quoted = 0;
			at_cmd = 1;
			i++;
			continue;
		}

		if (c == '<' || c == '>') {
			in_word = 0;
			quoted = 0;
			at_cmd = 0;
			i++;
			continue;
		}

		if (c == '#')
			return 0;

		if (!in_word) {
			in_word = 1;
			start = i;
			word_cmd = at_cmd;
		}
		i++;
	}

	ctx->start = in_word ? start : pos;
	ctx->cmd_pos = in_word ? word_cmd : at_cmd;
	ctx->valid = !in_word || (!quoted && quote == 0);
	return ctx->valid;
}

static int sh_complete_word_text(const struct sh_line *line, size_t start,
				 char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;
	size_t i;

	for (i = start; i < line->pos; i++) {
		char c = line->data[i];

		if (c == '\\' && i + 1 < line->pos)
			c = line->data[++i];
		buf = sh_buf_push(buf, &len, &cap, c);
		if (!buf)
			return -1;
	}

	buf = sh_buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
}

static int sh_complete_escape(const char *text, char suffix, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	for (; *text; text++) {
		char c = *text;

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
		    c == '|' || c == '<' || c == '>' || c == '#' || c == '\\' ||
		    c == '\'' || c == '"') {
			buf = sh_buf_push(buf, &len, &cap, '\\');
			if (!buf)
				return -1;
		}
		buf = sh_buf_push(buf, &len, &cap, c);
		if (!buf)
			return -1;
	}

	if (suffix) {
		buf = sh_buf_push(buf, &len, &cap, suffix);
		if (!buf)
			return -1;
	}

	buf = sh_buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
}

static size_t sh_complete_lcp(const struct sh_match_list *list)
{
	const char *first = list->items[0].name;
	size_t lcp = strlen(first);
	size_t i;

	for (i = 1; i < list->len; i++) {
		const char *name = list->items[i].name;
		size_t j = 0;

		while (j < lcp && name[j] == first[j])
			j++;
		lcp = j;
		if (lcp == 0)
			break;
	}
	return lcp;
}

static const char *sh_complete_display_name(const struct sh_match *match)
{
	const char *slash = strrchr(match->name, '/');

	return slash ? slash + 1 : match->name;
}

static void sh_complete_print(const struct sh_match_list *list)
{
	struct winsize ws;
	size_t cols = 80;
	size_t maxlen = 0;
	size_t width;
	size_t ncols;
	size_t rows;
	size_t i, r, c;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col)
		cols = ws.ws_col;

	for (i = 0; i < list->len; i++) {
		size_t len = strlen(sh_complete_display_name(&list->items[i]));

		if (list->items[i].is_dir)
			len++;
		if (len > maxlen)
			maxlen = len;
	}

	width = maxlen + 2;
	ncols = cols / width;
	if (ncols < 1)
		ncols = 1;
	if (ncols > list->len)
		ncols = list->len;
	rows = (list->len + ncols - 1) / ncols;

	(void)sh_write_all(STDOUT_FILENO, "\r\n", 2);
	for (r = 0; r < rows; r++) {
		for (c = 0; c < ncols; c++) {
			size_t idx = c * rows + r;
			const struct sh_match *match;
			const char *name;
			size_t len;

			if (idx >= list->len)
				continue;

			match = &list->items[idx];
			name = sh_complete_display_name(match);
			len = strlen(name);
			(void)sh_write_all(STDOUT_FILENO, name, len);
			if (match->is_dir) {
				(void)sh_write_all(STDOUT_FILENO, "/", 1);
				len++;
			}
			if (c + 1 < ncols && idx + rows < list->len) {
				size_t pad = width - len;

				while (pad-- > 0)
					(void)sh_write_all(STDOUT_FILENO, " ",
							   1);
			}
		}
		(void)sh_write_all(STDOUT_FILENO, "\r\n", 2);
	}
}

static int sh_complete_collect(const struct sh_line *line,
			       const struct sh_complete_ctx *ctx,
			       struct sh_match_list *list)
{
	static const char *const path_dirs[] = {
		"/bin",
		"/sbin",
		"/usr/bin",
		"/usr/sbin",
	};
	static const char *const builtins[] = { "cd", "pwd", "exit" };
	char *word = NULL;
	const char *slash;
	size_t i;
	int rc = 0;

	if (sh_complete_word_text(line, ctx->start, &word))
		return -1;

	slash = strrchr(word, '/');
	if (ctx->cmd_pos && !slash) {
		size_t word_len = strlen(word);

		for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
			if (!strncmp(builtins[i], word, word_len) &&
			    sh_match_add(list, builtins[i], 0)) {
				rc = -1;
				goto out;
			}
		}
		for (i = 0; i < sizeof(path_dirs) / sizeof(path_dirs[0]); i++) {
			if (sh_complete_scan_dir(list, path_dirs[i], "", word,
						 1)) {
				rc = -1;
				goto out;
			}
		}
	} else {
		size_t dirlen = slash ? (size_t)(slash - word) + 1 : 0;
		const char *base = word + dirlen;
		char *dirpart;
		char dir[SH_COMPLETE_PATH_MAX];

		dirpart = malloc(dirlen + 1);
		if (!dirpart) {
			rc = -1;
			goto out;
		}
		memcpy(dirpart, word, dirlen);
		dirpart[dirlen] = '\0';

		if (sh_complete_dir_name(dirpart, dir, sizeof(dir)) == 0 &&
		    sh_complete_scan_dir(list, dir, dirpart, base, 0))
			rc = -1;
		free(dirpart);
	}

out:
	free(word);
	if (rc == 0 && list->len > 1)
		qsort(list->items, list->len, sizeof(list->items[0]),
		      sh_complete_cmp);
	return rc;
}

static int sh_line_replace(struct sh_line *line, size_t start, size_t end,
			   const char *text)
{
	size_t text_len = strlen(text);
	size_t new_len;

	if (start > end || end > line->len)
		return -1;

	new_len = line->len - (end - start) + text_len;
	if (sh_line_reserve(line, new_len + 1))
		return -1;

	memmove(line->data + start + text_len, line->data + end,
		line->len - end + 1);
	memcpy(line->data + start, text, text_len);
	line->len = new_len;
	line->pos = start + text_len;
	return 0;
}

static int sh_edit_complete(struct sh_edit *ed)
{
	struct sh_complete_ctx ctx;
	struct sh_match_list list = { 0 };
	char *word = NULL;
	char *replacement = NULL;
	size_t word_len;
	int rc = 0;

	if (!sh_complete_context(&ed->line, &ctx))
		return 0;
	if (sh_complete_word_text(&ed->line, ctx.start, &word))
		return -1;
	word_len = strlen(word);

	if (sh_complete_collect(&ed->line, &ctx, &list)) {
		rc = -1;
		goto out;
	}

	if (list.len == 0) {
		ed->tab_armed = 0;
		(void)sh_write_all(STDOUT_FILENO, "\a", 1);
		goto out;
	}

	if (list.len == 1) {
		char suffix = list.items[0].is_dir ? '/' : ' ';

		if (sh_complete_escape(list.items[0].name, suffix,
				       &replacement)) {
			rc = -1;
			goto out;
		}
		if (sh_line_replace(&ed->line, ctx.start, ed->line.pos,
				    replacement)) {
			rc = -1;
			goto out;
		}
		ed->tab_armed = 0;
		goto out;
	}

	size_t lcp = sh_complete_lcp(&list);

	if (lcp > word_len) {
		char *prefix = strndup(list.items[0].name, lcp);

		if (!prefix) {
			rc = -1;
			goto out;
		}
		if (sh_complete_escape(prefix, '\0', &replacement)) {
			free(prefix);
			rc = -1;
			goto out;
		}
		free(prefix);
		if (sh_line_replace(&ed->line, ctx.start, ed->line.pos,
				    replacement)) {
			rc = -1;
			goto out;
		}
		ed->tab_armed = 0;
	} else if (ed->tab_armed) {
		sh_complete_print(&list);
		ed->tab_armed = 0;
	} else {
		ed->tab_armed = 1;
	}

out:
	free(word);
	free(replacement);
	sh_match_list_free(&list);
	return rc;
}

int sh_tty_raw(void)
{
	struct termios raw;

	if (ioctl(STDIN_FILENO, TCGETS, &sh_tty_saved) != 0)
		return -1;

	raw = sh_tty_saved;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	raw.c_iflag &= ~(ICRNL | INLCR | IGNCR | IXON | IXOFF);
	raw.c_oflag &= ~OPOST;
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	if (ioctl(STDIN_FILENO, TCSETS, &raw) != 0)
		return -1;

	sh_tty_is_raw = 1;
	return 0;
}

void sh_tty_restore(void)
{
	if (!sh_tty_is_raw)
		return;
	(void)ioctl(STDIN_FILENO, TCSETS, &sh_tty_saved);
	sh_tty_is_raw = 0;
}

void sh_tty_fatal(int sig)
{
	sh_tty_restore();
	_exit(128 + sig);
}

static int sh_tty_getc(void)
{
	for (;;) {
		unsigned char c;
		ssize_t n = read(STDIN_FILENO, &c, 1);

		if (n == 1)
			return c;
		if (n == 0)
			return SH_TTY_EOF;
		if (errno == EINTR)
			continue;
		return SH_TTY_ERR;
	}
}

static int sh_tty_getc_timeout(int timeout_ms)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;
	pfd.revents = 0;
	do {
		ret = poll(&pfd, 1, timeout_ms);
	} while (ret < 0 && errno == EINTR);

	if (ret <= 0)
		return SH_TTY_TIMEOUT;
	return sh_tty_getc();
}

static int sh_edit_escape_seq(struct sh_edit *ed)
{
	char params[16];
	size_t nparams = 0;
	int final = -1;
	int c;

	for (;;) {
		c = sh_tty_getc_timeout(50);
		if (c < 0)
			return 0;
		if (c >= 0x40 && c <= 0x7e) {
			final = c;
			break;
		}
		if (nparams + 1 < sizeof(params))
			params[nparams++] = (char)c;
	}
	params[nparams] = '\0';

	switch (final) {
	case 'A':
		return sh_edit_history_move(ed, -1);
	case 'B':
		return sh_edit_history_move(ed, 1);
	case 'C':
		if (ed->line.pos < ed->line.len)
			ed->line.pos++;
		return 0;
	case 'D':
		if (ed->line.pos > 0)
			ed->line.pos--;
		return 0;
	case 'H':
		ed->line.pos = 0;
		return 0;
	case 'F':
		ed->line.pos = ed->line.len;
		return 0;
	case '~': {
		int code = atoi(params);

		switch (code) {
		case 1:
		case 7:
			ed->line.pos = 0;
			break;
		case 4:
		case 8:
			ed->line.pos = ed->line.len;
			break;
		case 3:
			sh_line_delete_at(&ed->line);
			break;
		default:
			break;
		}
		return 0;
	}
	default:
		return 0;
	}
}

static int sh_edit_escape(struct sh_edit *ed)
{
	int c = sh_tty_getc_timeout(50);

	if (c == '[' || c == 'O')
		return sh_edit_escape_seq(ed);
	return 0;
}

static int sh_edit_draw(const struct sh_edit *ed)
{
	char seq[32];
	int n;

	if (sh_write_all(STDOUT_FILENO, "\r\033[K", 4))
		return -1;
	if (sh_write_all(STDOUT_FILENO, ed->prompt, ed->prompt_len))
		return -1;
	if (ed->line.len &&
	    sh_write_all(STDOUT_FILENO, ed->line.data, ed->line.len))
		return -1;

	if (ed->line.pos < ed->line.len) {
		n = snprintf(seq, sizeof(seq), "\033[%luD",
			     (unsigned long)(ed->line.len - ed->line.pos));
		if (n < 0)
			return -1;
		if ((size_t)n >= sizeof(seq))
			n = (int)sizeof(seq) - 1;
		if (sh_write_all(STDOUT_FILENO, seq, (size_t)n))
			return -1;
	}
	return 0;
}

static int sh_edit_init(struct sh_edit *ed, const char *prompt,
			size_t prompt_len)
{
	memset(ed, 0, sizeof(*ed));
	ed->prompt = prompt;
	ed->prompt_len = prompt_len;
	ed->hist_index = sh_history.len;

	if (sh_line_reserve(&ed->line, 1))
		return -1;
	ed->line.data[0] = '\0';
	return 0;
}

static void sh_edit_free(struct sh_edit *ed)
{
	free(ed->line.data);
	free(ed->draft);
	ed->line.data = NULL;
	ed->draft = NULL;
}

int sh_edit_line(const char *prompt, size_t prompt_len, char **out)
{
	struct sh_edit ed;
	int status = SH_EDIT_OK;

	*out = NULL;
	if (sh_edit_init(&ed, prompt, prompt_len))
		return SH_EDIT_ERR;
	if (sh_edit_draw(&ed)) {
		sh_edit_free(&ed);
		return SH_EDIT_ERR;
	}

	for (;;) {
		int c = sh_tty_getc();

		if (c == SH_TTY_EOF) {
			if (sh_write_all(STDOUT_FILENO, "\r\n", 2))
				status = SH_EDIT_ERR;
			else
				status = SH_EDIT_EOF;
			break;
		}
		if (c == SH_TTY_ERR) {
			status = SH_EDIT_ERR;
			break;
		}

		if (c != '\t')
			ed.tab_armed = 0;

		if (c == '\n' || c == '\r') {
			if (sh_write_all(STDOUT_FILENO, "\r\n", 2)) {
				status = SH_EDIT_ERR;
			} else {
				*out = ed.line.data;
				ed.line.data = NULL;
			}
			break;
		}
		if (c == 0x03) {
			(void)sh_write_all(STDOUT_FILENO, "^C\r\n", 4);
			status = SH_EDIT_CANCEL;
			break;
		}

		if (c == 0x04) {
			if (ed.line.len == 0) {
				(void)sh_write_all(STDOUT_FILENO, "\r\n", 2);
				status = SH_EDIT_EOF;
				break;
			}
			sh_line_delete_at(&ed.line);
		} else if (c == 0x7f || c == 0x08) {
			sh_line_delete_before(&ed.line);
		} else if (c == 0x01) {
			ed.line.pos = 0;
		} else if (c == 0x05) {
			ed.line.pos = ed.line.len;
		} else if (c == 0x02) {
			if (ed.line.pos > 0)
				ed.line.pos--;
		} else if (c == 0x06) {
			if (ed.line.pos < ed.line.len)
				ed.line.pos++;
		} else if (c == '\t') {
			if (sh_edit_complete(&ed)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else if (c == 0x0b) {
			sh_line_kill_to_end(&ed.line);
		} else if (c == 0x15) {
			sh_line_kill_to_start(&ed.line);
		} else if (c == 0x17) {
			sh_line_erase_word(&ed.line);
		} else if (c == 0x10) {
			if (sh_edit_history_move(&ed, -1)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else if (c == 0x0e) {
			if (sh_edit_history_move(&ed, 1)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else if (c == 0x1b) {
			if (sh_edit_escape(&ed)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else if (c >= 0x20 && c != 0x7f) {
			if (sh_line_insert(&ed.line, (char)c)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else {
			continue;
		}

		if (sh_edit_draw(&ed)) {
			status = SH_EDIT_ERR;
			break;
		}
	}

	sh_edit_free(&ed);
	return status;
}
