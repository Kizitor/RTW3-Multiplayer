// Software versions of the x87 transcendental instructions (FSIN, FCOS, FSINCOS, FPTAN, FPATAN, FYL2X, F2XM1) as the
// game's eleven RTL routines use them (x87.cpp). Intel and AMD CPUs don't round those instructions identically; these
// functions use only IEEE double operations (+ - * / sqrt, SSE2) and integer operations, so every x86 CPU gives the
// same bits. Header-only, so that x87.cpp's hooks inline them (the globe's projections call SinCos millions of times a
// battle minute). They follow the x87's own results (rounded to a double) to about one unit in the last place:
// - Sin, Cos, SinCos, Tan reduce the argument as the x87 does: by multiples of pi/2 with its 66-bit pi
//   (pi66 = 0x3243F6A8885A308D3 * 2^-64, 4.0e-21 below pi; checked against the hardware). r = |x| - n*pi66/2 is exact,
//   a double-double: below 2^20 a two-part Cody-Waite step whose products and differences are all exact, up to 2^63
//   32-bit limb integer arithmetic. Then fdlibm's kernels with the tail (k_sin.c, k_cos.c, k_tan.c). So sin(pi) is
//   1.2246063538223773e-16 and cos(pi/2) 6.1230317691118863e-17 as on the x87 (the true values: 1.2246467991473532e-16,
//   6.123233995736766e-17). |x| >= 2^63, infinities and NaNs give the routines' own results (the argument unchanged,
//   the indefinite NaN).
// - Atan2: fdlibm's e_atan2.c with the quotient's exact remainder (Dekker's product) as a first-order correction and
//   the quadrant's pi added in two parts; the special cases and NaN rules as FPATAN.
// - Exp: the game's routine (fldl2e; fmul; frndint; f2xm1; fld1; fadd; fscale) emulated under the x87 precision
//   control: x * log2(e) (FLDL2E's 64-bit constant) and 1 + (2^f - 1) rounded to 24, 53 or 64 bits as the x87 rounds
//   them, the rest exact; 2^f - 1 in double-double (a table of 2^(j/32) and a polynomial), rounded to 64 bits as
//   F2XM1's result.
// - Ln, ArcTan: fdlibm (e_log.c, s_atan.c).
#pragma once
//
// Derived from fdlibm 5.3 (k_sin.c, k_cos.c, k_tan.c, s_atan.c, e_atan2.c, e_log.c):
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
// k_tan.c:
// ====================================================
// Copyright 2004 Sun Microsystems, Inc.  All Rights Reserved.
//
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
// Changes: C++, the bit access through memcpy; the x87's argument reduction (above) instead of e_rem_pio2.c's; the
// correction terms in atan2; Exp as above instead of e_exp.c; NaN arguments pass through as on the x87.
//
// Determinism: every floating-point operation is an IEEE double operation in SSE2 registers (this DLL is built for SSE2;
// the x87 precision control has no effect on them; the last operation of a result is an explicit SSE2 instruction, as
// 32-bit MSVC would otherwise sometimes do it on the x87, which returns the double) with round to nearest (x87.cpp's
// callers make sure of MXCSR). The double-double steps (TwoSum, Dekker's product) need the operations in the written
// order: /fp:precise, and 32-bit x86 has no FMA to contract into. Integer operations: 32-bit, and 32x32->64-bit
// products (__emulu). No library calls.
#include <emmintrin.h>
#include <intrin.h>
#include <stdint.h>
#include <string.h>

namespace softmath {
namespace detail {

inline uint64_t Bits(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}
inline double FromBits(uint64_t u) {
    double d;
    memcpy(&d, &u, 8);
    return d;
}
inline int32_t Hi(double d) { return (int32_t)(uint32_t)(Bits(d) >> 32); }
inline uint32_t Lo(double d) { return (uint32_t)Bits(d); }
inline double WithHi(double d, int32_t hi) { return FromBits(((uint64_t)(uint32_t)hi << 32) | Lo(d)); }
inline double ClearLo(double d) { return FromBits(Bits(d) & 0xffffffff00000000ull); }
inline double Abs(double d) { return FromBits(Bits(d) & 0x7fffffffffffffffull); }
inline double Sqrt(double d) { return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(d), _mm_set_sd(d))); }
inline bool IsNan(double d) { return (Bits(d) << 1) > 0xffe0000000000000ull; }
inline double Nan() { return FromBits(0xfff8000000000000ull); }  // the x87 "indefinite" (masked invalid operation)
inline double Inf(bool neg) { return FromBits(neg ? 0xfff0000000000000ull : 0x7ff0000000000000ull); }
inline double Pow2(int e) { return FromBits((uint64_t)(uint32_t)(e + 1023) << 52); }  // 2^e, -1022 <= e <= 1023
inline double U32ToD(uint32_t v) { return (double)(int32_t)(v ^ 0x80000000u) + 2147483648.0; }
// A function's last operation as an SSE2 instruction: 32-bit MSVC returns doubles in ST0 and would otherwise sometimes
// do it with the x87 (rounded to the x87's precision control).
inline double Add(double a, double b) { return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(a), _mm_set_sd(b))); }
inline double Sub(double a, double b) { return _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(a), _mm_set_sd(b))); }
inline double Mul(double a, double b) { return _mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(a), _mm_set_sd(b))); }
inline double Div(double a, double b) { return _mm_cvtsd_f64(_mm_div_sd(_mm_set_sd(a), _mm_set_sd(b))); }

// s + e = a + b exactly (Knuth's TwoSum)
inline void TwoSum(double a, double b, double& s, double& e) {
    double t = a + b, bb = t - a;
    e = (a - (t - bb)) + (b - bb);
    s = t;
}
// s + e = a + b exactly if |a| >= |b| or a == 0 (Dekker's Fast2Sum)
inline void FastTwoSum(double a, double b, double& s, double& e) {
    double t = a + b;
    e = b - (t - a);
    s = t;
}
// p + e = a * b exactly (Dekker's product with Veltkamp's split; |a|, |b| < 2^995, no underflow)
inline void TwoProd(double a, double b, double& p, double& e) {
    double ca = 134217729.0 * a, cb = 134217729.0 * b;
    double ah = ca - (ca - a), al = a - ah, bh = cb - (cb - b), bl = b - bh;
    double t = a * b;
    e = ((ah * bh - t) + ah * bl + al * bh) + al * bl;
    p = t;
}
// v rounded to a multiple of 2^u, ties to even (|v| < 2^(u+51))
inline double RoundTo(double v, int u) {
    double c = 1.5 * Pow2(u + 52);
    return (v + c) - c;
}

// k_sin.c, k_cos.c: |x| <= pi/4, y the tail of x
const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
             S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
             C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;

inline double KSin(double x, double y, int iy) {
    int32_t ix = Hi(x) & 0x7fffffff;
    if (ix < 0x3e400000 && (int)x == 0) return x;  // |x| < 2^-27
    double z = x * x, v = z * x;
    double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) return Add(x, v * (S1 + z * r));
    return Sub(x, (z * (0.5 * y - v * r) - y) - v * S1);
}

inline double KCos(double x, double y) {
    int32_t ix = Hi(x) & 0x7fffffff;
    if (ix < 0x3e400000 && (int)x == 0) return 1.0;  // |x| < 2^-27
    double z = x * x;
    double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3fd33333) return Sub(1.0, 0.5 * z - (z * r - x * y));  // |x| < 0.3
    double qx = ix > 0x3fe90000 ? 0.28125 : FromBits((uint64_t)(uint32_t)(ix - 0x00200000) << 32);  // x/4
    double hz = 0.5 * z - qx, a = 1.0 - qx;
    return Sub(a, hz - (z * r - x * y));
}

// k_tan.c: tan(x + y) (iy = 1) or -1/tan(x + y) (iy = -1); |x| <= ~pi/4, y the tail of x
const double TT[] = {3.33333333333334091986e-01,  1.33333333333201242699e-01, 5.39682539762260521377e-02,
                    2.18694882948595424599e-02,  8.86323982359930005737e-03, 3.59207910759131235356e-03,
                    1.45620945432529025516e-03,  5.88041240820264096874e-04, 2.46463134818469906812e-04,
                    7.81794442939557092300e-05,  7.14072491382608190305e-05, -1.85586374855275456654e-05,
                    2.59073051863633712884e-05};
const double pio4 = 7.85398163397448278999e-01, pio4lo = 3.06161699786838301793e-17;

inline double KTan(double x, double y, int iy) {
    int32_t hx = Hi(x), ix = hx & 0x7fffffff;
    double z, r, v, w, s, a, t;
    if (ix < 0x3e300000 && (int)x == 0) {  // |x| < 2^-28
        if (((ix | (int32_t)Lo(x)) | (iy + 1)) == 0) return Div(1.0, Abs(x));
        if (iy == 1) return x;
        z = w = x + y;  // -1 / (x + y), carefully
        z = ClearLo(z);
        v = y - (z - x);
        t = a = -1.0 / w;
        t = ClearLo(t);
        s = 1.0 + t * z;
        return Add(t, a * (s + t * v));
    }
    if (ix >= 0x3fe59428) {  // |x| >= 0.6744
        if (hx < 0) {
            x = -x;
            y = -y;
        }
        z = pio4 - x;
        w = pio4lo - y;
        x = z + w;
        y = 0.0;
    }
    z = x * x;
    w = z * z;
    r = TT[1] + w * (TT[3] + w * (TT[5] + w * (TT[7] + w * (TT[9] + w * TT[11]))));
    v = z * (TT[2] + w * (TT[4] + w * (TT[6] + w * (TT[8] + w * (TT[10] + w * TT[12])))));
    s = z * x;
    r = y + z * (s * (r + v) + y);
    r += TT[0] * s;
    w = Add(x, r);
    if (ix >= 0x3fe59428) {
        v = (double)iy;
        return Mul((double)(1 - ((hx >> 30) & 2)), v - 2.0 * (x - (w * w / (w + v) - r)));
    }
    if (iy == 1) return w;
    z = ClearLo(w);  // -1 / (x + r), carefully
    v = r - (z - x);
    t = a = -1.0 / w;
    t = ClearLo(t);
    s = 1.0 + t * z;
    return Add(t, a * (s + t * v));
}

// The x87's argument reduction: pi66/2 = 0x3243F6A8885A308D3 * 2^-65 = P1 + P2 (33 + 32 significant bits).
const double invpio2 = 6.36619772367581382433e-01, P1 = 0x1.921fb544p0, P2 = 0x1.0b4611a6p-34;
const uint32_t M0 = 0x85a308d3u, M1 = 0x243f6a88u;  // pi66/2 * 2^65 = 3 * 2^64 + M1 * 2^32 + M0
const double INVM = 0x1.45f306dc9c883p-66;          // 1 / (pi66/2 * 2^65)

// a = n * pi66/2 + r for pi/4 < a < 2^63: returns n mod 4; r = rh + rl exactly, |r| <= ~pi/4.
inline int RemPio2(double a, double& rh, double& rl) {
    if (Hi(a) < 0x41300000) {  // a < 2^20: n < 2^20, so n*P1, n*P2 and a - n*P1 (Sterbenz) are exact
        int n = (int)(a * invpio2 + 0.5);
        double fn = (double)n;
        TwoSum(a - fn * P1, -(fn * P2), rh, rl);
        return n & 3;
    }
    // In units of 2^-65: a = m << s (m the 53-bit significand), pi66/2 = M (66 bits). R = a - n*M in 128-bit two's
    // complement (32-bit limbs), with n first estimated in double (off by up to ~2^11), then corrected by k.
    uint64_t b = Bits(a);
    int s = (int)(b >> 52) - 1010, sh = s & 31;  // s = 33..75
    uint32_t mh = ((uint32_t)(b >> 32) & 0xfffffu) | 0x100000u, ml = (uint32_t)b;
    uint32_t x0 = ml << sh, x1 = sh ? (ml >> (32 - sh)) | (mh << sh) : mh, x2 = sh ? mh >> (32 - sh) : 0;
    uint32_t X0 = 0, X1 = x0, X2 = x1, X3 = x2;  // m << s in 4 limbs (s < 64)
    if (s >= 64) {                                // (then x2 == 0)
        X1 = 0;
        X2 = x0;
        X3 = x1;
    }
    double nd = a * invpio2;
    if (nd < 0x1p52) nd = (nd + 0x1p52) - 0x1p52;  // the nearest integer (above 2^52 nd is one)
    uint32_t nh = (uint32_t)(int32_t)(nd * 0x1p-32);
    uint32_t nl = (uint32_t)(int32_t)(nd - (double)(int32_t)nh * 0x1p32 - 0x1p31) ^ 0x80000000u;
    uint64_t p00 = __emulu(nl, M0), p01 = __emulu(nl, M1), p10 = __emulu(nh, M0), p11 = __emulu(nh, M1);
    uint64_t c = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    uint32_t N1 = (uint32_t)c;
    c = (c >> 32) + (p01 >> 32) + (p10 >> 32) + (uint32_t)p11 + __emulu(nl, 3);
    uint32_t N2 = (uint32_t)c, N3 = (uint32_t)((c >> 32) + (p11 >> 32)) + nh * 3;
    uint64_t d = (uint64_t)X0 - (uint32_t)p00;
    uint32_t R0 = (uint32_t)d, br = (uint32_t)(d >> 63);
    d = (uint64_t)X1 - N1 - br;
    uint32_t R1 = (uint32_t)d;
    br = (uint32_t)(d >> 63);
    d = (uint64_t)X2 - N2 - br;
    uint32_t R2 = (uint32_t)d;
    br = (uint32_t)(d >> 63);
    uint32_t R3 = X3 - N3 - br;
    int sg = 1;
    if (R3 >> 31) {  // R < 0: negate
        sg = -1;
        d = (uint64_t)(uint32_t)~R0 + 1;
        R0 = (uint32_t)d;
        d = (uint64_t)(uint32_t)~R1 + (uint32_t)(d >> 32);
        R1 = (uint32_t)d;
        R2 = ~R2 + (uint32_t)(d >> 32);
    }
    int k = (int)((U32ToD(R2) * 0x1p64 + U32ToD(R1) * 0x1p32 + U32ToD(R0)) * INVM + 0.5);  // |R| < 2^77
    uint32_t quad = nl + (uint32_t)(sg * k);
    if (k) {  // R -= k*M
        uint64_t k0 = __emulu((uint32_t)k, M0), k1 = __emulu((uint32_t)k, M1);
        c = (k0 >> 32) + (uint32_t)k1;
        uint32_t K1 = (uint32_t)c, K2 = (uint32_t)((c >> 32) + (k1 >> 32)) + 3u * (uint32_t)k;
        d = (uint64_t)R0 - (uint32_t)k0;
        R0 = (uint32_t)d;
        br = (uint32_t)(d >> 63);
        d = (uint64_t)R1 - K1 - br;
        R1 = (uint32_t)d;
        br = (uint32_t)(d >> 63);
        R2 = R2 - K2 - br;
        if (R2 >> 31) {
            sg = -sg;
            d = (uint64_t)(uint32_t)~R0 + 1;
            R0 = (uint32_t)d;
            d = (uint64_t)(uint32_t)~R1 + (uint32_t)(d >> 32);
            R1 = (uint32_t)d;
            R2 = ~R2 + (uint32_t)(d >> 32);
        }
    }
    double A = U32ToD(R2) * 0x1p64 + U32ToD(R1) * 0x1p32, B = U32ToD(R0);  // |R| < 2^66: A exact
    double h = A + B, l = B - (h - A);
    if (sg < 0) {
        h = -h;
        l = -l;
    }
    rh = h * 0x1p-65;
    rl = l * 0x1p-65;
    return (int)(quad & 3);
}

// s_atan.c
const double atanhi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01,
                         1.57079632679489655800e+00};
const double atanlo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17,
                         6.12323399573676603587e-17};
const double aT[] = {3.33333333333329318027e-01,  -1.99999999998764832476e-01, 1.42857142725034663711e-01,
                     -1.11111104054623557880e-01, 9.09088713343650656196e-02,  -7.69187620504482999495e-02,
                     6.66107313738753120669e-02,  -5.83357013379057348645e-02, 4.97687799461593236017e-02,
                     -3.65315727442169155270e-02, 1.62858201153657823623e-02};

// atan(a) + c = hi + lo (0 <= a < 2^66, c a small correction): s_atan.c with c added before the last rounding
inline void AtanP(double a, double c, double& hi, double& lo) {
    int32_t ix = Hi(a);
    int id;
    if (ix < 0x3fdc0000) {  // a < 0.4375
        if (ix < 0x3e200000) {  // a < 2^-29
            hi = a;
            lo = c;
            return;
        }
        id = -1;
    } else if (ix < 0x3ff30000) {  // a < 1.1875
        if (ix < 0x3fe60000) {     // 7/16 <= a < 11/16
            id = 0;
            a = (2.0 * a - 1.0) / (2.0 + a);
        } else {  // 11/16 <= a < 19/16
            id = 1;
            a = (a - 1.0) / (a + 1.0);
        }
    } else if (ix < 0x40038000) {  // a < 2.4375
        id = 2;
        a = (a - 1.5) / (1.0 + 1.5 * a);
    } else {  // 2.4375 <= a < 2^66
        id = 3;
        a = -1.0 / a;
    }
    double z = a * a, w = z * z;
    double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) {
        hi = a;
        lo = c - a * (s1 + s2);
        return;
    }
    hi = atanhi[id];
    lo = a - ((a * (s1 + s2) - atanlo[id]) - c);
}

// e_atan2.c
const double pi_o_4 = 7.8539816339744827900e-01, pi_o_2 = 1.5707963267948965580e+00, pi = 3.1415926535897931160e+00,
             pi_lo = 1.2246467991473531772e-16;

// Exp: log2(e) rounded to 64 bits (FLDL2E) * 2^63; ln 2 in two parts; 2^(j/32) in two parts, j = -16..16
const uint32_t L2EHI = 0xb8aa3b29u, L2ELO = 0x5c17f0bcu;
const double LN2H = 0x1.62e42fefa39efp-1, LN2L = 0x1.abc9e3b39803fp-56;
const double E3 = 0x1.5555555555555p-3, E4 = 0x1.5555555555555p-5, E5 = 0x1.1111111111111p-7,
             E6 = 0x1.6c16c16c16c17p-10, E7 = 0x1.a01a01a01a01ap-13, E8 = 0x1.a01a01a01a01ap-16;  // 1/k!
const double EXP2T[33][2] = {
    {0x1.6a09e667f3bcdp-1, -0x1.bdd3413b26456p-55}, {0x1.71f75e8ec5f74p-1, -0x1.16e4786887a99p-56},
    {0x1.7a11473eb0187p-1, -0x1.41577ee04992fp-56}, {0x1.82589994cce13p-1, -0x1.d4c1dd41532d8p-55},
    {0x1.8ace5422aa0dbp-1, 0x1.6e9f156864b27p-55},  {0x1.93737b0cdc5e5p-1, -0x1.75fc781b57ebcp-58},
    {0x1.9c49182a3f090p-1, 0x1.c7c46b071f2bep-57},  {0x1.a5503b23e255dp-1, -0x1.d2f6edb8d41e1p-55},
    {0x1.ae89f995ad3adp-1, 0x1.7a1cd345dcc81p-55},  {0x1.b7f76f2fb5e47p-1, -0x1.5584f7e54ac3bp-57},
    {0x1.c199bdd85529cp-1, 0x1.11065895048ddp-56},  {0x1.cb720dcef9069p-1, 0x1.503cbd1e949dbp-57},
    {0x1.d5818dcfba487p-1, 0x1.2ed02d75b3707p-56},  {0x1.dfc97337b9b5fp-1, -0x1.1a5cd4f184b5cp-55},
    {0x1.ea4afa2a490dap-1, -0x1.e9c23179c2893p-55}, {0x1.f50765b6e4540p-1, 0x1.9d3e12dd8a18bp-55},
    {0x1.0000000000000p0, 0.0},                     {0x1.059b0d3158574p0, 0x1.d73e2a475b465p-55},
    {0x1.0b5586cf9890fp0, 0x1.8a62e4adc610bp-54},   {0x1.11301d0125b51p0, -0x1.6c51039449b3ap-54},
    {0x1.172b83c7d517bp0, -0x1.19041b9d78a76p-55},  {0x1.1d4873168b9aap0, 0x1.e016e00a2643cp-54},
    {0x1.2387a6e756238p0, 0x1.9b07eb6c70573p-54},   {0x1.29e9df51fdee1p0, 0x1.612e8afad1255p-55},
    {0x1.306fe0a31b715p0, 0x1.6f46ad23182e4p-55},   {0x1.371a7373aa9cbp0, -0x1.63aeabf42eae2p-54},
    {0x1.3dea64c123422p0, 0x1.ada0911f09ebcp-55},   {0x1.44e086061892dp0, 0x1.89b7a04ef80d0p-59},
    {0x1.4bfdad5362a27p0, 0x1.d4397afec42e2p-56},   {0x1.5342b569d4f82p0, -0x1.07abe1db13cadp-55},
    {0x1.5ab07dd485429p0, 0x1.6324c054647adp-54},   {0x1.6247eb03a5585p0, -0x1.383c17e40b497p-54},
    {0x1.6a09e667f3bcdp0, -0x1.bdd3413b26456p-54}};

// e_log.c
const double ln2HI = 6.93147180369123816490e-01, ln2LO = 1.90821492927058770002e-10, two54 = 1.80143985094819840000e+16,
             Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01, Lg3 = 2.857142874366239149e-01,
             Lg4 = 2.222219843214978396e-01, Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
             Lg7 = 1.479819860511658591e-01;

}  // namespace detail

using namespace softmath::detail;  // (inside namespace softmath only)

// Beyond the x87's range (|x| >= 2^63) FSIN and FCOS leave the argument unchanged; infinities are invalid operations
// (the indefinite NaN); NaNs pass through.
inline double Sin(double x) {
    int32_t ix = Hi(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return KSin(x, 0.0, 0);
    if (ix >= 0x43e00000) return ix < 0x7ff00000 ? x : IsNan(x) ? Add(x, x) : Nan();
    double rh, rl;
    int n = RemPio2(Abs(x), rh, rl);
    double s = n & 1 ? KCos(rh, rl) : KSin(rh, rl, 1);
    if (n & 2) s = -s;
    return Hi(x) < 0 ? -s : s;
}

inline double Cos(double x) {
    int32_t ix = Hi(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return KCos(x, 0.0);
    if (ix >= 0x43e00000) return ix < 0x7ff00000 ? x : IsNan(x) ? Add(x, x) : Nan();
    double rh, rl;
    int n = RemPio2(Abs(x), rh, rl);
    double c = n & 1 ? KSin(rh, rl, 1) : KCos(rh, rl);
    return (n + 1) & 2 ? -c : c;
}

// Beyond its range FSINCOS pushes nothing: the game's routine stores the argument as the cosine and the empty register
// (the indefinite NaN) as the sine.
inline void SinCos(double x, double* s, double* c) {
    int32_t ix = Hi(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {
        *s = KSin(x, 0.0, 0);
        *c = KCos(x, 0.0);
        return;
    }
    if (ix >= 0x43e00000) {
        if (ix < 0x7ff00000) {
            *s = Nan();
            *c = x;
        } else
            *s = *c = IsNan(x) ? Add(x, x) : Nan();
        return;
    }
    double rh, rl;
    int n = RemPio2(Abs(x), rh, rl);
    double ks = KSin(rh, rl, 1), kc = KCos(rh, rl);
    switch (n) {
        case 0: *s = ks; *c = kc; break;
        case 1: *s = kc; *c = -ks; break;
        case 2: *s = -ks; *c = -kc; break;
        default: *s = -kc; *c = ks; break;
    }
    if (Hi(x) < 0) *s = -*s;
}

// Beyond its range FPTAN pushes nothing: the game's routine pops the argument and returns the empty register.
inline double Tan(double x) {
    int32_t ix = Hi(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return KTan(x, 0.0, 1);
    if (ix >= 0x43e00000) return IsNan(x) ? Add(x, x) : Nan();
    double rh, rl;
    int n = RemPio2(Abs(x), rh, rl);
    double t = KTan(rh, rl, 1 - ((n & 1) << 1));
    return Hi(x) < 0 ? -t : t;
}

inline double Atan(double x) {
    int32_t hx = Hi(x), ix = hx & 0x7fffffff;
    int id;
    if (ix >= 0x44100000) {  // |x| >= 2^66
        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && Lo(x) != 0)) return Add(x, x);  // NaN
        return hx > 0 ? Add(atanhi[3], atanlo[3]) : -Add(atanhi[3], atanlo[3]);
    }
    if (ix < 0x3fdc0000) {                // |x| < 0.4375
        if (ix < 0x3e200000) return x;   // |x| < 2^-29
        id = -1;
    } else {
        x = Abs(x);
        if (ix < 0x3ff30000) {      // |x| < 1.1875
            if (ix < 0x3fe60000) {  // 7/16 <= |x| < 11/16
                id = 0;
                x = (2.0 * x - 1.0) / (2.0 + x);
            } else {  // 11/16 <= |x| < 19/16
                id = 1;
                x = (x - 1.0) / (x + 1.0);
            }
        } else {
            if (ix < 0x40038000) {  // |x| < 2.4375
                id = 2;
                x = (x - 1.5) / (1.0 + 1.5 * x);
            } else {  // 2.4375 <= |x| < 2^66
                id = 3;
                x = -1.0 / x;
            }
        }
    }
    double z = x * x, w = z * z;
    double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) return Sub(x, x * (s1 + s2));
    z = Sub(atanhi[id], (x * (s1 + s2) - atanlo[id]) - x);
    return hx < 0 ? -z : z;
}

// FPATAN: atan(y/x) in (-pi, pi]. NaN operands: the NaN; of two, the one with the larger significand (equal
// significands: the positive one).
inline double Atan2(double y, double x) {
    if (IsNan(x) || IsNan(y)) {
        if (!IsNan(y)) return Add(x, x);
        if (!IsNan(x)) return Add(y, y);
        uint64_t mx = Bits(x) & 0x000fffffffffffffull, my = Bits(y) & 0x000fffffffffffffull;
        return mx > my ? x : my > mx ? y : FromBits(Bits(x) & Bits(y));
    }
    int32_t hx = Hi(x), ix = hx & 0x7fffffff, hy = Hi(y), iy = hy & 0x7fffffff;
    uint32_t lx = Lo(x), ly = Lo(y);
    int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);  // 2*sign(x) + sign(y)
    if ((iy | ly) == 0) {                         // y = 0
        switch (m) {
            case 0:
            case 1: return y;
            case 2: return pi;
            default: return -pi;
        }
    }
    if ((ix | lx) == 0) return hy < 0 ? -pi_o_2 : pi_o_2;  // x = 0
    if (ix == 0x7ff00000) {                                 // x = INF
        if (iy == 0x7ff00000) {
            switch (m) {
                case 0: return pi_o_4;
                case 1: return -pi_o_4;
                case 2: return 3.0 * pi_o_4;
                default: return -3.0 * pi_o_4;
            }
        }
        switch (m) {
            case 0: return 0.0;
            case 1: return -0.0;
            case 2: return pi;
            default: return -pi;
        }
    }
    if (iy == 0x7ff00000) return hy < 0 ? -pi_o_2 : pi_o_2;  // y = INF
    int32_t k = (iy - ix) >> 20;
    if (k > 60) return hy < 0 ? -pi_o_2 : pi_o_2;  // |y/x| > 2^60: pi/2 -+ |x/y| rounds to pi/2
    if (k < -60) {                                 // |y/x| < 2^-60: atan(y/x) = y/x
        if (hx < 0) return hy < 0 ? -pi : pi;
        double z = Div(Abs(y), Abs(x));
        return hy < 0 ? -z : z;
    }
    // q = |y/x| rounded, and the exact remainder |y| - q|x| (Dekker's product) for atan's first-order correction;
    // scaled by a power of two first (|x| into [1, 2)) so that nothing overflows or underflows
    double ax = Abs(x), ay = Abs(y);
    if (ix >= 0x5fe00000) {  // |x| >= 2^511
        ax *= 0x1p-600;
        ay *= 0x1p-600;
    } else if (ix < 0x20000000) {  // |x| < 2^-511 (subnormals included)
        ax *= 0x1p600;
        ay *= 0x1p600;
    }
    double sc = FromBits((uint64_t)(uint32_t)(2046 - ((Hi(ax) >> 20) & 0x7ff)) << 52);
    ax *= sc;
    ay *= sc;
    double q = ay / ax, ph, pl;
    TwoProd(q, ax, ph, pl);
    double c = (((ay - ph) - pl) / ax) / (1.0 + q * q);
    double hi, lo;
    AtanP(q, c, hi, lo);
    if (m < 2) {
        double z = Add(hi, lo);
        return m ? -z : z;
    }
    double s, e;  // pi - atan: pi's two parts with the exact difference
    TwoSum(pi, -hi, s, e);
    double z = Add(s, (e + pi_lo) - lo);
    return m == 2 ? z : -z;
}

// The game's Exp (fld x; fldl2e; fmulp; fld st0; frndint; fsub st1, st; fxch; f2xm1; fld1; faddp; fscale; fstp st1)
// under the x87 precision control pc (the control word's bits 8-9; 0: 24-bit, 2: 53-bit, 3: 64-bit significands):
// t = x * L2E64 (exact: 117-bit integer product) and y = 1 + (2^f - 1) are rounded to that precision, f = t - rint(t)
// and fscale are exact, F2XM1's result is 2^f - 1 in double-double rounded to 64 bits. The routine's extended result
// rounded to a double (exp(710) = inf, exp(-745) = 4.9e-324).
inline double Exp(double x, int pc) {
    uint64_t bx = Bits(x);
    uint32_t hx = (uint32_t)(bx >> 32) & 0x7fffffffu;
    if (hx >= 0x7ff00000u) return IsNan(x) ? Add(x, x) : Nan();  // +-inf: inf - inf in the routine
    if (hx >= 0x408f4000u) return Hi(x) < 0 ? 0.0 : Inf(false);  // |x| >= 1000: 2^n beyond any double
    if (hx < 0x3af00000u) return 1.0;  // |x| < 2^-80
    // Q = m * L2E (|x| = m * 2^(E-1075), L2E64 = L2E * 2^-63): 117 bits in 32-bit limbs
    uint32_t mh = (hx & 0xfffffu) | 0x100000u, ml = (uint32_t)bx;
    uint64_t a = __emulu(ml, L2ELO), b = __emulu(ml, L2EHI), c = __emulu(mh, L2ELO), d = __emulu(mh, L2EHI);
    uint64_t s1 = (a >> 32) + (uint32_t)b + (uint32_t)c;
    uint64_t s2 = (s1 >> 32) + (b >> 32) + (c >> 32) + (uint32_t)d;
    uint32_t q0 = (uint32_t)a, q1 = (uint32_t)s1, q2 = (uint32_t)s2, q3 = (uint32_t)((s2 >> 32) + (d >> 32));
    int et = (int)(hx >> 20) - 1075 - 63;  // |x| * L2E64 = Q * 2^et
    if (q3 < 0x100000u) {                  // Q < 2^116: into [2^116, 2^117)
        q3 = (q3 << 1) | (q2 >> 31);
        q2 = (q2 << 1) | (q1 >> 31);
        q1 = (q1 << 1) | (q0 >> 31);
        q0 <<= 1;
        et--;
    }
    uint64_t T;  // |t| = T * 2^et: Q rounded to the precision (nearest, ties to even)
    if (pc == 2) {
        T = ((uint64_t)q3 << 32) | q2;
        if (q1 > 0x80000000u || (q1 == 0x80000000u && (q0 | (uint32_t)(T & 1)))) T++;
        et += 64;
    } else if (pc == 0) {
        T = ((uint64_t)q3 << 3) | (q2 >> 29);
        uint32_t r = q2 & 0x1fffffffu;
        if (r > 0x10000000u || (r == 0x10000000u && (q1 | q0 | (uint32_t)(T & 1)))) T++;
        et += 93;
    } else {
        T = ((uint64_t)q3 << 43) | ((uint64_t)q2 << 11) | (q1 >> 21);
        uint32_t r = q1 & 0x1fffffu;
        if (r > 0x100000u || (r == 0x100000u && (q0 | (uint32_t)(T & 1)))) T++;
        if (T == 0) {  // carried out of 64 bits
            T = 0x8000000000000000ull;
            et++;
        }
        et += 53;
    }
    double th, tl, sc = Pow2(et);  // t exactly (a double unless 64-bit)
    FastTwoSum(U32ToD((uint32_t)(T >> 32)) * 0x1p32, U32ToD((uint32_t)T), th, tl);
    th *= sc;
    tl *= sc;
    if (Hi(x) < 0) {
        th = -th;
        tl = -tl;
    }
    // frndint (to nearest even) and f = t - n
    double nd = (th + 0x1.8p52) - 0x1.8p52, fh, fl;
    int n = (int)nd;
    TwoSum(th - nd, tl, fh, fl);
    if (fh > 0.5 || (fh == 0.5 && fl > 0)) {
        n++;
        fh -= 1.0;
    } else if (fh < -0.5 || (fh == -0.5 && fl < 0)) {
        n--;
        fh += 1.0;
    }
    // g = 2^f - 1 = 2^(j/32) * (1 + expm1(u)) - 1 with u = (f - j/32) * ln2, |u| <= ln2/64; about 2^-100 accurate
    double jd = (fh * 32.0 + 0x1.8p52) - 0x1.8p52, uh, ul, sh, sl, eh, el, ph, pl, gh, gl;
    int j = (int)jd;
    double dh = fh - jd * 0.03125;
    TwoProd(dh, LN2H, uh, ul);
    TwoSum(uh, ul + (dh * LN2L + fl * LN2H), uh, ul);
    TwoProd(uh, uh, sh, sl);
    double poly = uh * sh * (E3 + uh * (E4 + uh * (E5 + uh * (E6 + uh * (E7 + uh * E8)))));
    TwoSum(uh, 0.5 * sh, eh, el);
    el += ul + (0.5 * sl + uh * ul + poly);
    FastTwoSum(eh, el, eh, el);
    const double* tj = EXP2T[j + 16];
    TwoProd(tj[0], eh, ph, pl);
    TwoSum(tj[0] - 1.0, ph, gh, gl);
    gl += tj[1] + (pl + tj[0] * el + tj[1] * eh);
    FastTwoSum(gh, gl, gh, gl);
    if (gh != 0) {  // F2XM1's 64-bit result
        int eg = ((Hi(gh) >> 20) & 0x7ff) - 1023;
        if (gl != 0 && (Bits(gh) & 0xfffffffffffffull) == 0 && (gl < 0) == (gh > 0)) eg--;  // just below a power of 2
        gl = RoundTo(gl, eg - 63);
    }
    // y + yl = 1 + g rounded to the precision (yl: the 64-bit significand's last 11 bits)
    double h, l, y, yl = 0;
    TwoSum(1.0, gh, h, l);
    l += gl;
    if (pc == 2)
        y = h + l;
    else {
        int e = h > 1.0 || (h == 1.0 && l >= 0) ? 0 : -1;
        if (pc == 0) {
            y = RoundTo(h, e - 23);
            double r = h - y;
            if (l != 0 && Abs(r) == Pow2(e - 24) && (r > 0) == (l > 0)) y += r + r;
        } else {
            y = h;
            yl = RoundTo(l, e - 63);
        }
    }
    // fscale; the extended result rounded to a double once (also into the subnormals)
    if (n > 1024) return Inf(false);
    if (n > -1022 || (n == -1022 && (y > 1.0 || (y == 1.0 && yl >= 0)))) {
        y += yl;
        return n < 1024 ? Mul(y, Pow2(n)) : Mul(Mul(y, 0x1p1023), 2.0);
    }
    if (n < -1076) return 0.0;
    double A = y * Pow2(n + 1074), R = (A + 0x1p52) - 0x1p52;  // in units of 2^-1074: A + yl*2^(n+1074) to an integer
    if (yl != 0 && Abs(A - R) == 0.5 && (A - R > 0) == (yl > 0)) R += 2.0 * (A - R);
    return Mul(R, 0x1p-1074);
}

inline double Log(double x) {
    int32_t hx = Hi(x), k = 0;
    uint32_t lx = Lo(x);
    if (hx < 0x00100000) {                                  // x < 2^-1022
        if (((hx & 0x7fffffff) | lx) == 0) return Inf(true);  // log(+-0) = -inf
        if (hx < 0) return IsNan(x) ? Add(x, x) : Nan();           // log(-#) = NaN
        k -= 54;
        x *= two54;  // subnormal number, scale up x
        hx = Hi(x);
    }
    if (hx >= 0x7ff00000) return Add(x, x);
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    int32_t i = (hx + 0x95f64) & 0x100000;
    x = WithHi(x, hx | (i ^ 0x3ff00000));  // normalize x or x/2
    k += (i >> 20);
    double f = x - 1.0;
    double dk = (double)k;
    if ((0x000fffff & (2 + hx)) < 3) {  // -2^-20 <= f < 2^-20
        if (f == 0.0) return k == 0 ? 0.0 : Add(dk * ln2HI, dk * ln2LO);
        double R = f * f * (0.5 - 0.33333333333333333 * f);
        return k == 0 ? Sub(f, R) : Sub(dk * ln2HI, (R - dk * ln2LO) - f);
    }
    double s = f / (2.0 + f);
    double z = s * s;
    i = hx - 0x6147a;
    double w = z * z;
    int32_t j = 0x6b851 - hx;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i |= j;
    double R = t2 + t1;
    if (i > 0) {
        double hfsq = 0.5 * f * f;
        if (k == 0) return Sub(f, hfsq - s * (hfsq + R));
        return Sub(dk * ln2HI, (hfsq - (s * (hfsq + R) + dk * ln2LO)) - f);
    }
    if (k == 0) return Sub(f, s * (f - R));
    return Sub(dk * ln2HI, (s * (f - R) - dk * ln2LO) - f);
}

}  // namespace softmath
