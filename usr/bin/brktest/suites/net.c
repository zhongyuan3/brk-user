#include "../harness.h"
#include "../util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static int case_inet(struct test_ctx *ctx)
{
	struct in_addr in;

	(void)ctx;
	if (!inet_aton("127.0.0.1", &in))
		return test_fail_msg("net: inet_aton failed");
	if (strcmp(inet_ntoa(in), "127.0.0.1"))
		return test_fail_msg("net: inet_ntoa mismatch");
	if (ntohs(htons(0x1234)) != 0x1234 ||
	    ntohl(htonl(0x12345678)) != 0x12345678)
		return test_fail_msg("net: byte-order helpers failed");
	return 0;
}

static int case_tcp(struct test_ctx *ctx)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = 0,
	};
	struct sockaddr_in peer;
	socklen_t addrlen = sizeof(addr);
	socklen_t peerlen = sizeof(peer);
	struct pollfd pfd;
	char buf[8];
	int listener;
	int accepted;
	pid_t pid;
	int status;

	(void)ctx;
	if (!inet_aton("127.0.0.1", &addr.sin_addr))
		return test_fail_msg("net: inet_aton failed");
	listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0 || bind(listener, (struct sockaddr *)&addr,
				 sizeof(addr)) != 0 ||
	    listen(listener, 4) != 0 ||
	    getsockname(listener, (struct sockaddr *)&addr, &addrlen) != 0)
		return test_fail_msg("net: TCP listener setup failed: %d", errno);
	if (ntohs(addr.sin_port) == 0)
		return test_fail_msg("net: TCP listener has port 0");

	pid = fork();
	if (pid < 0)
		return test_fail_msg("net: fork failed: %d", errno);
	if (pid == 0) {
		int fd = socket(AF_INET, SOCK_STREAM, 0);

		if (fd < 0 ||
		    connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
			_exit(1);
		if (send(fd, "ping", 4, 0) != 4)
			_exit(2);
		if (shutdown(fd, SHUT_WR) != 0)
			_exit(3);
		if (recv(fd, buf, 4, 0) != 4 || memcmp(buf, "pong", 4))
			_exit(4);
		close(fd);
		_exit(0);
	}

	pfd.fd = listener;
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 3000) != 1 || !(pfd.revents & POLLIN))
		return test_fail_msg("net: TCP accept poll failed");
	accepted = accept(listener, (struct sockaddr *)&peer, &peerlen);
	if (accepted < 0)
		return test_fail_msg("net: TCP accept failed: %d", errno);
	if (peer.sin_family != AF_INET || ntohs(peer.sin_port) == 0 ||
	    ntohl(peer.sin_addr.s_addr) != 0x7f000001)
		return test_fail_msg("net: TCP getpeername is wrong");
	if (getpeername(accepted, (struct sockaddr *)&peer, &peerlen) != 0)
		return test_fail_msg("net: getpeername failed: %d", errno);

	pfd.fd = accepted;
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 3000) != 1 || !(pfd.revents & POLLIN))
		return test_fail_msg("net: TCP data poll failed");
	if (recv(accepted, buf, 4, 0) != 4 || memcmp(buf, "ping", 4))
		return test_fail_msg("net: TCP recv failed");
	if (send(accepted, "pong", 4, 0) != 4)
		return test_fail_msg("net: TCP send failed");
	close(accepted);
	close(listener);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("net: TCP child failed");
	return 0;
}

static int case_udp(struct test_ctx *ctx)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = 0,
	};
	struct sockaddr_in peer;
	socklen_t addrlen = sizeof(addr);
	socklen_t peerlen = sizeof(peer);
	struct pollfd pfd;
	char buf[8];
	int server;
	pid_t pid;
	int status;

	(void)ctx;
	if (!inet_aton("127.0.0.1", &addr.sin_addr))
		return test_fail_msg("net: inet_aton failed");
	server = socket(AF_INET, SOCK_DGRAM, 0);
	if (server < 0 || bind(server, (struct sockaddr *)&addr,
			       sizeof(addr)) != 0 ||
	    getsockname(server, (struct sockaddr *)&addr, &addrlen) != 0)
		return test_fail_msg("net: UDP bind failed: %d", errno);
	if (ntohs(addr.sin_port) == 0)
		return test_fail_msg("net: UDP server has port 0");

	pid = fork();
	if (pid < 0)
		return test_fail_msg("net: fork failed: %d", errno);
	if (pid == 0) {
		int fd = socket(AF_INET, SOCK_DGRAM, 0);

		if (fd < 0)
			_exit(1);
		if (sendto(fd, "u", 1, 0, (struct sockaddr *)&addr,
			   sizeof(addr)) != 1)
			_exit(2);
		if (recv(fd, buf, 1, 0) != 1 || buf[0] != 'v')
			_exit(3);
		close(fd);
		_exit(0);
	}

	pfd.fd = server;
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 3000) != 1 || !(pfd.revents & POLLIN))
		return test_fail_msg("net: UDP poll failed");
	if (recvfrom(server, buf, 1, 0, (struct sockaddr *)&peer,
		     &peerlen) != 1 || buf[0] != 'u')
		return test_fail_msg("net: UDP recvfrom failed");
	if (peer.sin_family != AF_INET || ntohs(peer.sin_port) == 0)
		return test_fail_msg("net: UDP peer address is wrong");
	if (sendto(server, "v", 1, 0, (struct sockaddr *)&peer, peerlen) != 1)
		return test_fail_msg("net: UDP sendto failed");
	close(server);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("net: UDP child failed");
	return 0;
}

static int case_options(struct test_ctx *ctx)
{
	int fd;
	int value;
	socklen_t len;

	(void)ctx;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return test_fail_msg("net: socket failed: %d", errno);

	len = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &value, &len) != 0 ||
	    value != SOCK_STREAM)
		return test_fail_msg("net: SO_TYPE is wrong");
	value = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value,
		       sizeof(value)) != 0)
		return test_fail_msg("net: SO_REUSEADDR failed: %d", errno);
	len = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &value, &len) != 0 ||
	    value != 0)
		return test_fail_msg("net: SO_ERROR is not clear");

	value = 4096;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &value,
		       sizeof(value)) != 0)
		return test_fail_msg("net: SO_SNDBUF failed: %d", errno);
	len = sizeof(value);
	if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &value, &len) != 0 ||
	    value < 1024)
		return test_fail_msg("net: SO_SNDBUF is wrong");

	value = 1;
	errno = 0;
	if (setsockopt(fd, SOL_SOCKET, 9999, &value, sizeof(value)) != -1 ||
	    errno != ENOPROTOOPT)
		return test_fail_msg("net: unknown socket option was accepted");

	close(fd);
	return 0;
}

static const struct test_case net_cases[] = {
	{ "inet", case_inet, 0, 10000 },
	{ "tcp", case_tcp, TEST_OWN_PGRP, 15000 },
	{ "udp", case_udp, TEST_OWN_PGRP, 15000 },
	{ "options", case_options, 0, 10000 },
};

const struct test_suite brktest_net_suite = {
	.name = "net",
	.description = "TCP/UDP loopback sockets, socket options and inet helpers",
	.cases = net_cases,
	.nr_cases = sizeof(net_cases) / sizeof(net_cases[0]),
};
