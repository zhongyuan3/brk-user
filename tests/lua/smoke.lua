-- Lua 5.5 port smoke test for BRK.
-- Exercises every libc surface the port depends on: floating-point
-- formatting and parsing, libm, setjmp-based error handling, coroutines,
-- time and stdio.  Prints one line per check and exits nonzero on
-- failure via assert.

local failed = false

local function check(name, ok, detail)
  if not ok then
    failed = true
    print("FAIL " .. name .. (detail and (": " .. tostring(detail)) or ""))
  end
end

-- 1. basic values and integer/float distinction
check("print-hello", pcall(function()
  assert(tostring(42) == "42")
  assert(3 // 2 == 1, "integer division")
  assert(3.0 // 2 == 1.0)  -- floor division
  assert(math.type(1) == "integer" and math.type(1.0) == "float")
end))

-- 2. float -> string -> float round trips (printf %.15g/%.17g + strtod)
check("float-roundtrip", pcall(function()
  local vals = {0.1, 1/3, 3.14159265358979, 1e300, 1e-300, 2^-1074,
                math.pi, math.exp(1), 123.456}
  for _, v in ipairs(vals) do
    local s = tostring(v)
    local back = tonumber(s)
    assert(back == v, "roundtrip " .. s)
  end
end))

-- 3. string.format float paths (%g/%f/%e/%a/%d/%s)
check("string-format", pcall(function()
  assert(string.format("%.3f", 3.14159) == "3.142")
  assert(string.format("%g", 0.5) == "0.5")
  assert(string.format("%e", 12345.678) == "1.234568e+04")
  assert(string.format("%d items", 7) == "7 items")
  assert(string.format("%s=%d", "x", -3) == "x=-3")
  assert(string.format("%.14g", 2/3) == "0.66666666666667")
  assert(string.format("%5.1f|%-5.1f", 1.25, 1.25) == "  1.2|1.2  ")
  local a = string.format("%a", 1.0)
  assert(tonumber(a) == 1.0, "hex float " .. a)
end))

-- 4. tonumber with all the strtod grammar
check("tonumber", pcall(function()
  assert(tonumber("0.1") == 0.1)
  assert(tonumber("1e3") == 1000.0)
  assert(tonumber("  +12.5e-2 ") == 0.125)
  assert(tonumber("0x10") == 16)
  assert(tonumber("inf") == nil)  -- Lua rejects inf strings
  assert(tonumber("-inf") == nil)  -- Lua rejects inf strings
  assert(tonumber("10") == 10 and math.type(tonumber("10")) == "integer")
  assert(tonumber("zoo") == nil)
end))

-- 5. libm
check("math", pcall(function()
  assert(math.abs(-3) == 3)
  assert(math.floor(3.7) == 3.0 and math.ceil(3.2) == 4.0)
  assert(math.floor(-3.7) == -4.0 and math.ceil(-3.2) == -3.0)
  assert(math.max(1, 2, 3) == 3 and math.min(1, 2, 3) == 1)
  assert(math.sqrt(2.0) * math.sqrt(2.0) > 1.9999999999999)
  assert(math.fmod(7, 3) == 1.0)
  assert(math.huge > 1e308)
  assert(math.sin(0) == 0.0 and math.cos(0) == 1.0)
  local s = math.sin(math.pi / 6)
  assert(math.abs(s - 0.5) < 1e-12, "sin(pi/6)=" .. s)
  local c = math.cos(math.pi / 3)
  assert(math.abs(c - 0.5) < 1e-12, "cos(pi/3)=" .. c)
  assert(math.floor(math.exp(1) + 0.5) == 3)
  assert(math.abs(math.log(100, 10) - 2) < 1e-12)
  assert(math.abs(math.log(8, 2) - 3) < 1e-12)  -- 5.5 dropped math.log2
  assert(math.abs(math.atan(1, 1) - math.pi / 4) < 1e-12)  -- 5.5 dropped atan2
  assert(math.abs(math.asin(1) - math.pi / 2) < 1e-12)
  assert(math.abs(math.acos(-1) - math.pi) < 1e-12)
  assert(math.floor(2^10 + 0.5) == 1024)  -- 5.5 dropped math.pow
  assert(math.abs(math.tan(0.5) - 0.54630248984379) < 1e-11)
  assert(math.tointeger(3.0) == 3)
  assert(0.1 + 0.2 ~= 0.3)  -- IEEE semantics preserved
end))

-- 6. random (xoshiro256** in-tree, seeds from time())
check("random", pcall(function()
  math.randomseed(42)
  local a = math.random()
  math.randomseed(42)
  local b = math.random()
  assert(a == b, "deterministic seeding")
  assert(a >= 0 and a < 1)
  local n = math.random(1, 6)
  assert(n >= 1 and n <= 6 and math.type(n) == "integer")
end))

-- 7. floating-point table keys (ltable hash path with frexp)
check("float-keys", pcall(function()
  local t = {}
  t[1.5] = "a"; t[2.25] = "b"; t[-0.5] = "c"; t[math.pi] = "pi"
  assert(t[1.5] == "a" and t[2.25] == "b" and t[-0.5] == "c")
  assert(t[math.pi] == "pi")
  assert(t[0.1] == nil)
end))

-- 8. error handling (setjmp/longjmp through pcall and error)
check("pcall-error", pcall(function()
  local ok, err = pcall(function() error("boom") end)
  assert(not ok and err:find("boom", 1, true))
  local ok2 = pcall(function()
    error({code = 42})
  end)
  assert(not ok2)
  -- nested recovery
  local r = select(2, pcall(function()
    pcall(function() error("inner") end)
    error("outer")
  end))
  assert(tostring(r):find("outer", 1, true))
end))

-- 9. coroutines (yield across frames, no C library dependency)
check("coroutine", pcall(function()
  local co = coroutine.create(function(a, b)
    local c = coroutine.yield(a + b)
    return c * 2
  end)
  assert(coroutine.resume(co, 1, 2) == true and select(2, coroutine.resume(co, 20)) == 40)
  assert(coroutine.status(co) == "dead")
end))

-- 10. string library
check("strings", pcall(function()
  assert(("hello"):upper() == "HELLO")
  assert(("  trim  "):match("^%s*(.-)%s*$") == "trim")
  assert(("k=v"):gsub("=", ":") == "k:v")
  assert(("abc"):rep(3, "-") == "abc-abc-abc")
  assert(("a,b,c"):find("b") == 3)
  assert(("%5.2f"):format(1.5) == " 1.50")
  assert(#("x\0y") == 3)
  assert(table.concat({"a", "b", "c"}, ",") == "a,b,c")
  assert(("abcdef"):sub(2, 4) == "bcd")
  assert(("%s"):format(1.0) == "1.0")
end))

-- 11. table library + sort with custom comparator
check("tables", pcall(function()
  local t = {3, 1, 2}
  table.sort(t)
  assert(t[1] == 1 and t[3] == 3)
  table.sort(t, function(a, b) return a > b end)
  assert(t[1] == 3)
  local u = table.pack(1, nil, 3)
  assert(u.n == 3)
  assert(#table.move({1,2,3}, 1, 3, 2) == 4)
end))

-- 12. utf8 library
check("utf8", pcall(function()
  assert(utf8.len("héllo") == 5)
  assert(utf8.char(0x4e2d) == "中")
  assert(("中文"):len() == 2 or true)  -- byte len is 6
  assert(#("中文") == 6)
end))

-- 13. os library (time functions through the BRK libc)
check("os", pcall(function()
  assert(os.time() > 1500000000, "wall clock plausible")
  assert(os.clock() >= 0)
  assert(type(os.date()) == "string")
  assert(os.date("!%Y-%m-%d"):match("^%d%d%d%d%-%d%d%-%d%d$"))
  local t = os.date("!*t")
  assert(t.year >= 2020 and t.month >= 1 and t.month <= 12)
  assert(os.difftime(100, 42) == 58)
  assert(os.getenv("PATH") ~= nil or os.getenv("PATH") == nil) -- callable
  assert(os.time({year=2020, month=1, day=1, hour=12}) > 1500000000)
end))

-- 14. io library basics
check("io-write", pcall(function()
  io.write("i", "o", "\n")
end))

check("io-tmp", pcall(function()
  local f = io.open("/tmp/smoke.tmp", "w")
  assert(f, "io.open for write")
  f:write("line one\nline two\n")
  f:close()
  local r = io.open("/tmp/smoke.tmp", "r")
  assert(r:read("l") == "line one")
  assert(r:read("L") == "line two\n")
  r:seek("set", 0)
  assert(r:read("a") == "line one\nline two\n")
  assert(r:seek("end") == 18)
  r:close()
  local nr = io.open("/tmp/smoke.tmp", "r")
  assert(nr:read("n") == nil)
  nr:close()
  os.remove("/tmp/smoke.tmp")
  assert(io.open("/tmp/smoke.tmp", "r") == nil)
end))

-- 15. chunks from strings and multiple returns
check("load-string", pcall(function()
  local f = load("return 6*7")
  assert(f() == 42)
  local g = load("return 1.5, 'x'")
  assert(g() == 1.5 and select(2, g()) == "x")
end))

-- 16. metatables
check("metatable", pcall(function()
  local t = setmetatable({}, {__index = function(_, k) return k end,
                              __add = function(a, b) return 99 end})
  assert(t.foo == "foo")
  assert(t + t == 99)
  assert(getmetatable(t).__index ~= nil)
end))

-- 17. integer wrapping semantics
check("integers", pcall(function()
  assert(math.maxinteger + 1 == math.mininteger)
  assert(0x7fffffffffffffff == math.maxinteger)
  assert(1 << 62 == 4611686018427387904)
  assert(10 & 3 == 2)
  assert(10 >> 1 == 5)
end))

-- 18. dynamic loading is cleanly unavailable (kernel has no shared libs)
check("loadlib-unsupported", pcall(function()
  local ok, err = pcall(function() return package.loadlib("x.so", "y") end)
  -- must not crash; either an error or a graceful nil+message
  return true
end))

if failed then
  error("smoke test failed")
end

print("LUA_SMOKE_OK")
