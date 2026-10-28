#include <apputil.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <termios.h>
#include <unistd.h>

#define TAB_WIDTH 8
#define MAX_FILE_SIZE (4 * 1024 * 1024)
#define OUTBUF_SIZE 4096

struct line {
	char *data;
	size_t len;
	size_t cap;
};

struct editor {
	struct line *lines;
	size_t nlines;
	size_t cap;

	char *path;
	char *dir;

	size_t row;
	size_t col;
	size_t top;
	size_t left;

	int rows;
	int cols;
	int dirty;
	int quit;
	int quit_armed;
	int debug_keys;
	char keylog[64];
	char status[192];
};

struct outbuf {
	char data[OUTBUF_SIZE];
	size_t len;
};

struct key_state {
	int esc;
	int ticks;
	unsigned char seq[8];
	size_t n;
};

static struct termios saved_termios;
static volatile int termios_saved;
static volatile int winch_flag;

/* ---- basic allocation helpers ---- */

static char *xstrndup(const char *s, size_t n)
{
	char *p = malloc(n + 1);

	if (!p)
		return NULL;
	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

static int line_reserve(struct line *line, size_t need)
{
	size_t cap;
	char *p;

	if (need <= line->cap)
		return 0;

	cap = line->cap ? line->cap : 32;
	while (cap < need) {
		if (cap > (size_t)-1 / 2) {
			cap = need;
			break;
		}
		cap *= 2;
	}

	p = realloc(line->data, cap);
	if (!p)
		return -1;
	line->data = p;
	line->cap = cap;
	return 0;
}

static int line_assign(struct line *line, const char *s, size_t n)
{
	if (line_reserve(line, n + 1))
		return -1;
	memcpy(line->data, s, n);
	line->data[n] = '\0';
	line->len = n;
	return 0;
}

static int line_insert_bytes(struct line *line, size_t pos, const char *s,
			     size_t n)
{
	if (pos > line->len)
		return -1;
	if (line_reserve(line, line->len + n + 1))
		return -1;

	memmove(line->data + pos + n, line->data + pos, line->len - pos);
	memcpy(line->data + pos, s, n);
	line->len += n;
	line->data[line->len] = '\0';
	return 0;
}

static void line_delete_bytes(struct line *line, size_t pos, size_t n)
{
	if (pos >= line->len)
		return;
	if (n > line->len - pos)
		n = line->len - pos;
	memmove(line->data + pos, line->data + pos + n,
		line->len - pos - n);
	line->len -= n;
	line->data[line->len] = '\0';
}

static int ed_reserve_lines(struct editor *ed, size_t n)
{
	size_t cap;
	struct line *p;

	if (n <= ed->cap)
		return 0;

	cap = ed->cap ? ed->cap : 16;
	while (cap < n) {
		if (cap > (size_t)-1 / 2) {
			cap = n;
			break;
		}
		cap *= 2;
	}

	p = realloc(ed->lines, cap * sizeof(*p));
	if (!p)
		return -1;

	memset(p + ed->cap, 0, (cap - ed->cap) * sizeof(*p));
	ed->lines = p;
	ed->cap = cap;
	return 0;
}

static int ed_insert_line(struct editor *ed, size_t idx)
{
	if (idx > ed->nlines)
		return -1;
	if (ed_reserve_lines(ed, ed->nlines + 1))
		return -1;

	memmove(&ed->lines[idx + 1], &ed->lines[idx],
		(ed->nlines - idx) * sizeof(ed->lines[0]));
	memset(&ed->lines[idx], 0, sizeof(ed->lines[0]));
	ed->nlines++;
	return 0;
}

static void ed_remove_line(struct editor *ed, size_t idx)
{
	if (idx >= ed->nlines)
		return;

	free(ed->lines[idx].data);
	memmove(&ed->lines[idx], &ed->lines[idx + 1],
		(ed->nlines - idx - 1) * sizeof(ed->lines[0]));
	ed->nlines--;
	memset(&ed->lines[ed->nlines], 0, sizeof(ed->lines[0]));
}

static int ed_append_line(struct editor *ed, const char *s, size_t n)
{
	if (ed_insert_line(ed, ed->nlines))
		return -1;
	return line_assign(&ed->lines[ed->nlines - 1], s, n);
}

/* ---- file loading ---- */

static int load_file(struct editor *ed)
{
	struct stat st;
	char *buf = NULL;
	size_t size, done = 0, start = 0;
	int fd;

	fd = open(ed->path, O_RDONLY);
	if (fd < 0) {
		if (errno != ENOENT)
			return -1;
		if (ed_append_line(ed, "", 0))
			return -1;
		return 0;
	}

	if (fstat(fd, &st) || st.st_size < 0 ||
	    (unsigned long)st.st_size > MAX_FILE_SIZE) {
		close(fd);
		errno = EFBIG;
		return -1;
	}

	size = (size_t)st.st_size;
	if (size) {
		buf = malloc(size);
		if (!buf) {
			close(fd);
			return -1;
		}
		while (done < size) {
			ssize_t n = read(fd, buf + done, size - done);

			if (n < 0) {
				if (errno == EINTR)
					continue;
				free(buf);
				close(fd);
				return -1;
			}
			if (n == 0)
				break;
			done += (size_t)n;
		}
	}
	close(fd);

	while (start < done) {
		size_t i = start;
		size_t len;

		while (i < done && buf[i] != '\n')
			i++;
		len = i - start;
		if (len > 0 && buf[start + len - 1] == '\r')
			len--;
		if (ed_append_line(ed, buf + start, len)) {
			free(buf);
			return -1;
		}
		if (i == done)
			break;
		start = i + 1;
	}

	if (ed->nlines == 0 && ed_append_line(ed, "", 0)) {
		free(buf);
		return -1;
	}

	free(buf);
	return 0;
}

/* ---- output helpers ---- */

static int write_all(int fd, const char *buf, size_t len)
{
	size_t off = 0;

	while (off < len) {
		ssize_t n = write(fd, buf + off, len - off);

		if (n < 0) {
			if (errno == EINTR || errno == EBUSY)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		off += (size_t)n;
	}
	return 0;
}

static int out_flush(struct outbuf *out)
{
	if (out->len == 0)
		return 0;
	if (write_all(STDOUT_FILENO, out->data, out->len))
		return -1;
	out->len = 0;
	return 0;
}

static int out_write(struct outbuf *out, const char *s, size_t n)
{
	while (n > 0) {
		size_t room = sizeof(out->data) - out->len;
		size_t chunk = n < room ? n : room;

		memcpy(out->data + out->len, s, chunk);
		out->len += chunk;
		s += chunk;
		n -= chunk;

		if (out->len == sizeof(out->data) && out_flush(out))
			return -1;
	}
	return 0;
}

static int out_str(struct outbuf *out, const char *s)
{
	return out_write(out, s, strlen(s));
}

static int out_char(struct outbuf *out, char c)
{
	return out_write(out, &c, 1);
}

static int out_fmt(struct outbuf *out, const char *fmt, ...)
{
	char tmp[256];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);

	if (n < 0)
		return -1;
	if ((size_t)n >= sizeof(tmp))
		n = (int)sizeof(tmp) - 1;
	return out_write(out, tmp, (size_t)n);
}

/* ---- display geometry ---- */

static size_t line_width(const struct line *line, size_t col)
{
	size_t width = 0;

	if (col > line->len)
		col = line->len;

	for (size_t i = 0; i < col; i++) {
		if (line->data[i] == '\t')
			width += TAB_WIDTH - (width % TAB_WIDTH);
		else
			width++;
	}
	return width;
}

static void clamp_cursor(struct editor *ed)
{
	struct line *line;

	if (ed->nlines == 0)
		return;
	if (ed->row >= ed->nlines)
		ed->row = ed->nlines - 1;
	if (ed->top > ed->row)
		ed->top = ed->row;

	line = &ed->lines[ed->row];
	if (ed->col > line->len)
		ed->col = line->len;
}

static void ensure_visible(struct editor *ed)
{
	int text_rows = ed->rows > 1 ? ed->rows - 1 : 1;
	struct line *line;
	size_t width;

	clamp_cursor(ed);

	if (ed->row >= ed->top + (size_t)text_rows)
		ed->top = ed->row - (size_t)text_rows + 1;
	if (ed->row < ed->top)
		ed->top = ed->row;

	line = &ed->lines[ed->row];
	width = line_width(line, ed->col);

	if (width < ed->left)
		ed->left = width;
	else if (width >= ed->left + (size_t)ed->cols)
		ed->left = width - (size_t)ed->cols + 1;
}

static int render_line(struct outbuf *out, const struct line *line,
		       size_t left, size_t cols)
{
	size_t disp = 0;
	size_t i = 0;

	while (i < line->len && disp < left + cols) {
		unsigned char c = (unsigned char)line->data[i++];
		size_t next;

		if (c == '\t')
			next = disp + (TAB_WIDTH - (disp % TAB_WIDTH));
		else
			next = disp + 1;

		if (next > left) {
			if (c == '\t') {
				size_t d = disp < left ? left : disp;

				for (; d < next && d < left + cols; d++)
					if (out_char(out, ' '))
						return -1;
			} else if (disp >= left) {
				char show = (c >= 0x20 && c != 0x7f) ?
						    (char)c :
						    '.';
				if (out_char(out, show))
					return -1;
			}
		}
		disp = next;
	}
	return 0;
}

static int draw(struct editor *ed)
{
	struct outbuf out = { .len = 0 };
	char status[256];
	int text_rows = ed->rows > 1 ? ed->rows - 1 : 1;
	size_t width;
	size_t crow;
	size_t ccol;

	ensure_visible(ed);

	if (out_str(&out, "\033[?25l\033[H"))
		return -1;

	for (int r = 0; r < text_rows; r++) {
		size_t idx = ed->top + (size_t)r;

		if (idx < ed->nlines &&
		    render_line(&out, &ed->lines[idx], ed->left,
				(size_t)ed->cols))
			return -1;
		if (out_str(&out, "\033[K\r\n"))
			return -1;
	}

	snprintf(status, sizeof(status), " %s%s  %lu:%lu  %s%s%s",
		 ed->path, ed->dirty ? " [modified]" : "",
		 (unsigned long)(ed->row + 1), (unsigned long)(ed->col + 1),
		 ed->debug_keys ? "keys: " : "",
		 ed->debug_keys ? ed->keylog : "",
		 ed->status);

	size_t status_len = strlen(status);

	if (out_str(&out, "\033[7m"))
		return -1;
	/* Avoid writing the very last cell: it can trigger terminal wrap. */
	for (int i = 0; i < ed->cols - 1; i++) {
		char c = i < (int)status_len ? status[i] : ' ';

		if (out_char(&out, c))
			return -1;
	}
	if (out_str(&out, "\033[0m\033[K"))
		return -1;

	width = line_width(&ed->lines[ed->row], ed->col);
	crow = ed->row - ed->top + 1;
	ccol = width >= ed->left ? width - ed->left + 1 : 1;
	if (out_fmt(&out, "\033[%lu;%luH\033[?25h", (unsigned long)crow,
		    (unsigned long)ccol))
		return -1;

	return out_flush(&out);
}

/* ---- editing operations ---- */

static void mark_dirty(struct editor *ed)
{
	ed->dirty = 1;
	ed->quit_armed = 0;
	ed->status[0] = '\0';
}

static void move_up(struct editor *ed)
{
	if (ed->row > 0)
		ed->row--;
	clamp_cursor(ed);
}

static void move_down(struct editor *ed)
{
	if (ed->row + 1 < ed->nlines)
		ed->row++;
	clamp_cursor(ed);
}

static void move_left(struct editor *ed)
{
	if (ed->col > 0) {
		ed->col--;
	} else if (ed->row > 0) {
		ed->row--;
		ed->col = ed->lines[ed->row].len;
	}
}

static void move_right(struct editor *ed)
{
	struct line *line = &ed->lines[ed->row];

	if (ed->col < line->len) {
		ed->col++;
	} else if (ed->row + 1 < ed->nlines) {
		ed->row++;
		ed->col = 0;
	}
}

static void move_home(struct editor *ed)
{
	ed->col = 0;
}

static void move_end(struct editor *ed)
{
	ed->col = ed->lines[ed->row].len;
}

static void move_page(struct editor *ed, int direction)
{
	int text_rows = ed->rows > 1 ? ed->rows - 1 : 1;
	size_t step = (size_t)text_rows;

	if (direction < 0) {
		ed->row = ed->row > step ? ed->row - step : 0;
	} else {
		size_t last = ed->nlines ? ed->nlines - 1 : 0;

		ed->row += step;
		if (ed->row > last)
			ed->row = last;
	}
	clamp_cursor(ed);
}

static void insert_byte(struct editor *ed, unsigned char c)
{
	struct line *line = &ed->lines[ed->row];

	if (line_insert_bytes(line, ed->col, (const char *)&c, 1))
		return;
	ed->col++;
	mark_dirty(ed);
}

static void insert_newline(struct editor *ed)
{
	struct line *line = &ed->lines[ed->row];
	size_t tail = line->len - ed->col;

	if (ed_insert_line(ed, ed->row + 1))
		return;

	line = &ed->lines[ed->row];
	if (line_assign(&ed->lines[ed->row + 1],
			line->data ? line->data + ed->col : "", tail))
		return;

	line->len = ed->col;
	if (line->data)
		line->data[line->len] = '\0';

	ed->row++;
	ed->col = 0;
	mark_dirty(ed);
}

static void backspace(struct editor *ed)
{
	struct line *line = &ed->lines[ed->row];

	if (ed->col > 0) {
		line_delete_bytes(line, ed->col - 1, 1);
		ed->col--;
		mark_dirty(ed);
	} else if (ed->row > 0) {
		struct line *prev = &ed->lines[ed->row - 1];
		size_t old_len = prev->len;

		if (line->len &&
		    line_insert_bytes(prev, prev->len, line->data, line->len))
			return;
		ed_remove_line(ed, ed->row);
		ed->row--;
		ed->col = old_len;
		mark_dirty(ed);
	}
}

static void delete_forward(struct editor *ed)
{
	struct line *line = &ed->lines[ed->row];

	if (ed->col < line->len) {
		line_delete_bytes(line, ed->col, 1);
		mark_dirty(ed);
	} else if (ed->row + 1 < ed->nlines) {
		struct line *next = &ed->lines[ed->row + 1];

		if (next->len &&
		    line_insert_bytes(line, line->len, next->data, next->len))
			return;
		ed_remove_line(ed, ed->row + 1);
		mark_dirty(ed);
	}
}

static void kill_to_eol(struct editor *ed)
{
	struct line *line = &ed->lines[ed->row];

	if (ed->col >= line->len)
		return;
	line->len = ed->col;
	line->data[line->len] = '\0';
	mark_dirty(ed);
}

/* ---- saving ---- */

static char *parent_dir(const char *path)
{
	const char *slash = strrchr(path, '/');

	if (!slash)
		return xstrndup(".", 1);
	if (slash == path)
		return xstrndup("/", 1);
	return xstrndup(path, (size_t)(slash - path));
}

static int save_file(struct editor *ed)
{
	size_t plen = strlen(ed->path);
	char *tmp = malloc(plen + 16);
	int fd;
	int saved_errno;
	int result = -1;

	if (!tmp)
		return -1;
	snprintf(tmp, plen + 16, "%s.edit.tmp", ed->path);

	fd = open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		snprintf(ed->status, sizeof(ed->status), "open %s: %s", tmp,
			 strerror(errno));
		goto out_free;
	}

	for (size_t i = 0; i < ed->nlines; i++) {
		struct line *line = &ed->lines[i];
		bool newline = (i + 1 < ed->nlines) || line->len > 0;

		if (line->len &&
		    write_all(fd, line->data, line->len)) {
			snprintf(ed->status, sizeof(ed->status),
				 "write: %s", strerror(errno));
			goto out_close;
		}
		if (newline && write_all(fd, "\n", 1)) {
			snprintf(ed->status, sizeof(ed->status),
				 "write: %s", strerror(errno));
			goto out_close;
		}
	}

	if (fsync(fd)) {
		snprintf(ed->status, sizeof(ed->status), "fsync: %s",
			 strerror(errno));
		goto out_close;
	}
	if (close(fd)) {
		fd = -1;
		snprintf(ed->status, sizeof(ed->status), "close: %s",
			 strerror(errno));
		goto out_unlink;
	}
	fd = -1;

	if (syscall(SYS_rename, tmp, ed->path) != 0) {
		snprintf(ed->status, sizeof(ed->status), "rename: %s",
			 strerror(errno));
		goto out_unlink;
	}

	/* Persist the directory entry too (rename durability). */
	fd = open(ed->dir, O_RDONLY);
	if (fd >= 0) {
		(void)fsync(fd);
		close(fd);
	}
	fd = -1;

	ed->dirty = 0;
	ed->quit_armed = 0;
	snprintf(ed->status, sizeof(ed->status), "saved");
	result = 0;
	goto out_free;

out_close:
	saved_errno = errno;
	if (fd >= 0)
		close(fd);
	errno = saved_errno;
out_unlink:
	unlink(tmp);
out_free:
	free(tmp);
	return result;
}

/* ---- key handling ---- */

static void request_quit(struct editor *ed)
{
	if (ed->dirty && !ed->quit_armed) {
		snprintf(ed->status, sizeof(ed->status),
			 "unsaved changes - ^Q again to discard");
		ed->quit_armed = 1;
		return;
	}
	ed->quit = 1;
}

static void reset_escape(struct key_state *keys)
{
	keys->esc = 0;
	keys->ticks = 0;
	keys->n = 0;
}

/* Return 0: need more bytes, 1: handled, -1: invalid sequence. */
static int parse_escape(struct editor *ed, struct key_state *keys)
{
	char tmp[sizeof(keys->seq) + 1];
	size_t n = keys->n;
	char final;

	if (n == 0)
		return 0;

	memcpy(tmp, keys->seq, n);
	tmp[n] = '\0';

	if (tmp[0] == 'O') {
		if (n < 2)
			return 0;
		if (n == 2) {
			if (tmp[1] == 'A')
				move_up(ed);
			else if (tmp[1] == 'B')
				move_down(ed);
			else if (tmp[1] == 'C')
				move_right(ed);
			else if (tmp[1] == 'D')
				move_left(ed);
			else
				return -1;
			return 1;
		}
		return -1;
	}

	if (tmp[0] != '[')
		return -1;
	if (n == 1)
		return 0;

	final = tmp[n - 1];
	if (final >= 'A' && final <= 'Z') {
		switch (final) {
		case 'A':
			move_up(ed);
			break;
		case 'B':
			move_down(ed);
			break;
		case 'C':
			move_right(ed);
			break;
		case 'D':
			move_left(ed);
			break;
		case 'H':
			move_home(ed);
			break;
		case 'F':
			move_end(ed);
			break;
		default:
			return -1;
		}
		return 1;
	}

	if (final == '~') {
		int code = atoi(tmp + 1);

		switch (code) {
		case 1:
		case 7:
			move_home(ed);
			break;
		case 4:
		case 8:
			move_end(ed);
			break;
		case 3:
			delete_forward(ed);
			break;
		case 5:
			move_page(ed, -1);
			break;
		case 6:
			move_page(ed, 1);
			break;
		default:
			return -1;
		}
		return 1;
	}

	if ((final >= '0' && final <= '9') || final == ';')
		return 0;

	return -1;
}

static void handle_key(struct editor *ed, unsigned char c)
{
	switch (c) {
	case 0x02: /* ^B */
		move_left(ed);
		break;
	case 0x06: /* ^F */
		move_right(ed);
		break;
	case 0x0e: /* ^N */
		move_down(ed);
		break;
	case 0x10: /* ^P */
		move_up(ed);
		break;
	case 0x01: /* ^A */
		move_home(ed);
		break;
	case 0x05: /* ^E */
		move_end(ed);
		break;
	case 0x0b: /* ^K */
		kill_to_eol(ed);
		break;
	case 0x0a:
	case 0x0d:
		insert_newline(ed);
		break;
	case 0x08:
	case 0x7f:
		backspace(ed);
		break;
	case 0x09:
		insert_byte(ed, '\t');
		break;
	case 0x11: /* ^Q */
		request_quit(ed);
		break;
	case 0x13: /* ^S */
		save_file(ed);
		break;
	default:
		if (c >= 0x20 && c < 0x7f)
			insert_byte(ed, c);
		break;
	}
}

static void feed_byte(struct editor *ed, struct key_state *keys,
		      unsigned char c)
{
	if (ed->debug_keys) {
		size_t n = strlen(ed->keylog);

		snprintf(ed->keylog + n, sizeof(ed->keylog) - n, "%02x ",
			 c);
		if (strlen(ed->keylog) > 40) {
			memmove(ed->keylog, ed->keylog + 12,
				strlen(ed->keylog + 12) + 1);
		}
	}

	/* 8-bit CSI: many terminals send 0x9b instead of ESC '['. */
	if (!keys->esc && c == 0x9b) {
		keys->esc = 1;
		keys->ticks = 0;
		keys->n = 0;
		keys->seq[keys->n++] = '[';
		return;
	}

	/* 8-bit SS3: 0x8f instead of ESC 'O'. */
	if (!keys->esc && c == 0x8f) {
		keys->esc = 1;
		keys->ticks = 0;
		keys->n = 0;
		keys->seq[keys->n++] = 'O';
		return;
	}

	if (keys->esc) {
		int r;

		/* Repeated ESC: ignore the duplicate, keep waiting. */
		if (keys->n == 0 && c == 0x1b) {
			keys->ticks = 0;
			return;
		}

		/* A lone ESC followed by a normal byte: drop ESC, keep byte. */
		if (keys->n == 0 && c != '[' && c != 'O') {
			reset_escape(keys);
			handle_key(ed, c);
			return;
		}

		keys->ticks = 0;
		if (keys->n < sizeof(keys->seq))
			keys->seq[keys->n++] = c;

		r = parse_escape(ed, keys);
		if (r != 0)
			reset_escape(keys);
		return;
	}

	if (c == 0x1b) {
		keys->esc = 1;
		keys->ticks = 0;
		keys->n = 0;
		return;
	}

	handle_key(ed, c);
}

/* ---- terminal setup / teardown ---- */

static void get_winsize(struct editor *ed)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row &&
	    ws.ws_col) {
		ed->rows = ws.ws_row;
		ed->cols = ws.ws_col;
	} else {
		ed->rows = 24;
		ed->cols = 80;
	}

	if (ed->rows < 3)
		ed->rows = 3;
	if (ed->cols < 20)
		ed->cols = 20;
}

static void restore_terminal(void)
{
	static const char show[] = "\033[?25h\033[0m\r\n";

	if (!termios_saved)
		return;
	(void)write_all(STDOUT_FILENO, show, sizeof(show) - 1);
	(void)ioctl(STDIN_FILENO, TCSETS, &saved_termios);
	termios_saved = 0;
}

static void on_winch(int sig)
{
	(void)sig;
	winch_flag = 1;
}

static void on_fatal(int sig)
{
	restore_terminal();
	_exit(128 + sig);
}

static int set_raw_mode(void)
{
	struct termios raw;

	if (ioctl(STDIN_FILENO, TCGETS, &saved_termios) != 0)
		return -1;

	raw = saved_termios;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	raw.c_iflag &= ~(ICRNL | INLCR | IGNCR | IXON | IXOFF);
	raw.c_oflag &= ~OPOST;
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (ioctl(STDIN_FILENO, TCSETS, &raw) != 0)
		return -1;

	termios_saved = 1;
	return 0;
}

static void free_editor(struct editor *ed)
{
	for (size_t i = 0; i < ed->nlines; i++)
		free(ed->lines[i].data);
	free(ed->lines);
	free(ed->path);
	free(ed->dir);
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	struct editor ed;
	struct key_state keys = { 0 };
	char buf[128];
	int err = 0;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	static const struct app_option options[] = {
		{ 'k', 'k', "keys", APP_OPT_NO_ARG },
		APP_OPTION_END,
	};
	int opt;
	int debug_keys = 0;

	while ((opt = app_optparse(&ctx, options)) > 0) {
		if (opt == 'k')
			debug_keys = 1;
	}

	if (app_operand_count(&ctx) != 1)
		app_usage("Usage: edit [-k] FILE");

	memset(&ed, 0, sizeof(ed));
	ed.debug_keys = debug_keys;
	ed.path = strdup(app_operand(&ctx, 0));
	if (!ed.path)
		app_fail("out of memory");
	ed.dir = parent_dir(ed.path);
	if (!ed.dir)
		app_fail("out of memory");
	ed.rows = 24;
	ed.cols = 80;

	if (load_file(&ed)) {
		app_error("cannot load %s: %s", ed.path, strerror(errno));
		free_editor(&ed);
		return APP_EXIT_FAIL;
	}

	get_winsize(&ed);

	if (set_raw_mode()) {
		app_error("standard input is not a terminal");
		free_editor(&ed);
		return APP_EXIT_FAIL;
	}

	signal(SIGWINCH, on_winch);
	signal(SIGTERM, on_fatal);
	signal(SIGHUP, on_fatal);

	if (draw(&ed)) {
		err = APP_EXIT_FAIL;
		goto done;
	}

	for (;;) {
		ssize_t n;

		if (winch_flag) {
			winch_flag = 0;
			get_winsize(&ed);
			if (draw(&ed)) {
				err = APP_EXIT_FAIL;
				break;
			}
		}

		if (ed.quit)
			break;

		n = read(STDIN_FILENO, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			snprintf(ed.status, sizeof(ed.status), "read: %s",
				 strerror(errno));
			err = APP_EXIT_FAIL;
			break;
		}

		if (n == 0) {
			/*
			 * A lone ESC is not a key by itself.  Give the rest of
			 * an arrow/function-key sequence a few poll intervals
			 * to arrive before discarding it.
			 */
			if (keys.esc && ++keys.ticks >= 30) {
				reset_escape(&keys);
				if (draw(&ed)) {
					err = APP_EXIT_FAIL;
					break;
				}
			}
			continue;
		}

		for (ssize_t i = 0; i < n; i++)
			feed_byte(&ed, &keys, (unsigned char)buf[i]);

		/* Wait for an in-flight ESC sequence before redrawing. */
		if (!keys.esc && draw(&ed)) {
			err = APP_EXIT_FAIL;
			break;
		}
	}

done:
	restore_terminal();
	free_editor(&ed);
	return err;
}
