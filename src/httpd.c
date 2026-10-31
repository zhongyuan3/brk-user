#include <apputil.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] =
	"Usage: httpd [-p PORT] [-r ROOT]\n"
	"Serve one HTTP request from ROOT (default: current directory).\n"
	"\n"
	"Options:\n"
	"  -p PORT  listen port (default 8080)\n"
	"  -r ROOT  document root (default .)\n"
	"  --help   display this help and exit";

static const struct app_option opts[] = {
	{ 'p', 'p', NULL, APP_OPT_REQUIRED_ARG },
	{ 'r', 'r', NULL, APP_OPT_REQUIRED_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
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

static int send_error(int fd, const char *status, const char *body)
{
	char hdr[256];
	int n = snprintf(hdr, sizeof(hdr),
			 "HTTP/1.0 %s\r\n"
			 "Content-Type: text/plain\r\n"
			 "Content-Length: %zu\r\n"
			 "Connection: close\r\n\r\n",
			 status, strlen(body));

	if (n < 0 || (size_t)n >= sizeof(hdr))
		return -1;
	if (write_all(fd, hdr, (size_t)n) != 0)
		return -1;
	return write_all(fd, body, strlen(body));
}

static void send_file(int fd, const char *root, const char *path)
{
	char full[512];
	struct stat st;
	int filefd;

	if (strstr(path, "..")) {
		send_error(fd, "400 Bad Request", "bad path\n");
		return;
	}

	int n = snprintf(full, sizeof(full), "%s%s", root, path);

	if (n < 0 || (size_t)n >= sizeof(full)) {
		send_error(fd, "414 URI Too Long", "path too long\n");
		return;
	}
	filefd = open(full, O_RDONLY);
	if (filefd < 0) {
		send_error(fd, "404 Not Found", "not found\n");
		return;
	}
	if (fstat(filefd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(filefd);
		send_error(fd, "403 Forbidden", "not a regular file\n");
		return;
	}

	char hdr[256];
	int hlen = snprintf(hdr, sizeof(hdr),
			    "HTTP/1.0 200 OK\r\n"
			    "Content-Type: application/octet-stream\r\n"
			    "Content-Length: %ld\r\n"
			    "Connection: close\r\n\r\n",
			    (long)st.st_size);

	if (hlen < 0 || (size_t)hlen >= sizeof(hdr) ||
	    write_all(fd, hdr, (size_t)hlen) != 0) {
		close(filefd);
		return;
	}

	char buf[1024];
	for (;;) {
		ssize_t r = read(filefd, buf, sizeof(buf));

		if (r < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (r == 0)
			break;
		if (write_all(fd, buf, (size_t)r) != 0)
			break;
	}
	close(filefd);
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	unsigned long port = 8080;
	const char *root = ".";
	int opt;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'p') {
			if (app_parse_ulong(ctx.arg, &port, 1, 65535) != 0)
				return APP_EXIT_FAIL;
		}
		if (opt == 'r')
			root = ctx.arg;
	}

	if (app_operand_count(&ctx) != 0)
		app_usage(usage);

	int fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		return app_fail_errno("socket failed");

	int one = 1;
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons((unsigned short)port),
		.sin_addr.s_addr = INADDR_ANY,
	};

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
	    listen(fd, 4) != 0) {
		int err = errno;

		close(fd);
		errno = err;
		return app_fail_errno("listen failed");
	}

	fprintf(stderr, "httpd: listening on port %lu\n", port);
	fflush(stderr);

	int cfd = accept(fd, NULL, NULL);

	close(fd);
	if (cfd < 0)
		return app_fail_errno("accept failed");

	char request[1024];
	size_t used = 0;

	while (used + 1 < sizeof(request)) {
		ssize_t n = recv(cfd, request + used,
				 sizeof(request) - used - 1, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(cfd);
			return app_fail_errno("recv failed");
		}
		if (n == 0)
			break;
		used += (size_t)n;
		request[used] = '\0';
		if (strstr(request, "\r\n\r\n") || strstr(request, "\n\n"))
			break;
	}

	char *method = request;
	char *p = strchr(method, ' ');

	if (!p) {
		send_error(cfd, "400 Bad Request", "bad request\n");
		close(cfd);
		return APP_EXIT_FAIL;
	}
	*p++ = '\0';
	while (*p == ' ')
		p++;
	char *path = p;

	p = strchr(path, ' ');
	if (!p) {
		send_error(cfd, "400 Bad Request", "bad request\n");
		close(cfd);
		return APP_EXIT_FAIL;
	}
	*p = '\0';

	if (strcmp(method, "GET")) {
		send_error(cfd, "405 Method Not Allowed",
			   "only GET supported\n");
		close(cfd);
		return APP_EXIT_FAIL;
	}

	char *query = strchr(path, '?');
	if (query)
		*query = '\0';
	if (!strcmp(path, "/"))
		strcpy(path, "/index.html");

	send_file(cfd, root, path);
	close(cfd);
	return APP_EXIT_OK;
}
