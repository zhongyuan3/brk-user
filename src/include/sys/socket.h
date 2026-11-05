#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stddef.h>
#include <sys/types.h>
#include <uapi/socket.h>

struct sockaddr_storage {
	sa_family_t ss_family;
	char __ss_padding[126];
};

int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, socklen_t addrlen);
int connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *addrlen);
int accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);
ssize_t sendto(int fd, const void *buf, size_t len, int flags,
	       const struct sockaddr *addr, socklen_t addrlen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
		 struct sockaddr *addr, socklen_t *addrlen);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);
int shutdown(int fd, int how);
int getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen);
int setsockopt(int fd, int level, int optname, const void *optval,
	       socklen_t optlen);
int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen);

#endif
