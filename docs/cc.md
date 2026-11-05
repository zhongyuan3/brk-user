# cc — BRK 用户态轻量 C 编译器

`src/cmds/cc/` 是一个运行在 BRK/RISC-V64 用户态的小型 C 编译器。它不依赖目标机上的
`as`/`ld`，而是自己完成词法、预处理、语法、代码生成、链接和 ELF 写出。
编译器自身仍然使用本仓库的 `libuser.a`，但**生成出来的程序不链接 libuser**，
只依赖内核系统调用。

## 快速开始

构建用户程序（包含 `/bin/cc`）：

```sh
make -C brk-user
```

重新生成 rootfs（会把 `cc` 装到 `/bin/cc`，把 `src/include/cc.h` 装到
`/usr/include/cc.h`，并把 `tests/` 复制到 `/home/tests`）：

```sh
make -C brk rootfs CROSS_COMPILE=riscv64-unknown-elf-
make -C brk run CROSS_COMPILE=riscv64-unknown-elf-
```

在 BRK shell 中：

```sh
cc /home/tests/hello.c -o /tmp/hello
/tmp/hello

cc /home/tests/multi_a.c /home/tests/multi_b.c -o /tmp/multi
/tmp/multi
```

用法：

```text
cc [-c] [-E] [-o output] [-I dir] [-D name[=value]] [-U name] input.c|input.o [...]
```

- 不指定 `-o` 时输出 `a.out`
- 多个 `.c` 会在同一次调用中分别编译，然后链接成一个可执行文件
- `-c` 只编译，不链接，使用内置的 `CCO1` 对象格式
- `.o` 输入会和 `.c` 输入一起交给链接器
- 输出文件会自动设为 `0755`

## 预处理器

支持：

- 注释：`//`、`/* ... */`
- 行续接：反斜杠 + 换行
- `#include "file"`：先搜索源文件所在目录，再搜索 `-I` 路径
- `#include <file>`：搜索 `-I` 路径和默认路径
- 默认 include 路径：`.`、`/usr/include`、`/usr/local/include`
- 对象式宏：`#define NAME value`、`#undef NAME`
- 函数式宏：`#define F(a,b) ...`，参数会先展开再替换
- 可变参数宏：`#define F(...)`、`__VA_ARGS__`
- 条件编译：`#ifdef`、`#ifndef`、`#if`、`#elif`、`#else`、`#endif`
- `#if` 支持整数常量、`defined(NAME)`、`!`、算术/比较/位运算/`&&`/`||`
- `#error`、`#warning`
- `-D name[=value]`、`-U name`、`-E`

不支持：

- `#` 字符串化、`##` token 粘贴
- `#include` 后接宏名
- 跨物理行的宏调用（宏调用需要在同一个逻辑行内）
- 复杂的标准头文件（目标机只提供精简的 `src/include/cc.h`）

## 支持的 C 子集

类型：

- `void`、`char`、`short`、`int`、`long`
- `signed` / `unsigned`
- 指针、数组（含多维数组）、函数、函数指针
- `struct`、`union`、`enum`、`typedef`
- `char` 默认是 **unsigned**（RISC-V psABI），`signed char` 显式声明有符号

声明：

- 全局变量、局部变量、函数定义、函数原型
- `extern` 全局声明和 `static` 文件内符号
- 一条声明中的多个声明符，例如 `int a, *p, b[4];`
- `for (int i = 0; ...)` 中的声明
- 对后定义函数的调用（编译器会建立隐式函数符号）
- 函数指针，例如 `int (*fp)(int, int);`
- 函数返回函数指针，例如 `int (*pick(int))(int, int);`
- 函数指针数组、结构体中的函数指针成员

语句：

- 块、`if/else`、`while`、`do-while`、`for`
- `switch` / `case` / `default`，支持 fallthrough 和嵌套 switch
- `goto` 与标签
- `return`、`break`、`continue`
- 表达式语句、空语句

表达式：

- 算术：`+ - * / %`
- 位运算：`& | ^ << >> ~`
- 比较：`== != < <= > >=`
- 逻辑：`&& || !`
- 赋值、复合赋值：`= += -= *= /= %= &= |= ^= <<= >>=`
- 自增自减：`++ --`（前缀/后缀）
- 三目、逗号、强制类型转换、`sizeof`
- 取地址 `&`、解引用 `*`、数组下标
- 成员访问 `.` 和 `->`
- 直接调用和通过函数指针的间接调用（最多 8 个参数）
- 可变参数函数：`...`、`va_list`、`va_start`、`va_arg`、`va_end`、`va_copy`

初始化器：

- 全局标量：常量表达式
- 全局 `char[]`：字符串字面量
- 全局数组：`{ ... }` 常量列表（可自动推断 `[]` 长度）
- 全局 `struct` / `union`：`{ ... }` 常量列表，支持嵌套
- 全局多维数组、结构体数组
- 全局指针：字符串字面量、`0`、`&global`、数组/函数地址
- 全局函数指针：函数地址
- 局部标量：任意表达式
- 局部数组/结构体/联合体：
  - `{ ... }` 位置初始化
  - 嵌套 `{ ... }`
  - `char[]` 字符串初始化
  - 未显式初始化的剩余部分自动清零
- 指定初始化器：`.field = ...`、`[index] = ...`（单层）
- brace elision：如 `int a[2][2] = {1,2,3,4};`
- 结构体拷贝赋值：`a = b`

不支持：

- 链式 designator（如 `.a.b = ...`、`[i].x = ...`）
- 指定初始化器与复杂 brace elision 混用的所有边界情况
- 全局聚合拷贝初始化（如文件作用域 `struct P p = other;`）
- 位域、柔性数组

内建函数（如果用户没有自己定义同名函数）：

```c
int write(int fd, const void *buf, long n);
int read(int fd, void *buf, long n);
int open(const char *path, int flags, int mode);
int close(int fd);
void exit(int status);
int putchar(int c);
int puts(const char *s);
```

`src/include/cc.h` 提供了这些原型以及 `NULL`、`EOF`、`O_*` 常量。

## 多文件与链接

同一次调用中的多个 `.c` 会分别作为独立 translation unit 编译，然后由内置的
简单链接器合并：

```sh
# 一次编译并链接
cc a.c b.c -o prog

# 分段编译
cc -c a.c -o a.o
cc -c b.c -o b.o
cc a.o b.o -o prog

# 混合源文件和对象文件
cc a.c b.o -o prog
```

对象文件是编译器私有的 `CCO1` 格式，不是标准 ELF `.o`；它只用于本编译器的
链接阶段。

链接规则：

- 所有 `main` 之外的外部符号按名字解析
- `extern` 声明只产生未定义符号，由其他单元的定义解析
- `static` 函数/全局变量会被加上单元后缀，保持文件内链接
- 每个单元可以有自己的 typedef、tag、枚举常量和宏
- 运行时符号 `__cc_puts`、`__cc_memcpy`、`__cc_memset` 由链接器统一注入
- 字符串字面量和 `static` 符号是单元内局部符号，不会跨对象按名字解析
- 重复的外部符号定义、未解析的引用都会在链接阶段报错
- 多个输入文件顺序不影响结果

暂不支持：

- 标准 ELF `.o` / 系统链接器互操作
- 静态库/动态库
- 链接脚本/自定义段

## 生成物 ABI

生成程序是：

- `ELF64`、`ET_EXEC`、little-endian、`e_machine = EM_RISCV`
- 单个 `PT_LOAD`，权限 `R|W|X`
- 固定加载基址 `0x10000`，代码从文件偏移 `0x1000` 开始
- 入口是编译器生成的 `__cc_start`
- `__cc_start` 按内核约定接收 `a0=argc, a1=argv, a2=envp`，调用 `main` 后用
  `SYS_exit(60)` 退出
- 系统调用号使用 BRK 的通用编号：`read=0, write=1, open=2, close=3, exit=60`

`puts`、结构体拷贝和聚合初始化清零分别使用 `__cc_puts`、`__cc_memcpy`、
`__cc_memset`。

## 明确不支持的功能

这些限制是为了保持实现足够小：

- `float` / `double`
- 按值传递/返回 `struct` / `union`
- 位域、柔性数组
- 可变参数中的 `double`、`struct` 按值参数
- 宏的 `#` 字符串化、`##` token 粘贴
- 链式 designator、复杂 brace elision 混用
- 全局聚合拷贝初始化
- `static` 局部变量（会报错）
- 标准 ELF `.o`、静态库/动态库

另外，内核当前不保存浮点上下文，因此生成代码只使用整数指令。

## 实现结构

`src/cmds/cc/` 按阶段划分：

- `cc.h`：模块间共享的类型、符号表和函数声明
- `util.c`：内存分配、错误处理和字节缓冲区
- `lexer.c`：整数/字符/字符串字面量、标识符、运算符、注释、转义
- `preprocess.c`：宏表、include 搜索、条件编译、`#if` 表达式求值
- `types.c`：`struct type`、`struct node`、全局/局部符号和作用域
- `emit.c`：`enc_r/enc_i/enc_s/enc_b/enc_u/enc_j`、伪指令、标签和重定位
- `parser.c`：声明、初始化器、语句和表达式解析
- `codegen.c`：栈式求值，表达式结果放在 `a0`，lvalue 地址放在 `a0`
- `link.c`：合并多个 unit 的 text/data/bss，解析符号并回填重定位
- `main.c`：命令行驱动和 ELF 输出入口

为了避免 RISC-V `B-type` 分支只有 ±4KB 的问题，条件跳转统一生成为：

```asm
b<inverse> +8
jal x0, target
```

函数调用使用 `jal ra, target`；通过函数指针调用使用 `jalr ra, 0(t2)`。
全局变量/字符串地址用 `lui + addi` 的 HI20/LO12 重定位，链接阶段统一回填。

## 开发与测试

只构建宿主机版本（用于快速检查生成的 ELF）：

```sh
make -C brk-user cc-host
./brk-user/build/cc-host -I brk-user/src/include \
    -o /tmp/hello.elf brk-user/tests/hello.c
riscv64-unknown-elf-readelf -h -l /tmp/hello.elf
```

`tests/` 下是已经验证过的示例：

| 文件 | 覆盖内容 | 预期输出 |
|---|---|---|
| `hello.c` | 基本输出 | `hello from cc` |
| `smoke.c` | 全局数组、循环、递归、`putnum` | `sum=` 和 `30` |
| `expr.c` | 指针、复合赋值、`++/--`、`sizeof`、`argc` | 多行数字 |
| `io.c` | `open/read/write/close`、转换、位运算 | 多行数字、`file-io` |
| `long.c` | 64 位常量与 `long/unsigned long` | `1118` |
| `large.c` | 大栈帧、超过 12 位的局部偏移 | `999` 和 `512` |
| `pp.c` | 宏、条件编译、`#include` | `pp-ok`、`43`、`1`、`45` |
| `struct.c` | struct/union/enum/typedef、成员访问、结构体拷贝 | 多行数字、`DCBA` |
| `funcptr.c` | 函数指针、函数指针数组、返回函数指针 | 多行数字 |
| `init.c` | 局部数组/结构体初始化、嵌套初始化、字符串初始化 | 多行数字 |
| `ginit.c` | 全局结构体/数组/指针/函数指针初始化 | 多行数字 |
| `designated.c` | 指定初始化器、brace elision、嵌套 struct 初始化 | 多行数字 |
| `macros.c` | 函数式宏、可变参数宏、嵌套展开 | 多行数字 |
| `control.c` | `switch`/`case`/`default`/fallthrough/`goto` | 多行数字 |
| `varargs.c` | `va_list`/`va_start`/`va_arg`/`va_copy`、variadic 函数指针 | 多行数字 |
| `multi_a.c` + `multi_b.c` | 多文件链接、`extern`、`static` 内部链接 | `multi`、`5`、`101`、`1`、`2`、`7` |
| `multi_a.c` + `multi_b.c`（`-c`） | 分段编译、对象文件、局部符号 | 同上 |

修改编译器后，建议至少跑一遍 `tests/expr.c`、`tests/struct.c`、
`tests/funcptr.c`、`tests/init.c` 和 `tests/multi_a.c multi_b.c`。
