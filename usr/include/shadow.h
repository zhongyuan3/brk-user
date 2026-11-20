#ifndef _SHADOW_H
#define _SHADOW_H

struct spwd {
	char *sp_namp;
	char *sp_pwdp;
};

struct spwd *getspnam(const char *name);

#endif
