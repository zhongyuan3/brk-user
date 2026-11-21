#ifndef _LIBGEN_H
#define _LIBGEN_H

/*
 * POSIX basename()/dirname().  Both may modify their argument buffer
 * (POSIX allows it; the implementations here never need to).
 */

char *basename(char *path);
char *dirname(char *path);

#endif
