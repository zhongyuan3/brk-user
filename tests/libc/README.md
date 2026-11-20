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
