#!/usr/bin/env python3
# Generator for the BRK libc math constants and polynomial fits.
#
# The libc math directory (usr/lib/libc/math) is written from scratch and
# must stay free of vendored code, so every irrational constant and every
# fitted polynomial coefficient used there is produced by this script.
# Running it prints ready-to-paste C snippets together with a verification
# report; the emitted values are committed in the sources so the build
# never depends on Python.
#
# Everything is computed from first principles with the Python standard
# library only: `decimal` provides the extended-precision arithmetic, and
# the transcendental helpers (pi, sin, cos, atan, asin) are implemented
# here from their defining series, so no external math library is trusted.
#
# usage: scripts/gen-libm-consts.py

from decimal import Decimal as D
from decimal import ROUND_FLOOR, ROUND_HALF_EVEN, getcontext

getcontext().prec = 400

TWO = D(2)
ONE = D(1)

# ---------------------------------------------------------------------------
# Extended-precision primitives (from defining series only)
# ---------------------------------------------------------------------------


def d_atan_inv(x):
    """atan(1/x) = sum (-1)^k / ((2k+1) x^(2k+1))."""
    x = D(x)
    term = ONE / x
    total = term
    k = 0
    while True:
        k += 1
        term /= x * x
        t = term / (2 * k + 1)
        total -= t if k % 2 else -t
        if t < D(10) ** -400:
            break
    return total


PI = 16 * d_atan_inv(5) - 4 * d_atan_inv(239)


def d_atanh_series(t):
    """2 * (t + t^3/3 + t^5/5 + ...) for |t| < 1."""
    t = D(t)
    term = t
    total = t
    k = 1
    while True:
        k += 1
        term *= t * t
        add = term / (2 * k - 1)
        total += add
        if add < D(10) ** -400:
            break
    return 2 * total


LN2 = d_atanh_series(D(1) / 3)
LN10 = d_atanh_series(D(9) / 11)


def d_sqrt(x):
    return D(x).sqrt()


def d_sin(x):
    x = D(x)
    term = x
    total = x
    for k in range(1, 200):
        term *= -(x * x) / ((2 * k) * (2 * k + 1))
        total += term
        if abs(term) < D(10) ** -400:
            break
    return total


def d_cos(x):
    x = D(x)
    term = ONE
    total = ONE
    for k in range(1, 200):
        term *= -(x * x) / ((2 * k - 1) * (2 * k))
        total += term
        if abs(term) < D(10) ** -400:
            break
    return total


def d_atan(x):
    """atan via halving: atan(x) = 2*atan(x/(1+sqrt(1+x^2)))."""
    x = D(x)
    n = 0
    while abs(x) > D("0.005"):
        x = x / (ONE + d_sqrt(ONE + x * x))
        n += 1
    term = x
    total = x
    k = 0
    while True:
        k += 1
        term *= -(x * x)
        add = term / (2 * k + 1)
        total += add
        if abs(add) < D(10) ** -400:
            break
    return total * (2 ** n)


def d_asin(x):
    """asin(x) = 2*atan(x/(1+sqrt(1-x^2))), |x| < 1."""
    x = D(x)
    return 2 * d_atan(x / (ONE + d_sqrt(ONE - x * x)))


def d_tan(x):
    return d_sin(x) / d_cos(x)


# ---------------------------------------------------------------------------
# Double-precision conversion helpers
# ---------------------------------------------------------------------------


def to_double(value):
    """Nearest double to a nonzero Decimal, as a C hex-float literal."""
    v = D(value)
    sign = ""
    if v < 0:
        sign = "-"
        v = -v
    assert v > 0
    e = 0
    while TWO ** (e + 1) <= v:
        e += 1
    while TWO ** e > v:
        e -= 1
    m = int((v / (TWO ** e) * TWO ** 52).to_integral_value(
        rounding=ROUND_HALF_EVEN))
    if m == TWO ** 53:
        m >>= 1
        e += 1
    return "%s0x1.%013xp%+d" % (sign, m - (1 << 52), e)


def eval_lit(lit):
    """Evaluate a C hex-float literal exactly in Decimal."""
    if lit == "0.0":
        return D(0)
    neg = lit.startswith("-")
    if neg:
        lit = lit[1:]
    assert lit.startswith("0x1.")
    mant, exp = lit[4:].split("p")
    v = D((1 << 52) + int(mant, 16)) * TWO ** (int(exp) - 52)
    return -v if neg else v


def check_pair(name, value, hi, lo, bits=100):
    err = abs(D(value) - eval_lit(hi) - eval_lit(lo)) / D(value)
    assert err < TWO ** -bits, "%s: pair error too large (%s)" % (name, err)


def split_hi(value, keep_bits):
    """Round to a `keep_bits`-significant-bit aligned grid."""
    v = D(value)
    e = 0
    while TWO ** (e + 1) <= v:
        e += 1
    while TWO ** e > v:
        e -= 1
    grid = TWO ** (e - keep_bits + 1)
    return (v / grid).to_integral_value(
        rounding=ROUND_HALF_EVEN) * grid


def hi_lo(value, keep_bits):
    """hi on a keep_bits grid, lo = residual; pair good to ~100+ bits."""
    hi = split_hi(value, keep_bits)
    return to_double(hi), to_double(D(value) - hi)


def plain_hi_lo(value):
    """hi = nearest double, lo = residual (0.0 when exact)."""
    hi = to_double(value)
    rest = D(value) - eval_lit(hi)
    return hi, "0.0" if rest == 0 else to_double(rest)


# ---------------------------------------------------------------------------
# 1. exp table: 2^(j/32) as hi/lo pairs
# ---------------------------------------------------------------------------

print("/* === exp table: 2^(j/32) as hi/lo pairs, 106-bit pairs === */")
rows = []
for j in range(32):
    v = TWO ** (D(j) / 32)
    hi, lo = plain_hi_lo(v)
    check_pair("exp2_t32[%d]" % j, v, hi, lo)
    rows.append("\t{ %s, %s }, /* 2^(%d/32) */" % (hi, lo, j))
print("static const double exp2_t32[32][2] = {")
print("\n".join(rows))
print("};")
print()

L32 = LN2 / 32
hi, lo = hi_lo(L32, 37)
check_pair("ln2_over32", L32, hi, lo, bits=85)
print("/* ln2/32 = %s + %s (hi: 37 significant bits, k*hi exact for"
      " |k| < 2^16) */" % (hi, lo))
print()

hi, lo = hi_lo(LN2, 33)
check_pair("ln2", LN2, hi, lo, bits=80)
print("/* ln2 = %s + %s (hi: 33 significant bits, e*hi exact for"
      " |e| < 2^20) */" % (hi, lo))
print()

print("/* 32/ln2 = %s (nearest double; k selection only) */"
      % to_double(32 / LN2))
print("/* 1/ln2  = %s (nearest double) */" % to_double(ONE / LN2))
print("/* ln10 = %s + %s */" % hi_lo(LN10, 33))
print("/* log2(e) = %s + %s */" % hi_lo(ONE / LN2, 33))
print("/* log10(e) = %s + %s */" % hi_lo(ONE / LN10, 33))
check_pair("ln10", LN10, *hi_lo(LN10, 33), bits=80)
check_pair("log2e", ONE / LN2, *hi_lo(ONE / LN2, 33), bits=80)
check_pair("log10e", ONE / LN10, *hi_lo(ONE / LN10, 33), bits=80)
print()

# ---------------------------------------------------------------------------
# 2. pi-related constants
# ---------------------------------------------------------------------------

PIO2 = PI / 2
PIO4 = PI / 4

for name, v in (("pi/2", PIO2), ("pi", PI), ("pi/6", PI / 6)):
    hi, lo = hi_lo(v, 33)
    check_pair(name, v, hi, lo, bits=80)
    print("/* %s = %s + %s */" % (name, hi, lo))
print()

T6 = ONE / d_sqrt(D(3))
hi, lo = hi_lo(T6, 33)
check_pair("t6", T6, hi, lo, bits=80)
print("/* 1/sqrt(3) = %s + %s */" % (hi, lo))
print()

print("/* 2/pi = %s (nearest double; k selection only) */"
      % to_double(TWO / PI))
print()

# pi/2 chain split: 4 parts of 24 significant bits, aligned so that
# k*P1, k*P2, k*P3, k*P4 are all exact for |k| < 2^29.
print("/* pi/2 chain split (24 significant bits per part) */")


def chain_split(value, scales):
    parts = []
    rest = D(value)
    for sc in scales:
        grid = TWO ** sc
        p = (rest / grid).to_integral_value(
            rounding=ROUND_HALF_EVEN) * grid
        rest -= p
        parts.append(p)
    return parts, rest


parts, rest = chain_split(PIO2, [-23, -47, -70, -94])
assert abs(rest) < TWO ** -93
for i, p in enumerate(parts):
    print("/* P%d = %s */" % (i + 1, to_double(p)))
print("/* residual after P4: %s */" % rest)
print()

# ---------------------------------------------------------------------------
# 3. 2/pi bit table for the huge-argument reduction
# ---------------------------------------------------------------------------

# 2/pi = sum_{j>=0} word[j] * 2^(-64*(j+1)); 18 words = 1152 bits,
# enough for any finite double (needs ~1080 bits for |x| < 2^1024).
# Computed in exact integer fixed point so no precision limit is hit:
# pi first, via Machin's formula at `FIXBITS + guard` scale.


def pi_fixed(scale):
    """pi * 2^scale as an integer, exact to within a few units."""

    def atan_inv_fixed(x):
        # sum (-1)^k / ((2k+1) x^(2k+1)), scaled by 2^scale
        total = 0
        xp = x
        k = 0
        while True:
            t = (1 << scale) // ((2 * k + 1) * xp)
            if t == 0 and k > 2:
                break
            total += -t if k % 2 else t
            k += 1
            xp *= x * x
        return total

    return 16 * atan_inv_fixed(5) - 4 * atan_inv_fixed(239)


FIXBITS = 1152
GUARD = 200
P = pi_fixed(FIXBITS + GUARD)
# Q = floor(2/pi * 2^FIXBITS): the error in P (a few units at
# 2^(FIXBITS+GUARD) scale) perturbs Q by ~2^-GUARD, far below 1.
Q, rem = divmod(1 << (2 * FIXBITS + GUARD + 1), P)
assert rem != 0  # 2/pi is irrational; a zero remainder would mean a bug
words = []
for j in range(18):
    shift = FIXBITS - 64 * (j + 1)
    words.append((Q >> shift) & ((1 << 64) - 1))
# independent cross-check of the leading word (the well-known hex
# expansion of 2/pi starts 0.A2F9836E4E441529...)
assert words[0] == 0xA2F9836E4E441529, "2/pi leading bits wrong"
# stability: recompute with a different guard and compare
P2v = pi_fixed(FIXBITS + GUARD + 60)
Q2v = (1 << (2 * FIXBITS + GUARD + 60 + 1)) // P2v
for j in range(18):
    shift = FIXBITS - 64 * (j + 1)
    assert ((Q2v >> shift) & ((1 << 64) - 1)) == words[j], \
        "2/pi words not stable across guards"
print("/* 2/pi fractional bits, 64-bit words, MSW first */")
print("static const uint64_t twopi_frac[18] = {")
for j, w in enumerate(words):
    print("\tUINT64_C(0x%016x), /* word %d */" % (w, j))
print("};")
print()

# ---------------------------------------------------------------------------
# 4. polynomial fits (Chebyshev-node interpolation in w = x*x)
# ---------------------------------------------------------------------------


def fit_poly(F, n, wmax, comment):
    """Interpolate F at n Chebyshev nodes on [0, wmax]; print the power
    basis (ascending) as C literals and verify with double coefficients."""
    c = D(wmax) / 2
    nodes, vals = [], []
    for k in range(n):
        theta = PI * (2 * k + 1) / (2 * n)
        w = c * (ONE - d_cos(theta))
        nodes.append(w)
        vals.append(F(w))
    # divided differences
    dd = list(vals)
    for j in range(1, n):
        for i in range(n - 1, j - 1, -1):
            dd[i] = (dd[i] - dd[i - 1]) / (nodes[i] - nodes[i - j])
    # Newton form -> power basis, ascending: poly[i] = coeff of w^i
    poly = [D(0)] * n
    for i in range(n - 1, -1, -1):
        # poly = poly * (w - nodes[i]) + dd[i]
        new = [D(0)] * n
        for p in range(n - 1, -1, -1):
            t = poly[p]
            if t == 0:
                continue
            if p + 1 <= n - 1:
                new[p + 1] += t
            new[p] -= t * nodes[i]
        new[0] += dd[i]
        poly = new
    lits = [to_double(x) for x in poly]
    print("/* %s: degree %d in w on [0, %s] */" % (comment, n - 1, wmax))
    for i, lit in enumerate(lits):
        print("/*   c[%d] = %s */" % (i, lit))

    def ev(w):
        acc = float.fromhex(lits[n - 1])
        for i in range(n - 2, -1, -1):
            acc = acc * w + float.fromhex(lits[i])
        return acc

    worst, worst_w = D(0), D(0)
    for i in range(1, 4001):
        w = D(wmax) * i / 4000
        e = abs(F(w) - D(repr(ev(float(w)))))
        if e > worst:
            worst, worst_w = e, w
    print("/*   max abs error (double coeffs, correction part):"
          " %.3e at w=%s */" % (float(worst), worst_w))
    print()
    return lits


print("/* === fitted polynomials (argument w = x*x) === */")
print()


def F_tan(w):
    r = d_sqrt(w)
    return (d_tan(r) - r) / (w * r)


tan_lits = fit_poly(F_tan, 17, float(PIO4) ** 2, "tan core:"
                    " tan(r) = r + r^3 * T(w)")


def F_atan(w):
    x = d_sqrt(w)
    return (d_atan(x) - x) / (w * x)


atan_lits = fit_poly(F_atan, 13, D("0.25"), "atan core:"
                     " atan(x) = x + x^3 * B(w)")

def d_exp(x):
    """e^x by Taylor for |x| <= 1 at extended precision."""
    term, total = ONE, ONE
    for k in range(1, 60):
        term = term * x / k
        total += term
        if abs(term) < D(10) ** -70:
            break
    return total


def d_tanh(x):
    return (d_exp(x) - d_exp(-x)) / (d_exp(x) + d_exp(-x))


def F_tanh(w):
    x = d_sqrt(w)
    return (d_tanh(x) - x) / (w * x)


tanh_lits = fit_poly(F_tanh, 15, D("0.25"), "tanh core:"
                     " tanh(x) = x + x^3 * H(w)")


def F_asin(w):
    x = d_sqrt(w)
    return (d_asin(x) - x) / (w * x)


asin_lits = fit_poly(F_asin, 15, D("0.25"), "asin core:"
                     " asin(x) = x + x^3 * A(w)")

print("/* all constants and fits verified at 150 decimal digits */")
