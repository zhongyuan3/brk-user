# Vendored Lua 5.5.1

Upstream: https://www.lua.org (lua-5.5.1.tar.gz), MIT license; the
copyright headers inside each file carry the full notice.

The sources are unmodified. The build compiles everything except
`luac.c` (the standalone bytecode compiler frontend, which defines its
own `main`) and links the rest statically against libuser.a, producing
`build/bin/lua`, installed as `/bin/lua` in the rootfs.

In-system smoke test: `tests/lua/smoke.lua`.
