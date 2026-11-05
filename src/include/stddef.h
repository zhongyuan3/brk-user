#ifndef _STDDEF_H
#define _STDDEF_H

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __WCHAR_TYPE__ wchar_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

/* Type with the strictest alignment requirement on the target. */
typedef struct {
	long long __max_align_ll
		__attribute__((__aligned__(__alignof__(long long))));
	long double __max_align_ld
		__attribute__((__aligned__(__alignof__(long double))));
} max_align_t;

#endif
