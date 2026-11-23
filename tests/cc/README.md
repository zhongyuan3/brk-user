# cc tests

These source files are copied to `/home/tests/cc/` in the image by `brk-user/scripts/mkrootfs.sh`.

After booting BRK, you can run them like this:

```sh
cc /home/tests/cc/hello.c -o /tmp/hello
/tmp/hello

cc /home/tests/cc/smoke.c -o /tmp/smoke
/tmp/smoke

cc /home/tests/cc/expr.c -o /tmp/expr
/tmp/expr

cc /home/tests/cc/io.c -o /tmp/io
/tmp/io hello

cc /home/tests/cc/long.c -o /tmp/long
/tmp/long

cc /home/tests/cc/large.c -o /tmp/large
/tmp/large

cc /home/tests/cc/pp.c -o /tmp/pp
/tmp/pp

cc /home/tests/cc/struct.c -o /tmp/struct
/tmp/struct

cc /home/tests/cc/funcptr.c -o /tmp/funcptr
/tmp/funcptr

cc /home/tests/cc/init.c -o /tmp/init
/tmp/init

cc /home/tests/cc/ginit.c -o /tmp/ginit
/tmp/ginit

cc /home/tests/cc/designated.c -o /tmp/designated
/tmp/designated

cc /home/tests/cc/macros.c -o /tmp/macros
/tmp/macros

cc /home/tests/cc/control.c -o /tmp/control
/tmp/control

cc /home/tests/cc/varargs.c -o /tmp/varargs
/tmp/varargs

cc /home/tests/cc/multi_a.c /home/tests/cc/multi_b.c -o /tmp/multi
/tmp/multi

# Separate compilation / object files
cc -c /home/tests/cc/multi_a.c -o /tmp/a.o
cc -c /home/tests/cc/multi_b.c -o /tmp/b.o
cc /tmp/a.o /tmp/b.o -o /tmp/multi_obj
/tmp/multi_obj

# ABI and standard stdarg
cc /home/tests/cc/abi.c -o /tmp/abi
/tmp/abi
cc /home/tests/cc/stdarg.c -o /tmp/stdarg
/tmp/stdarg

# Hosted libc programs
cc /home/tests/cc/hosted.c -lc -o /tmp/hosted
/tmp/hosted
cc /home/tests/cc/hosted_libc.c -lc -o /tmp/hosted_libc
/tmp/hosted_libc

# Preprocessor additions
cc /home/tests/cc/pp_float.c -o /tmp/pp_float
/tmp/pp_float
cc /home/tests/cc/pp_line.c -o /tmp/pp_line
/tmp/pp_line

# Code-generation additions
cc /home/tests/cc/float.c -o /tmp/float
/tmp/float
cc /home/tests/cc/byvalue.c -o /tmp/byvalue
/tmp/byvalue
cc /home/tests/cc/bitfield.c -o /tmp/bitfield
/tmp/bitfield
cc /home/tests/cc/args.c -o /tmp/args
/tmp/args
cc /home/tests/cc/agg_deep.c -o /tmp/agg_deep
/tmp/agg_deep
cc /home/tests/cc/float_cast.c -o /tmp/float_cast
/tmp/float_cast

# Global address constants and block-scope function declarations
cc /home/tests/cc/gaddr.c -o /tmp/gaddr
/tmp/gaddr
cc /home/tests/cc/blockfn.c -o /tmp/blockfn
/tmp/blockfn

# Front-end feature fixtures
cc /home/tests/cc/front_float.c -o /tmp/front_float
/tmp/front_float
cc /home/tests/cc/front_aggregate.c -o /tmp/front_aggregate
/tmp/front_aggregate
cc /home/tests/cc/front_bitfield.c -o /tmp/front_bitfield
/tmp/front_bitfield
cc /home/tests/cc/front_static.c -o /tmp/front_static
/tmp/front_static
cc /home/tests/cc/front_manyargs.c -o /tmp/front_manyargs
/tmp/front_manyargs

# Cross-unit CCO2/relocation fixture
cc /home/tests/cc/link_multi_a.c /home/tests/cc/link_multi_b.c -o /tmp/link_multi
/tmp/link_multi

# Assembly listing mode
cc -S /home/tests/cc/hello.c -o /tmp/hello.s
```

Expected results:

- `hello`: `hello from cc`
- `smoke`:

  ```text
  sum=
  30
  ```

- `expr`: in order, `20`, `13`, `3`, `8`, `4`, `2`, `5 7`, `hi`, `abc`,
  `4 16 1`, `55`, `123`, `1`
- `io hello`: starts with `8`, followed by `file-io`, then `8 4 -4`, etc.;
  the last line is `2 /tmp/io` and `hello`
- `long`: `1118`
- `large`: `999` and `512`
- `pp`: `pp-ok`, `43`, `1`, `45`
- `struct`: `7`, `10`, `10`, `A`, `42`, `DCBA`, `0 5 6`, `8 4 12`, `7 0`
- `funcptr`: `15`, `5`, `123`, `15`, `-1`, `23 17`, `3`
- `init`: `6`, `hi`, `15`, `15 ab`, `9`, `10`, `hi`, `7`, `30 xy`
- `ginit`: `15 ab`, `5 b`, `60`, `10`, `global`, `x`, `y`, `5`, `DCBA`, `5`
- `designated`: `7 8 ab`, `1 2 cd`, `3`, `10 0 20 30 0`, `1 2 3 4`,
  `1 a`, `3 b`, `5 0 lo`, `0 2 0 9`, `5 6 7 8`, `9 x`, `11 y`
- `macros`: `5`, `9`, `16`, `10`, `7`, `9`, `11`, `9`, `6`, `5`, `macros`
- `control`: `10 20 20 6 99`, `3 2 13 10`, `406`, `10`
- `varargs`: `42`, `15`, `0`, `15`, `1`, `alpha`, `beta`, `gamma`,
  `5-mid-7`
- `multi`: `multi`, `5`, `101`, `1`, `2`, `7`
- `multi_obj`: the same as `multi`; both `.o` files contain a same-named `static helper`,
  used to verify that local symbols do not conflict
- `abi`: `3 1 2 3`, `12`, `66`, `4 6`, `45`, `66`, `10`, `55`, `66`, `58`,
  `88`, `42`
- `stdarg`: `10 14`
- `hosted`: `hosted 42 9`
- `hosted_libc`: `qsort 1a 2b 3c`, `malloc heap`, `sqrt 3.0`,
  `dirent 3`, `read file 7`
- `pp_float`: `pp_float ok`
- `pp_line`: `pp_line ok`
- `float`: `4 5 7 4 9`, `3 4 1 1 1 1 0`, `8 6`, `3 3 3`, `1 1 1`
- `byvalue`: `7`, `10`, `7 10`, `10 13`, `10 14`, `8 11`
- `bitfield`: `5 17 -3 1`, `7 0 1 -8 7`, `0`, `0`, `123 9 456`,
  `1 579`, `7 -3 1 -6`, `18 703710 155`
- `args`: `55`, `55`, `39`, `43`, `10`, `45`
- `agg_deep`: `10 35`, `30 105`, `10 35`
- `float_cast`: `-2 1 -1`, `1 1`
- `gaddr`: `20 30 30 6`
- `blockfn`: `5 6 7`
- `front_float`: `1 -2 -6 2 -1 4 1`, `-1 6 1 4 8 8`, `-2`, `6`, `5`,
  `3 9`, `0 0 1 1 7`, `63`, `3 6`, `3 2`, `3 11`, `2`, `3`, `125`
- `front_aggregate`: `13 24`, `3 103`, `170`, `13 24`, `8 16 4`, `1`, `3 6`
- `front_bitfield`: `8`, `0 15 6 74565`, `25859`, `-3 12 1`, `8`, `4 4`,
  `6 31 2`, `120 121 4`
- `front_static`: `1 2 3`, `100`, `10 20`, `13 1 2 3`, `13 1 2 3`,
  `2 3 11`, `7`, `8`, `5 6`
- `front_manyargs`: `55`, `45`, `78`, `550`, `155`, `24`
- `link_multi`: `link`, `42`, `5`, `1`, `2`, `10`, `20`, `ab`, `17`, `0`, `0`,
  `cross`
- `hello.s`: a readable `.text` listing containing `main:` and RISC-V mnemonics

`pp.c` depends on `/usr/include/cc.h` and `pp.h` in the same directory, so the rootfs build script must first
install the headers and copy the entire `tests/` directory.

Host-side CCO2, relocation, malformed-object and ELF-layout checks live in
`link_tests.sh`; run them without booting BRK:

```sh
make cc-host HOSTCC=gcc
make cc-link-tests
```
