#include <stddef.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <sys/capability.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>

/*
 * Userland side of the ABI contract.  The kernel has the same assertions in
 * src/kernel/abi_check.c; keeping both means a change to either the kernel
 * UAPI headers or the userland mapping fails the build on the side that is
 * wrong.
 */

_Static_assert(sizeof(struct stat) == 128, "struct stat ABI changed");
_Static_assert(offsetof(struct stat, st_ino) == 8, "struct stat ABI changed");
_Static_assert(offsetof(struct stat, st_mode) == 16, "struct stat ABI changed");
_Static_assert(offsetof(struct stat, st_size) == 48, "struct stat ABI changed");
_Static_assert(offsetof(struct stat, st_atime) == 72, "struct stat ABI changed");

_Static_assert(sizeof(struct statfs) == 120, "struct statfs ABI changed");
_Static_assert(sizeof(struct timespec) == 16, "struct timespec ABI changed");
_Static_assert(sizeof(struct timeval) == 16, "struct timeval ABI changed");
_Static_assert(sizeof(struct rusage) == 144, "struct rusage ABI changed");
_Static_assert(sizeof(struct sigaction) == 24, "struct sigaction ABI changed");
_Static_assert(sizeof(struct tms) == 32, "struct tms ABI changed");

_Static_assert(sizeof(struct dirent) == 268, "struct dirent ABI changed");
_Static_assert(sizeof(struct dirent64) == 280, "struct dirent64 ABI changed");

_Static_assert(sizeof(struct utsname) == 390, "struct utsname ABI changed");
_Static_assert(sizeof(struct cap_header) == 8, "struct cap_header ABI changed");
_Static_assert(sizeof(struct cap_data) == 12, "struct cap_data ABI changed");
_Static_assert(sizeof(struct winsize) == 8, "struct winsize ABI changed");
_Static_assert(sizeof(struct sockaddr_in) == 16, "struct sockaddr_in ABI changed");
_Static_assert(sizeof(struct pollfd) == 8, "struct pollfd ABI changed");
_Static_assert(sizeof(struct termios) == 60, "struct termios ABI changed");
