#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <math.h>

struct rec {
	int key;
	char name[16];
};

static int cmp(const void *a, const void *b)
{
	const struct rec *ra = a;
	const struct rec *rb = b;

	return ra->key - rb->key;
}

int main(void)
{
	struct rec v[3] = { { 3, "c" }, { 1, "a" }, { 2, "b" } };
	struct dirent *de;
	DIR *d;
	FILE *f;
	char b[32];
	void *p;
	int n = 0;

	qsort(v, 3, sizeof(v[0]), cmp);
	printf("qsort %d%s %d%s %d%s\n", v[0].key, v[0].name, v[1].key,
	       v[1].name, v[2].key, v[2].name);

	p = malloc(128);
	if (p) {
		strcpy(p, "heap");
		printf("malloc %s\n", (char *)p);
		free(p);
	}
	printf("sqrt %.1f\n", sqrt(9.0));

	d = opendir("/");
	if (d) {
		while (readdir(d) && n < 3)
			n++;
		closedir(d);
	}
	printf("dirent %d\n", n);

	f = fopen("/tmp/hosted.txt", "w");
	if (f) {
		fprintf(f, "file %d\n", 7);
		fclose(f);
	}
	f = fopen("/tmp/hosted.txt", "r");
	if (f) {
		if (fgets(b, sizeof(b), f))
			printf("read %s", b);
		fclose(f);
	}
	return 0;
}
