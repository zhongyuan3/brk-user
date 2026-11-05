#include <sys/socket.h>
#include <syscall.h>

int socket(int domain, int type, int protocol)
{
	return syscall(SYS_socket, domain, type, protocol);
}

int bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	return syscall(SYS_bind, fd, addr, addrlen);
}

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	return syscall(SYS_connect, fd, addr, addrlen);
}

int listen(int fd, int backlog)
{
	return syscall(SYS_listen, fd, backlog);
}

int accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	return syscall(SYS_accept, fd, addr, addrlen);
}

int accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	return syscall(SYS_accept4, fd, addr, addrlen, flags);
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
	       const struct sockaddr *addr, socklen_t addrlen)
{
	return syscall(SYS_sendto, fd, buf, len, flags, addr, addrlen);
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
		 struct sockaddr *addr, socklen_t *addrlen)
{
	return syscall(SYS_recvfrom, fd, buf, len, flags, addr, addrlen);
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	return sendto(fd, buf, len, flags, NULL, 0);
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
	return recvfrom(fd, buf, len, flags, NULL, NULL);
}

int shutdown(int fd, int how)
{
	return syscall(SYS_shutdown, fd, how);
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	return syscall(SYS_getsockname, fd, addr, addrlen);
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	return syscall(SYS_getpeername, fd, addr, addrlen);
}

int setsockopt(int fd, int level, int optname, const void *optval,
	       socklen_t optlen)
{
	return syscall(SYS_setsockopt, fd, level, optname, optval, optlen);
}

int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
	return syscall(SYS_getsockopt, fd, level, optname, optval, optlen);
}
