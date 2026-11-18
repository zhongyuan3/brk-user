#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <uapi/types.h>

/*
 * POSIX spellings of the kernel ABI types.  Userland owns these typedefs;
 * the kernel only publishes the __brk_* forms in <uapi/types.h>.
 */
typedef __brk_dev_t dev_t;
typedef __brk_mode_t mode_t;
typedef __brk_nlink_t nlink_t;
typedef __brk_ino_t ino_t;
typedef __brk_off_t off_t;
typedef __brk_loff_t loff_t;
typedef __brk_pid_t pid_t;
typedef __brk_pid_t id_t;
typedef int clockid_t;
typedef __brk_clock_t clock_t;
typedef __brk_suseconds_t suseconds_t;
typedef __brk_time_t time_t;
typedef __brk_uid_t uid_t;
typedef __brk_gid_t gid_t;
typedef __brk_ssize_t ssize_t;
typedef __brk_blkcnt_t blkcnt_t;
typedef __brk_blksize_t blksize_t;

struct pthread;
typedef struct pthread *pthread_t;

#endif
