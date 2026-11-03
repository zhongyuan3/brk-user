# cc tests

这些源文件会被 `brk-user/scripts/mkrootfs.sh` 复制到镜像的 `/home/tests/`。

启动 BRK 后可以这样运行：

```sh
cc /home/tests/hello.c -o /tmp/hello
/tmp/hello

cc /home/tests/smoke.c -o /tmp/smoke
/tmp/smoke

cc /home/tests/expr.c -o /tmp/expr
/tmp/expr

cc /home/tests/io.c -o /tmp/io
/tmp/io hello

cc /home/tests/long.c -o /tmp/long
/tmp/long

cc /home/tests/large.c -o /tmp/large
/tmp/large

cc /home/tests/pp.c -o /tmp/pp
/tmp/pp

cc /home/tests/struct.c -o /tmp/struct
/tmp/struct

cc /home/tests/funcptr.c -o /tmp/funcptr
/tmp/funcptr

cc /home/tests/init.c -o /tmp/init
/tmp/init

cc /home/tests/ginit.c -o /tmp/ginit
/tmp/ginit

cc /home/tests/designated.c -o /tmp/designated
/tmp/designated

cc /home/tests/macros.c -o /tmp/macros
/tmp/macros

cc /home/tests/control.c -o /tmp/control
/tmp/control

cc /home/tests/varargs.c -o /tmp/varargs
/tmp/varargs

cc /home/tests/multi_a.c /home/tests/multi_b.c -o /tmp/multi
/tmp/multi

# 分段编译 / 对象文件
cc -c /home/tests/multi_a.c -o /tmp/a.o
cc -c /home/tests/multi_b.c -o /tmp/b.o
cc /tmp/a.o /tmp/b.o -o /tmp/multi_obj
/tmp/multi_obj
```

预期结果：

- `hello`：`hello from cc`
- `smoke`：

  ```text
  sum=
  30
  ```

- `expr`：依次为 `20`、`13`、`3`、`8`、`4`、`2`、`5 7`、`hi`、`abc`、
  `4 16 1`、`55`、`123`、`1`
- `io hello`：以 `8` 开头，随后是 `file-io`，再往后是 `8 4 -4` 等；
  最后一行是 `2 /tmp/io` 和 `hello`
- `long`：`1118`
- `large`：`999` 和 `512`
- `pp`：`pp-ok`、`43`、`1`、`45`
- `struct`：`7`、`10`、`10`、`A`、`42`、`DCBA`、`0 5 6`、`8 4 12`、`7 0`
- `funcptr`：`15`、`5`、`123`、`15`、`-1`、`23 17`、`3`
- `init`：`6`、`hi`、`15`、`15 ab`、`9`、`10`、`hi`、`7`、`30 xy`
- `ginit`：`15 ab`、`5 b`、`60`、`10`、`global`、`x`、`y`、`5`、`DCBA`、`5`
- `designated`：`7 8 ab`、`1 2 cd`、`3`、`10 0 20 30 0`、`1 2 3 4`、
  `1 a`、`3 b`、`5 0 lo`、`0 2 0 9`、`5 6 7 8`、`9 x`、`11 y`
- `macros`：`5`、`9`、`16`、`10`、`7`、`9`、`11`、`9`、`6`、`5`、`macros`
- `control`：`10 20 20 6 99`、`3 2 13 10`、`406`、`10`
- `varargs`：`42`、`15`、`0`、`15`、`1`、`alpha`、`beta`、`gamma`、
  `5-mid-7`
- `multi`：`multi`、`5`、`101`、`1`、`2`、`7`
- `multi_obj`：与 `multi` 相同；两个 `.o` 中都有同名的 `static helper`，
  用于验证局部符号不会冲突

`pp.c` 依赖 `/usr/include/cc.h` 和同目录的 `pp.h`，因此需要先由 rootfs 构建脚本
安装头文件并复制整个 `tests/` 目录。
