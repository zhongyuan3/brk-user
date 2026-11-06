#include <dirent.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/capability.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <termios.h>

/*
 * Userland side of the ABI contract.  The kernel has the same assertions in
 * src/kernel/abi_check.c; keeping both means a change to either the kernel
 * UAPI headers or the userland mapping fails the build on the side that is
 * wrong.
 */

_Static_assert(CHAR_BIT == 8, "CHAR_BIT mismatch");
_Static_assert(sizeof(int8_t) == 1, "int8_t ABI changed");
_Static_assert(sizeof(int16_t) == 2, "int16_t ABI changed");
_Static_assert(sizeof(int32_t) == 4, "int32_t ABI changed");
_Static_assert(sizeof(int64_t) == 8, "int64_t ABI changed");
_Static_assert(sizeof(intptr_t) == sizeof(void *), "intptr_t ABI changed");
_Static_assert(sizeof(size_t) == sizeof(void *), "size_t ABI changed");
_Static_assert(_Alignof(max_align_t) >= _Alignof(long double),
	       "max_align_t alignment changed");

_Static_assert(sizeof(struct stat) == 128, "struct stat ABI changed");
_Static_assert(offsetof(struct stat, st_ino) == 8, "struct stat ABI changed");
_Static_assert(offsetof(struct stat, st_mode) == 16, "struct stat ABI changed");
_Static_assert(offsetof(struct stat, st_size) == 48, "struct stat ABI changed");
_Static_assert(offsetof(struct stat, st_atime) == 72,
	       "struct stat ABI changed");

_Static_assert(sizeof(struct statvfs) == 88, "struct statvfs ABI changed");
_Static_assert(sizeof(struct timespec) == 16, "struct timespec ABI changed");
_Static_assert(sizeof(struct timeval) == 16, "struct timeval ABI changed");
_Static_assert(sizeof(struct iovec) == 16, "struct iovec ABI changed");
_Static_assert(sizeof(struct rusage) == 144, "struct rusage ABI changed");
_Static_assert(sizeof(struct sigaction) == 24, "struct sigaction ABI changed");
_Static_assert(sizeof(struct tms) == 32, "struct tms ABI changed");

_Static_assert(sizeof(struct dirent) == 268, "struct dirent ABI changed");
_Static_assert(sizeof(ino_t) == 8, "ino_t is not 64-bit");
_Static_assert(sizeof(struct posix_dent) == 24,
	       "struct posix_dent ABI changed");
_Static_assert(offsetof(struct posix_dent, d_name) == 17,
	       "struct posix_dent ABI changed");

_Static_assert(sizeof(struct utsname) == 390, "struct utsname ABI changed");
_Static_assert(sizeof(struct cap_header) == 8, "struct cap_header ABI changed");
_Static_assert(sizeof(struct cap_data) == 12, "struct cap_data ABI changed");
_Static_assert(sizeof(struct winsize) == 8, "struct winsize ABI changed");
_Static_assert(sizeof(struct sockaddr_in) == 16,
	       "struct sockaddr_in ABI changed");
_Static_assert(sizeof(struct pollfd) == 8, "struct pollfd ABI changed");
_Static_assert(sizeof(struct termios) == 60, "struct termios ABI changed");
