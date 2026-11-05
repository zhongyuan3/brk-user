#include <arpa/inet.h>
#include <brk/cmd.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char usage[] =
	"Usage: httpget [-i] URL\n"
	"Fetch an http:// URL and write the response body to stdout.\n"
	"\n"
	"Options:\n"
	"  -i       include response headers\n"
	"  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'i', 'i', NULL, BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int write_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t n = send(fd, buf, len, 0);

		if (n < 0) {
			if (errno == EINTR)
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

static int write_stdout(const char *buf, size_t len)
{
	while (len) {
		ssize_t n = write(STDOUT_FILENO, buf, len);

		if (n < 0) {
			if (errno == EINTR)
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

static int read_body(int fd, bool include_headers)
{
	char buf[1024];
	bool in_headers = !include_headers;

	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return brk_cmd_fail_errno("recv failed");
		}
		if (n == 0)
			break;

		const char *p = buf;
		size_t left = (size_t)n;

		if (in_headers) {
			/* Discard headers, keeping a small carry between reads. */
			char carry[4];
			size_t carry_len = 0;

			while (left) {
				char c = *p++;

				left--;
				if (carry_len < sizeof(carry))
					carry[carry_len++] = c;
				else {
					memmove(carry, carry + 1,
						sizeof(carry) - 1);
					carry[sizeof(carry) - 1] = c;
				}
				if ((carry_len >= 4 &&
				     !memcmp(carry + carry_len - 4, "\r\n\r\n",
					     4)) ||
				    (carry_len >= 2 &&
				     !memcmp(carry + carry_len - 2, "\n\n",
					     2))) {
					in_headers = false;
					break;
				}
			}
			if (in_headers)
				continue;
		}

		if (left && write_stdout(p, left) != 0)
			return brk_cmd_fail_errno("write stdout failed");
	}
	return BRK_CMD_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool include_headers = false;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'i')
			include_headers = true;
	}

	if (brk_cmd_operand_count(&ctx) != 1)
		brk_cmd_usage(usage);

	const char *url = brk_cmd_operand(&ctx, 0);

	if (!strncmp(url, "http://", 7))
		url += 7;

	const char *slash = strchr(url, '/');
	const char *path = slash ? slash : "/";
	char hostport[256];

	size_t hostlen = slash ? (size_t)(slash - url) : strlen(url);

	if (!hostlen || hostlen >= sizeof(hostport))
		return brk_cmd_fail("invalid URL");

	memcpy(hostport, url, hostlen);
	hostport[hostlen] = '\0';

	unsigned long port = 80;
	char *colon = strrchr(hostport, ':');

	if (colon) {
		*colon = '\0';
		if (brk_cmd_parse_ulong(colon + 1, &port, 1, 65535) != 0)
			return BRK_CMD_EXIT_FAIL;
	}

	struct hostent *he = gethostbyname(hostport);

	if (!he)
		return brk_cmd_fail("cannot resolve '%s'", hostport);

	struct in_addr addr;

	memcpy(&addr.s_addr, he->h_addr, 4);

	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons((unsigned short)port),
		.sin_addr = addr,
	};

	int fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		return brk_cmd_fail_errno("socket failed");
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		int err = errno;

		close(fd);
		errno = err;
		return brk_cmd_fail_errno("connect failed");
	}

	char request[1024];
	int rlen = snprintf(request, sizeof(request),
			    "GET %s HTTP/1.0\r\n"
			    "Host: %s\r\n"
			    "User-Agent: brk-httpget/1.0\r\n"
			    "Connection: close\r\n\r\n",
			    path, hostport);

	if (rlen < 0 || (size_t)rlen >= sizeof(request) ||
	    write_all(fd, request, (size_t)rlen) != 0) {
		close(fd);
		return brk_cmd_fail_errno("send request failed");
	}
	shutdown(fd, SHUT_WR);

	int ret = read_body(fd, include_headers);

	close(fd);
	return ret;
}
