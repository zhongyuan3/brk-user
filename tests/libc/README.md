# libc tests

These tests are linked against `libc.a`, so they exercise the libc directly
instead of being compiled by the on-target `cc`.

Build the test binaries:

```sh
make -C brk-user tests
```

The binaries are written to `brk-user/build/<BUILD>/tests/libc/`
(`BUILD` defaults to `debug`). Regenerating the rootfs also builds and
installs them:

```sh
make -C brk-user rootfs
make -C brk-user run
```

Inside BRK, run the allocator test:

```sh
/home/tests/libc/alloc
```

Expected result:

```text
alloc: ok
```

`alloc.c` covers small and large allocations, `calloc` zeroing and overflow,
`realloc` grow/shrink/`NULL`/zero-size behaviour, coalescing across many
blocks, impossible allocation sizes and concurrent allocation from multiple
threads.

The API test covers the pure-composition additions:

```sh
/home/tests/libc/libapi
```

Expected result:

```text
libapi: ok
```

`libapi.c` exercises the directory stream API (opendir/readdir/rewinddir/
telldir/seekdir), getopt()/getopt_long() including attached values, the
"--" terminator and invalid options, the string extensions (strtok_r,
strsep, strcasestr, strchrnul, ffs, basename/dirname, strsignal),
rand/srand/rand_r determinism, div/ldiv/lldiv, atof, asctime/ctime/
gmtime_r/localtime_r and the timegm() round trip, mkstemp/mkdtemp
exclusivity, truncate/ftruncate, isatty/gethostname/ttyname on the
console, the scanf engine (sscanf fields, suppression, %n, matching
failure vs EOF, and fscanf over a tmpfile), realpath with symlink
chains and relative paths, and strptime round-tripping strfime-style
fields through timegm().
