#include <dirent.h>
#include <limits.h>
#include <net/if.h>
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
#include <uapi/time.h>
#include <uapi/uio.h>
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

/*
 * Cross-boundary pins: the POSIX types below cross the syscall
 * boundary by pointer, so their layouts must equal the kernel's
 * __brk_ ABI types.  Types built by field-wise conversion (struct
 * stat, statvfs, tms, utsname, rusage, brk_proc ...) need no pin.
 */
_Static_assert(sizeof(struct sigaction) == sizeof(struct __brk_sigaction),
	       "sigaction layout drift vs kernel");
_Static_assert(offsetof(struct sigaction, sa_mask)
	       == offsetof(struct __brk_sigaction, sa_mask),
	       "sigaction sa_mask drift vs kernel");
_Static_assert(sizeof(struct timespec) == sizeof(struct __brk_timespec),
	       "timespec layout drift vs kernel");
_Static_assert(sizeof(struct pollfd) == sizeof(struct __brk_pollfd),
	       "pollfd layout drift vs kernel");
_Static_assert(sizeof(struct iovec) == sizeof(struct __brk_iovec),
	       "iovec layout drift vs kernel");
_Static_assert(sizeof(struct termios) == sizeof(struct __brk_termios),
	       "termios layout drift vs kernel");
_Static_assert(offsetof(struct termios, c_cc)
	       == offsetof(struct __brk_termios, c_cc),
	       "termios c_cc drift vs kernel");
_Static_assert(offsetof(struct termios, c_ispeed)
	       == offsetof(struct __brk_termios, c_ispeed),
	       "termios c_ispeed drift vs kernel");
_Static_assert(offsetof(struct termios, c_ospeed)
	       == offsetof(struct __brk_termios, c_ospeed),
	       "termios c_ospeed drift vs kernel");
_Static_assert(sizeof(struct winsize) == sizeof(struct __brk_winsize),
	       "winsize layout drift vs kernel");
_Static_assert(sizeof(struct sockaddr_in) == sizeof(struct __brk_sockaddr_in),
	       "sockaddr_in layout drift vs kernel");
_Static_assert(offsetof(struct sockaddr_in, sin_port)
	       == offsetof(struct __brk_sockaddr_in, sin_port),
	       "sockaddr_in sin_port drift vs kernel");
_Static_assert(offsetof(struct sockaddr_in, sin_addr)
	       == offsetof(struct __brk_sockaddr_in, sin_addr),
	       "sockaddr_in sin_addr drift vs kernel");
_Static_assert(sizeof(struct ifreq) == sizeof(struct __brk_ifreq),
	       "ifreq layout drift vs kernel");
_Static_assert(sizeof(struct ifconf) == sizeof(struct __brk_ifconf),
	       "ifconf layout drift vs kernel");
_Static_assert(__builtin_types_compatible_p(sigset_t, __brk_sigset_t),
	       "sigset_t no longer aliases the kernel type");
_Static_assert(__builtin_types_compatible_p(siginfo_t, __brk_siginfo_t),
	       "siginfo_t no longer aliases the kernel type");
_Static_assert(sizeof(struct timeval) == sizeof(struct __brk_timeval),
	       "timeval layout drift vs kernel");
_Static_assert(sizeof(struct tms) == 32, "struct tms ABI changed");

_Static_assert(sizeof(struct dirent) == 268, "struct dirent ABI changed");
_Static_assert(sizeof(ino_t) == 8, "ino_t is not 64-bit");
_Static_assert(sizeof(struct posix_dent) == 24,
	       "struct posix_dent ABI changed");
_Static_assert(offsetof(struct posix_dent, d_name) == 17,
	       "struct posix_dent ABI changed");

_Static_assert(sizeof(struct utsname) == 390, "struct utsname ABI changed");
_Static_assert(sizeof(struct __brk_cap_header) == 8, "struct __brk_cap_header ABI changed");
_Static_assert(sizeof(struct __brk_cap_data) == 12, "struct __brk_cap_data ABI changed");
_Static_assert(sizeof(struct winsize) == 8, "struct winsize ABI changed");
_Static_assert(sizeof(struct sockaddr_in) == 16,
	       "struct sockaddr_in ABI changed");
_Static_assert(sizeof(struct pollfd) == 8, "struct pollfd ABI changed");
_Static_assert(sizeof(struct termios) == 60, "struct termios ABI changed");
