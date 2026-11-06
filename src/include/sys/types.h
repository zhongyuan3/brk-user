#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <uapi/types.h>

/*
 * POSIX spellings of the kernel ABI types.  Userland owns these typedefs;
 * the kernel only publishes the __kernel_* forms in <uapi/types.h>.
 */
typedef __kernel_dev_t dev_t;
typedef __kernel_mode_t mode_t;
typedef __kernel_nlink_t nlink_t;
typedef __kernel_ino_t ino_t;
typedef __kernel_off_t off_t;
typedef __kernel_loff_t loff_t;
typedef __kernel_pid_t pid_t;
typedef __kernel_pid_t id_t;
typedef int clockid_t;
typedef __kernel_clock_t clock_t;
typedef __kernel_suseconds_t suseconds_t;
typedef __kernel_time_t time_t;
typedef __kernel_uid_t uid_t;
typedef __kernel_gid_t gid_t;
typedef __kernel_ssize_t ssize_t;
typedef __kernel_blkcnt_t blkcnt_t;
typedef __kernel_blksize_t blksize_t;

#endif
