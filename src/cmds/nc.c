#include <arpa/inet.h>
#include <brk/cmd.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char usage[] =
	"Usage: nc [-u] [-l] [-p PORT] [HOST] [PORT]\n"
	"Connect to HOST:PORT, or listen with -l.\n"
	"\n"
	"Options:\n"
	"  -u       use UDP\n"
	"  -l       listen for one connection/datagram\n"
	"  -p PORT  local/listen port\n"
	"  --help   display this help and exit\n"
	"\n"
	"UDP mode sends/receives one datagram; the listener echoes it back.";

static const struct brk_cmd_option opts[] = {
	{ 'u', 'u', "udp", BRK_CMD_OPT_NO_ARG },
	{ 'l', 'l', "listen", BRK_CMD_OPT_NO_ARG },
	{ 'p', 'p', NULL, BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int write_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t n = write(fd, buf, len);

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

static int tcp_relay(int fd)
{
	struct pollfd fds[2] = {
		{ .fd = STDIN_FILENO, .events = POLLIN },
		{ .fd = fd, .events = POLLIN },
	};
	bool stdin_open = true;

	for (;;) {
		int ret = poll(fds, 2, -1);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return brk_cmd_fail_errno("poll failed");
		}

		if (stdin_open && (fds[0].revents & (POLLIN | POLLHUP))) {
			char buf[1024];
			ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));

			if (n < 0) {
				if (errno == EINTR)
					continue;
				return brk_cmd_fail_errno("read stdin failed");
			}
			if (n == 0) {
				stdin_open = false;
				fds[0].fd = -1;
				shutdown(fd, SHUT_WR);
			} else if (write_all(fd, buf, (size_t)n) != 0) {
				if (errno == EPIPE || errno == ECONNRESET)
					break;
				return brk_cmd_fail_errno(
					"write socket failed");
			}
		}

		if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
			char buf[1024];
			ssize_t n = recv(fd, buf, sizeof(buf), 0);

			if (n < 0) {
				if (errno == EINTR)
					continue;
				if (errno == ECONNRESET || errno == EPIPE)
					break;
				return brk_cmd_fail_errno("recv failed");
			}
			if (n == 0)
				break;
			if (write_all(STDOUT_FILENO, buf, (size_t)n) != 0)
				return brk_cmd_fail_errno(
					"write stdout failed");
		}
	}
	return BRK_CMD_EXIT_OK;
}

static int tcp_client(struct in_addr addr, unsigned int port)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)port),
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
	int ret = tcp_relay(fd);

	close(fd);
	return ret;
}

static int tcp_server(unsigned int port)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)port),
		.sin_addr.s_addr = INADDR_ANY,
	};
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;

	if (fd < 0)
		return brk_cmd_fail_errno("socket failed");
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		int err = errno;

		close(fd);
		errno = err;
		return brk_cmd_fail_errno("bind failed");
	}
	if (listen(fd, 1) != 0) {
		int err = errno;

		close(fd);
		errno = err;
		return brk_cmd_fail_errno("listen failed");
	}

	fprintf(stderr, "nc: listening on port %u\n", port);
	fflush(stderr);

	int cfd = accept(fd, NULL, NULL);

	if (cfd < 0) {
		int err = errno;

		close(fd);
		errno = err;
		return brk_cmd_fail_errno("accept failed");
	}
	close(fd);

	int ret = tcp_relay(cfd);

	close(cfd);
	return ret;
}

static int udp_client(struct in_addr addr, unsigned int port)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)port),
		.sin_addr = addr,
	};
	char buf[4096];
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	ssize_t n;

	if (fd < 0)
		return brk_cmd_fail_errno("socket failed");
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		int err = errno;

		close(fd);
		errno = err;
		return brk_cmd_fail_errno("connect failed");
	}

	n = read(STDIN_FILENO, buf, sizeof(buf));
	if (n < 0) {
		close(fd);
		return brk_cmd_fail_errno("read stdin failed");
	}
	if (n > 0) {
		if (send(fd, buf, (size_t)n, 0) < 0 && errno != EAGAIN) {
			int err = errno;

			close(fd);
			errno = err;
			return brk_cmd_fail_errno("send failed");
		}
	}

	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int ret = poll(&pfd, 1, 3000);

	if (ret < 0) {
		if (errno != EINTR) {
			close(fd);
			return brk_cmd_fail_errno("poll failed");
		}
	} else if (ret > 0 && (pfd.revents & POLLIN)) {
		n = recv(fd, buf, sizeof(buf), 0);
		if (n > 0 && write_all(STDOUT_FILENO, buf, (size_t)n) != 0) {
			close(fd);
			return BRK_CMD_EXIT_FAIL;
		}
	}
	close(fd);
	return BRK_CMD_EXIT_OK;
}

static int udp_server(unsigned int port)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)port),
		.sin_addr.s_addr = INADDR_ANY,
	};
	struct sockaddr_in peer;
	socklen_t peerlen = sizeof(peer);
	char buf[4096];
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0)
		return brk_cmd_fail_errno("socket failed");
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		int err = errno;

		close(fd);
		errno = err;
		return brk_cmd_fail_errno("bind failed");
	}

	fprintf(stderr, "nc: listening on udp port %u\n", port);
	fflush(stderr);

	ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer,
			     &peerlen);

	if (n < 0) {
		int err = errno;

		close(fd);
		errno = err;
		return brk_cmd_fail_errno("recvfrom failed");
	}
	if (write_all(STDOUT_FILENO, buf, (size_t)n) != 0) {
		close(fd);
		return BRK_CMD_EXIT_FAIL;
	}
	sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&peer, peerlen);
	close(fd);
	return BRK_CMD_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool udp = false;
	bool listen_mode = false;
	unsigned long local_port = 0;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'u')
			udp = true;
		if (opt == 'l')
			listen_mode = true;
		if (opt == 'p') {
			if (brk_cmd_parse_ulong(ctx.arg, &local_port, 0,
						65535) != 0)
				return BRK_CMD_EXIT_FAIL;
		}
	}

	if (listen_mode) {
		if (!local_port) {
			if (brk_cmd_operand_count(&ctx) != 1)
				brk_cmd_usage(usage);
			if (brk_cmd_parse_ulong(brk_cmd_operand(&ctx, 0),
						&local_port, 1, 65535) != 0)
				return BRK_CMD_EXIT_FAIL;
		}
		if (udp)
			return udp_server((unsigned int)local_port);
		return tcp_server((unsigned int)local_port);
	}

	if (brk_cmd_operand_count(&ctx) != 2)
		brk_cmd_usage(usage);

	struct in_addr addr;
	unsigned long port;
	const char *host = brk_cmd_operand(&ctx, 0);

	if (!strcmp(host, "localhost")) {
		addr.s_addr = INADDR_LOOPBACK;
	} else {
		struct hostent *he = gethostbyname(host);

		if (!he)
			return brk_cmd_fail("cannot resolve '%s'", host);
		memcpy(&addr.s_addr, he->h_addr, 4);
	}
	if (brk_cmd_parse_ulong(brk_cmd_operand(&ctx, 1), &port, 1, 65535) != 0)
		return BRK_CMD_EXIT_FAIL;

	if (udp)
		return udp_client(addr, (unsigned int)port);
	return tcp_client(addr, (unsigned int)port);
}
