#ifndef _DLFCN_H
#define _DLFCN_H

/*
 * Dynamic loading API.  The implementations live in the dynamic
 * linker (/lib/ld-brk.so), so they resolve only in programs that
 * run interpreted; static binaries see NULL through weak bindings.
 */

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0

#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1L)

void *dlopen(const char *name, int flags);
void *dlsym(void *handle, const char *name);
int dlclose(void *handle);
char *dlerror(void);

#endif
