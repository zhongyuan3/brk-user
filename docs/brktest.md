# brktest

`brktest` is BRK's userspace test harness.  It is split into deterministic
functional suites and bounded stress suites.

## Usage

```sh
brktest functional [suite|suite.case]
brktest stress [suite|suite.case]
brktest all
```

`all` runs the functional suites followed by a short stress pass.  Legacy
suite names (`fork`, `kill`, `sig`, `meta`, `perm`, `group`, `auth`,
`symlink`, `posix`, `net`) are kept as aliases.

Useful options:

```text
--list                 list suites and cases
--seed N               random seed
--timeout MS           per-case timeout
--workers N            stress workers
--iterations N         stress iterations
--duration SEC         stress duration hint
--format human|tap     output format
--no-isolate           run cases without a child process
--allow-destructive    allow wall-clock changes
```

Examples:

```sh
brktest functional
brktest stress --iterations 20 --workers 4
brktest stress --iterations 100 --workers 8 --timeout 60000
brktest file.basic
brktest all --format tap
```

## Functional suites

| suite | contents |
|---|---|
| `process` | fork, wait, exit status and kill |
| `signal` | handlers, masks, pending signals, SIGWINCH |
| `fs` | metadata, hard links and symlinks |
| `creds` | uid/gid, permissions, setuid and auth databases |
| `posix` | POSIX syscall batch |
| `net` | TCP/UDP loopback sockets and inet helpers |
| `file` | file I/O, append, truncate, rename and directories |
| `io` | pipes, readv/writev, poll/select and SIGPIPE |
| `memory` | malloc, mmap, mprotect and copy-on-write |
| `time` | clocks, sleeps and wall-clock changes |
| `sys` | process identity, uname, rusage, times and `/proc` |
| `dev` | devtmpfs nodes and basic device access |

## Stress suites

| suite | load |
|---|---|
| `stress_process` | fork/wait and exec churn |
| `stress_fd` | open/dup/close and pipe churn |
| `stress_fs` | concurrent create/write/rename/unlink |
| `stress_io` | repeated pipe transfers with checksums |
| `stress_memory` | malloc/mmap allocation churn |
| `stress_net` | short-lived TCP connection churn |
| `stress_signal` | SIGTERM/kill/wait churn |
| `stress_mixed` | mixed fd, pipe and memory load |

Stress tests are bounded by `--workers` and `--iterations`.  The default
values are intentionally small so `brktest all` remains usable on QEMU.

## Safety

- Each case runs in a child process with a timeout.
- Cases that spawn descendants opt into a private process group, so the
  harness can kill the whole group on timeout.
- Privileged cases are skipped when not running as root.
- Tests tagged destructive are skipped unless `--allow-destructive` is used.
- Temporary files are created under `/tmp/brktest_<tag>_<pid>`.

## Output

Human format:

```text
[RUN ] file.basic
[ OK ] file.basic (80ms)

--- file: 1 passed, 0 failed, 0 skipped
=== file: ok ===
```

TAP format:

```text
TAP version 13
ok 1 - file.basic
1..1
# 1 suite(s), 0 failed
```

The exit status is non-zero if any case fails.
