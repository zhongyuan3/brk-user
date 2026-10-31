#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <pwutil.h>

#define SALT_LEN 8
#define PW_TMP_LEN 256

static void hex_encode(const unsigned char *in, size_t len, char *out)
{
	static const char hex[] = "0123456789abcdef";

	for (size_t i = 0; i < len; i++) {
		out[i * 2] = hex[in[i] >> 4];
		out[i * 2 + 1] = hex[in[i] & 0xf];
	}
	out[len * 2] = '\0';
}

int pw_hash(const char *password, const char *salt, char *out, size_t size)
{
	char buf[PW_TMP_LEN];
	unsigned char digest[32];
	char hex[PW_HASH_HEX_LEN + 1];
	size_t slen = strlen(salt);
	size_t plen = strlen(password);
	int n;

	if (slen + plen >= sizeof(buf))
		return -1;

	memcpy(buf, salt, slen);
	memcpy(buf + slen, password, plen);

	sha256(buf, slen + plen, digest);
	hex_encode(digest, sizeof(digest), hex);

	n = snprintf(out, size, "$sha256$%s$%s", salt, hex);
	if (n < 0 || (size_t)n >= size)
		return -1;
	return 0;
}

int pw_verify(const char *password, const char *stored)
{
	const char *salt, *end;
	char computed[PW_HASH_HEX_LEN + 1];
	char saltbuf[32];
	unsigned char digest[32];
	size_t slen, plen;
	int diff = 0;

	if (!stored || strncmp(stored, "$sha256$", 8))
		return -1;

	salt = stored + 8;
	end = strchr(salt, '$');
	if (!end)
		return -1;

	slen = (size_t)(end - salt);
	if (slen == 0 || slen >= sizeof(saltbuf))
		return -1;
	memcpy(saltbuf, salt, slen);
	saltbuf[slen] = '\0';

	if (strlen(end + 1) != PW_HASH_HEX_LEN)
		return -1;

	plen = strlen(password);
	if (slen + plen >= PW_TMP_LEN)
		return -1;

	char buf[PW_TMP_LEN];

	memcpy(buf, saltbuf, slen);
	memcpy(buf + slen, password, plen);
	sha256(buf, slen + plen, digest);
	hex_encode(digest, sizeof(digest), computed);

	for (int i = 0; i < PW_HASH_HEX_LEN; i++)
		diff |= computed[i] ^ end[1 + i];

	return diff ? -1 : 0;
}

int pw_generate(const char *password, char *out, size_t size)
{
	struct timeval tv;
	char salt[SALT_LEN + 1];
	unsigned int seed;

	gettimeofday(&tv, NULL);
	seed = (unsigned int)tv.tv_sec ^ ((unsigned int)tv.tv_usec << 12) ^
	       (unsigned int)getpid();

	snprintf(salt, sizeof(salt), "%08x", seed);
	return pw_hash(password, salt, out, size);
}

ssize_t read_password(char *buf, size_t size)
{
	struct termios old, noecho;
	int restore = 0;

	if (ioctl(STDIN_FILENO, TCGETS, &old) == 0) {
		noecho = old;
		noecho.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHOCTL);
		if (ioctl(STDIN_FILENO, TCSETS, &noecho) == 0)
			restore = 1;
	}

	if (!fgets(buf, size, stdin)) {
		if (restore)
			ioctl(STDIN_FILENO, TCSETS, &old);
		return -1;
	}

	if (restore) {
		ioctl(STDIN_FILENO, TCSETS, &old);
		putchar('\n');
	}

	size_t len = strlen(buf);

	while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';

	return (ssize_t)len;
}
