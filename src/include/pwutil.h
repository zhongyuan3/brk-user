#ifndef _PWUTIL_H
#define _PWUTIL_H

#include <stddef.h>
#include <sys/types.h>

#define PW_HASH_HEX_LEN 64
#define PW_HASH_BUF_LEN (7 + 16 + 1 + PW_HASH_HEX_LEN + 1)

void sha256(const void *data, size_t len, unsigned char out[32]);

int pw_hash(const char *password, const char *salt, char *out, size_t size);
int pw_verify(const char *password, const char *stored);
int pw_generate(const char *password, char *out, size_t size);

ssize_t read_password(char *buf, size_t size);

#endif
