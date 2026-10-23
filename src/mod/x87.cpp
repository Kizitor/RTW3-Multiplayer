// The game's x87 transcendental routines in live battles (engine level 16).
//
// Intel and AMD CPUs don't round FSIN, FCOS, FSINCOS, FPTAN, FPATAN, FYL2X and F2XM1 identically, so two PCs could load
// or play the same battle differently. In this game build every such instruction sits in one of eleven tiny RTL leaf
// routines (System's Exp, Cos, Sin, SinCos, Ln, ArcTan, an ArcSin(Single), an ArcTan2, and a second unit's Tan, SinCos,
// ArcTan2; no transcendental instruction is inlined anywhere else: docs/GAME-INTERNALS.md "x87 transcendentals").
// They are found by their exact bytes and hooked. Outside a live battle each hook jumps straight to the game's own
// routine. In a live battle (SetSoft) the routines are computed by softmath.h instead: the same bits on every CPU.
//
// TEST ONLY: RTW3MP_TEST_X87=ext[:N] | dbl[:N] | ppm[:N] makes this copy's own (hardware) results differ from another
// CPU's: every N-th result (default 1) is nudged by one unit in the last place of the 64-bit mantissa (ext), of a double
// (dbl), or by about 1e-6 of its value (ppm: far beyond any CPU difference, to show which results reach the battle).
// Exact results (0, +-1, infinities, NaN) are left alone. The software maths is not affected by it (a live battle with
// the software maths on doesn't use the hardware results at all).
#include "x87.h"

#include <emmintrin.h>
#include <intrin.h>

#include <algorithm>

#include "common.h"
#include "softmath.h"
#include "../../third_party/minhook/include/MinHook.h"

namespace x87 {
namespace {

enum Kind { kUnary, kSinCos, kAtan2, kAsinS };
enum Fn { fExp, fCos, fSin, fSinCos, fLn, fArcTan, fArcSinS, fArcTan2, fTan, fSinCos2, fArcTan2b, fCount };

struct Site {
    const char* name;
    Kind kind;
    const char* sig;  // the routine's bytes (hex)
    int copy;         // which occurrence of the bytes (both copies of SinCos and ArcTan2 are identical)
};
const Site kSites[fCount] = {
    {"Exp", kUnary, "558BECDB6D08D9EADEC9D9C0D9FCDCE9D9C9D9F0D9E8DEC1D9FDDDD95DC20C00", 0},
    {"Cos", kUnary, "558BECDB6D08D9FF9B5DC20C00", 0},
    {"Sin", kUnary, "558BECDB6D08D9FE9B5DC20C00", 0},
    {"SinCos", kSinCos, "558BECDB6D08D9FBDB3ADB389B5DC20C00", 0},
    {"Ln", kUnary, "558BECDB6D08D9EDD9C9D9F19B5DC20C00", 0},
    {"ArcTan", kUnary, "558BECDB6D08D9E8D9F39B5DC20C00", 0},
    {"ArcSin", kAsinS, "558BECD9E8D94508DDD2D8C8DEE9D9FAD9F35DC20400", 0},
    {"ArcTan2", kAtan2, "558BECDB6D14DB6D08D9F39B5DC21800", 0},
    {"Tan", kUnary, "558BECDB6D08D9F2DDD89B5DC20C00", 0},
    {"SinCos(2)", kSinCos, "558BECDB6D08D9FBDB3ADB389B5DC20C00", 1},
    {"ArcTan2(2)", kAtan2, "558BECDB6D14DB6D08D9F39B5DC21800", 1},
};

void* o_fn[fCount];         // MinHook trampolines (the game's own routines)
void* g_target[fCount];     // the game's routines (hooked)
volatile long g_count[fCount];
volatile long g_soft = 0;   // 1: the software maths
volatile long g_pert = 0;   // TEST: 1 nudge the 64-bit mantissa, 2 a double's last place
long g_pertEvery = 1, g_pertSeen = 0, g_pertDone = 0;
bool g_tried = false, g_ok = false;
std::string g_testText;

// MXCSR for the software maths: round to nearest, no flush-to-zero / denormals-are-zero, every exception masked (the
// game's RTL leaves some SSE exceptions unmasked; the x87 routines run with all of them masked).
struct MxGuard {
    unsigned old;
    bool set;
    MxGuard() {
        old = _mm_getcsr();
        set = (old & 0xFFC0u) != 0x1F80u;
        if (set) _mm_setcsr(0x1F80u | (old & 0x3Fu));
    }
    ~MxGuard() {
        if (set) _mm_setcsr(old);
    }
};

// The x87 precision control (control word bits 8-9): the game's Exp rounds two intermediate results to it.
int Pc() {
    unsigned short cw;
    __asm fnstcw cw
    return (cw >> 8) & 3;
}

double __cdecl SoftUnary(int fn, double x) {
    MxGuard g;
    switch (fn) {
        case fExp: return softmath::Exp(x, Pc());
        case fCos: return softmath::Cos(x);
        case fSin: return softmath::Sin(x);
        case fLn: return softmath::Log(x);
        case fArcTan: return softmath::Atan(x);
        default: return softmath::Tan(x);
    }
}

// Delphi's SinCos(Theta; var Sin, Cos: Extended): EAX = @Sin, EDX = @Cos.
void __cdecl SoftSinCos(void* sinOut, void* cosOut, double x) {
    double s, c;
    {
        MxGuard g;
        softmath::SinCos(x, &s, &c);
    }
    __asm {
        fld s
        mov eax, sinOut
        fstp tbyte ptr [eax]
        fld c
        mov eax, cosOut
        fstp tbyte ptr [eax]
    }
}

double __cdecl SoftAtan2(double x, double y) {
    MxGuard g;
    return softmath::Atan2(y, x);
}

// The game's ArcSin(Single): FPATAN(x, sqrt(1 - x*x)), the multiply, subtraction and root rounded to double precision
// (the battle's control word) as here.
double __cdecl SoftAsinS(double x) {
    MxGuard g;
    double t = 1.0 - x * x;
    return softmath::Atan2(x, _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(t), _mm_set_sd(t))));
}

// TEST: nudges an 80-bit result in memory (mantissa: 8 bytes, then sign and exponent).
void __cdecl PerturbExt(void* p) {
    uint64_t m;
    uint16_t se;
    memcpy(&m, p, 8);
    memcpy(&se, (uint8_t*)p + 8, 2);
    uint16_t e = se & 0x7FFF;
    if (m == 0 || e == 0x7FFF || (e == 0x3FFF && m == 0x8000000000000000ull)) return;  // 0, inf/NaN, +-1
    if (++g_pertSeen % g_pertEvery) return;
    uint64_t d = g_pert == 3 ? m >> 20 : g_pert == 2 ? 0x800 : 1;
    m = m > 0xFFFFFFFFFFFFFFFFull - d ? m - d : m + d;
    memcpy(p, &m, 8);
    g_pertDone++;
}
void __cdecl PerturbSinCos(void* s, void* c) {
    PerturbExt(s);
    PerturbExt(c);
}

// The hooks. Hardware path: a jump to the trampoline (the game's own code, unchanged). Software path: the argument is
// rounded to a double by the x87 (the battle's control word: round to nearest) and computed by softmath. EAX, ECX and
// EDX are kept (the routines don't change them).
#define X87_UNARY(NAME, K)                                                                                              \
    __declspec(naked) void NAME##_soft() {                                                                              \
        __asm push ecx                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm fld tbyte ptr [esp + 16]                                                                                  \
        __asm sub esp, 8                                                                                                \
        __asm fstp qword ptr [esp]                                                                                      \
        __asm push K                                                                                                    \
        __asm call SoftUnary                                                                                            \
        __asm add esp, 12                                                                                               \
        __asm pop eax                                                                                                   \
        __asm pop edx                                                                                                   \
        __asm pop ecx                                                                                                   \
        __asm ret 12                                                                                                    \
    }                                                                                                                   \
    __declspec(naked) void NAME##_pert() {                                                                              \
        __asm push dword ptr [esp + 12]                                                                                 \
        __asm push dword ptr [esp + 12]                                                                                 \
        __asm push dword ptr [esp + 12]                                                                                 \
        __asm call dword ptr [o_fn + K * 4]                                                                             \
        __asm push ecx                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm sub esp, 12                                                                                               \
        __asm fstp tbyte ptr [esp]                                                                                      \
        __asm push esp                                                                                                  \
        __asm call PerturbExt                                                                                           \
        __asm add esp, 4                                                                                                \
        __asm fld tbyte ptr [esp]                                                                                       \
        __asm add esp, 12                                                                                               \
        __asm pop eax                                                                                                   \
        __asm pop edx                                                                                                   \
        __asm pop ecx                                                                                                   \
        __asm ret 12                                                                                                    \
    }                                                                                                                   \
    __declspec(naked) void NAME() {                                                                                     \
        __asm inc dword ptr [g_count + K * 4]                                                                           \
        __asm cmp dword ptr [g_soft], 0                                                                                 \
        __asm jne NAME##_soft                                                                                           \
        __asm cmp dword ptr [g_pert], 0                                                                                 \
        __asm jne NAME##_pert                                                                                           \
        __asm jmp dword ptr [o_fn + K * 4]                                                                              \
    }

#define X87_SINCOS(NAME, K)                                                                                             \
    __declspec(naked) void NAME##_soft() {                                                                              \
        __asm push ecx                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm fld tbyte ptr [esp + 16]                                                                                  \
        __asm sub esp, 8                                                                                                \
        __asm fstp qword ptr [esp]                                                                                      \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm call SoftSinCos                                                                                           \
        __asm add esp, 16                                                                                               \
        __asm pop eax                                                                                                   \
        __asm pop edx                                                                                                   \
        __asm pop ecx                                                                                                   \
        __asm ret 12                                                                                                    \
    }                                                                                                                   \
    __declspec(naked) void NAME##_pert() {                                                                              \
        __asm push eax                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push dword ptr [esp + 20]                                                                                 \
        __asm push dword ptr [esp + 20]                                                                                 \
        __asm push dword ptr [esp + 20]                                                                                 \
        __asm call dword ptr [o_fn + K * 4]                                                                             \
        __asm pop edx                                                                                                   \
        __asm pop eax                                                                                                   \
        __asm push ecx                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm call PerturbSinCos                                                                                        \
        __asm add esp, 8                                                                                                \
        __asm pop eax                                                                                                   \
        __asm pop edx                                                                                                   \
        __asm pop ecx                                                                                                   \
        __asm ret 12                                                                                                    \
    }                                                                                                                   \
    __declspec(naked) void NAME() {                                                                                     \
        __asm inc dword ptr [g_count + K * 4]                                                                           \
        __asm cmp dword ptr [g_soft], 0                                                                                 \
        __asm jne NAME##_soft                                                                                           \
        __asm cmp dword ptr [g_pert], 0                                                                                 \
        __asm jne NAME##_pert                                                                                           \
        __asm jmp dword ptr [o_fn + K * 4]                                                                              \
    }

// ArcTan2(Y, X: Extended): X at [esp+4], Y at [esp+16] (Delphi pushes the left argument first).
#define X87_ATAN2(NAME, K)                                                                                              \
    __declspec(naked) void NAME##_soft() {                                                                              \
        __asm push ecx                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm fld tbyte ptr [esp + 28]                                                                                  \
        __asm sub esp, 8                                                                                                \
        __asm fstp qword ptr [esp]                                                                                      \
        __asm fld tbyte ptr [esp + 24]                                                                                  \
        __asm sub esp, 8                                                                                                \
        __asm fstp qword ptr [esp]                                                                                      \
        __asm call SoftAtan2                                                                                            \
        __asm add esp, 16                                                                                               \
        __asm pop eax                                                                                                   \
        __asm pop edx                                                                                                   \
        __asm pop ecx                                                                                                   \
        __asm ret 24                                                                                                    \
    }                                                                                                                   \
    __declspec(naked) void NAME##_pert() {                                                                              \
        __asm push dword ptr [esp + 24]                                                                                 \
        __asm push dword ptr [esp + 24]                                                                                 \
        __asm push dword ptr [esp + 24]                                                                                 \
        __asm push dword ptr [esp + 24]                                                                                 \
        __asm push dword ptr [esp + 24]                                                                                 \
        __asm push dword ptr [esp + 24]                                                                                 \
        __asm call dword ptr [o_fn + K * 4]                                                                             \
        __asm push ecx                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm sub esp, 12                                                                                               \
        __asm fstp tbyte ptr [esp]                                                                                      \
        __asm push esp                                                                                                  \
        __asm call PerturbExt                                                                                           \
        __asm add esp, 4                                                                                                \
        __asm fld tbyte ptr [esp]                                                                                       \
        __asm add esp, 12                                                                                               \
        __asm pop eax                                                                                                   \
        __asm pop edx                                                                                                   \
        __asm pop ecx                                                                                                   \
        __asm ret 24                                                                                                    \
    }                                                                                                                   \
    __declspec(naked) void NAME() {                                                                                     \
        __asm inc dword ptr [g_count + K * 4]                                                                           \
        __asm cmp dword ptr [g_soft], 0                                                                                 \
        __asm jne NAME##_soft                                                                                           \
        __asm cmp dword ptr [g_pert], 0                                                                                 \
        __asm jne NAME##_pert                                                                                           \
        __asm jmp dword ptr [o_fn + K * 4]                                                                              \
    }

// ArcSin(X: Single): X at [esp+4].
#define X87_ASINS(NAME, K)                                                                                              \
    __declspec(naked) void NAME##_soft() {                                                                              \
        __asm push ecx                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm fld dword ptr [esp + 16]                                                                                  \
        __asm sub esp, 8                                                                                                \
        __asm fstp qword ptr [esp]                                                                                      \
        __asm call SoftAsinS                                                                                            \
        __asm add esp, 8                                                                                                \
        __asm pop eax                                                                                                   \
        __asm pop edx                                                                                                   \
        __asm pop ecx                                                                                                   \
        __asm ret 4                                                                                                     \
    }                                                                                                                   \
    __declspec(naked) void NAME##_pert() {                                                                              \
        __asm push dword ptr [esp + 4]                                                                                  \
        __asm call dword ptr [o_fn + K * 4]                                                                             \
        __asm push ecx                                                                                                  \
        __asm push edx                                                                                                  \
        __asm push eax                                                                                                  \
        __asm sub esp, 12                                                                                               \
        __asm fstp tbyte ptr [esp]                                                                                      \
        __asm push esp                                                                                                  \
        __asm call PerturbExt                                                                                           \
        __asm add esp, 4                                                                                                \
        __asm fld tbyte ptr [esp]                                                                                       \
        __asm add esp, 12                                                                                               \
        __asm pop eax                                                                                                   \
        __asm pop edx                                                                                                   \
        __asm pop ecx                                                                                                   \
        __asm ret 4                                                                                                     \
    }                                                                                                                   \
    __declspec(naked) void NAME() {                                                                                     \
        __asm inc dword ptr [g_count + K * 4]                                                                           \
        __asm cmp dword ptr [g_soft], 0                                                                                 \
        __asm jne NAME##_soft                                                                                           \
        __asm cmp dword ptr [g_pert], 0                                                                                 \
        __asm jne NAME##_pert                                                                                           \
        __asm jmp dword ptr [o_fn + K * 4]                                                                              \
    }

// (C4414: the stubs' conditional jumps to their _soft/_pert parts are near jumps, as intended)
#pragma warning(push)
#pragma warning(disable : 4414)
X87_UNARY(d_Exp, 0)
X87_UNARY(d_Cos, 1)
X87_UNARY(d_Sin, 2)
X87_SINCOS(d_SinCos, 3)
X87_UNARY(d_Ln, 4)
X87_UNARY(d_ArcTan, 5)
X87_ASINS(d_ArcSinS, 6)
X87_ATAN2(d_ArcTan2, 7)
X87_UNARY(d_Tan, 8)
X87_SINCOS(d_SinCos2, 9)
X87_ATAN2(d_ArcTan2b, 10)
#pragma warning(pop)

void* const kDetours[fCount] = {(void*)&d_Exp,    (void*)&d_Cos,     (void*)&d_Sin,     (void*)&d_SinCos,
                                (void*)&d_Ln,     (void*)&d_ArcTan,  (void*)&d_ArcSinS, (void*)&d_ArcTan2,
                                (void*)&d_Tan,    (void*)&d_SinCos2, (void*)&d_ArcTan2b};

std::vector<uint8_t> Unhex(const char* s) {
    std::vector<uint8_t> v;
    for (; s[0] && s[1]; s += 2) {
        auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        v.push_back((uint8_t)(nib(s[0]) << 4 | nib(s[1])));
    }
    return v;
}

// Every occurrence of the bytes in the game's executable sections.
std::vector<uint8_t*> FindAll(const std::vector<uint8_t>& pat) {
    std::vector<uint8_t*> out;
    auto base = (uint8_t*)GetModuleHandleW(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* p = base + sec->VirtualAddress;
        uint8_t* end = p + sec->Misc.VirtualSize;
        while (p < end) {  // readable committed regions only
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            uint8_t* rend = std::min(end, (uint8_t*)mbi.BaseAddress + mbi.RegionSize);
            DWORD pr = mbi.Protect & 0xFF;
            bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
                            (pr == PAGE_READONLY || pr == PAGE_READWRITE || pr == PAGE_WRITECOPY || pr == PAGE_EXECUTE_READ ||
                             pr == PAGE_EXECUTE_READWRITE || pr == PAGE_EXECUTE_WRITECOPY);
            for (uint8_t* q = p; readable && q + pat.size() <= rend; q++) {
                q = (uint8_t*)memchr(q, pat[0], (size_t)(rend - pat.size() + 1 - q));
                if (!q) break;
                if (!memcmp(q, pat.data(), pat.size())) out.push_back(q);
            }
            p = rend;
        }
    }
    return out;
}

// The x87 instructions' own results, as the game's routines compute them under the battle's control word (0x27F: 53-bit
// precision, round to nearest; only Exp's fmulp and faddp depend on the precision), rounded to double.
const unsigned short kBattleCw = 0x27F;

double HwUnary(int fn, double x) {
    double r = 0;
    unsigned short cw = kBattleCw, old;
    __asm {
        fnstcw old
        fldcw cw
        fld x
        mov eax, fn
        cmp eax, 1
        jne n2
        fcos
        jmp done
    n2: cmp eax, 2
        jne n5
        fsin
        jmp done
    n5: cmp eax, 5
        jne n4
        fld1
        fpatan
        jmp done
    n4: cmp eax, 4
        jne n0
        fldln2
        fxch st(1)
        fyl2x
        jmp done
    n0: cmp eax, 0
        jne n8
        fldl2e
        fmulp st(1), st
        fld st(0)
        frndint
        fsub st(1), st
        fxch st(1)
        f2xm1
        fld1
        faddp st(1), st
        fscale
        fstp st(1)
        jmp done
    n8: fptan
        fstp st(0)
    done:
        fstp r
        fldcw old
    }
    return r;
}

void HwSinCos(double x, double* s, double* c) {
    unsigned short cw = kBattleCw, old;
    __asm {
        fnstcw old
        fldcw cw
        fld x
        fsincos
        mov eax, c
        fstp qword ptr [eax]
        mov eax, s
        fstp qword ptr [eax]
        fldcw old
    }
}

double HwAtan2(double y, double x) {
    double r = 0;
    unsigned short cw = kBattleCw, old;
    __asm {
        fnstcw old
        fldcw cw
        fld y
        fld x
        fpatan
        fstp r
        fldcw old
    }
    return r;
}

double Ulps(double a, double b) {
    if (a == b) return 0;
    uint64_t ua, ub;
    memcpy(&ua, &a, 8);
    memcpy(&ub, &b, 8);
    if ((ua ^ ub) >> 63) return 1e9;
    return (double)(ua > ub ? ua - ub : ub - ua);
}

// Calls a hooked routine the way the game's Delphi code does (Extended arguments on the stack, the callee pops them,
// the result in ST0; SinCos: EAX = @Sin, EDX = @Cos). Returns false if ESP, EBX, ESI, EDI or ECX changed.
bool CallGame(int i, double x, double y, double* r, double* r2) {
    void* t = g_target[i];
    double a = 0, b = 0;
    uint8_t s10[10], c10[10];
    unsigned espBefore = 0, espAfter = 0, ecxAfter = 0;
    bool ok = true;
    __asm {
        push ebx
        push esi
        push edi
        mov ebx, 0x11111111
        mov esi, 0x22222222
        mov edi, 0x33333333
        mov espBefore, esp
        mov ecx, 0x44444444
        mov eax, i
        cmp eax, 3
        je L_sincos
        cmp eax, 9
        je L_sincos
        cmp eax, 7
        je L_atan2
        cmp eax, 10
        je L_atan2
        cmp eax, 6
        je L_asins
        sub esp, 12
        fld x
        fstp tbyte ptr [esp]
        call t
        fstp a
        jmp L_done
    L_sincos:
        sub esp, 12
        fld x
        fstp tbyte ptr [esp]
        lea eax, s10
        lea edx, c10
        call t
        jmp L_done
    L_atan2:
        sub esp, 12
        fld y
        fstp tbyte ptr [esp]
        sub esp, 12
        fld x
        fstp tbyte ptr [esp]
        call t
        fstp a
        jmp L_done
    L_asins:
        sub esp, 4
        fld x
        fstp dword ptr [esp]
        call t
        fstp a
    L_done:
        mov espAfter, esp
        mov ecxAfter, ecx
        cmp ebx, 0x11111111
        jne L_bad
        cmp esi, 0x22222222
        jne L_bad
        cmp edi, 0x33333333
        je L_good
    L_bad:
        mov ok, 0
    L_good:
        pop edi
        pop esi
        pop ebx
    }
    if (i == fSinCos || i == fSinCos2) {
        __asm {
            fld tbyte ptr s10
            fstp a
            fld tbyte ptr c10
            fstp b
        }
    }
    *r = a;
    if (r2) *r2 = b;
    return ok && espBefore == espAfter && ecxAfter == 0x44444444;
}

}  // namespace

std::string CpuVendor() {
    static std::string v;
    if (!v.empty()) return v;
    char buf[64] = {0};
    if (GetEnvironmentVariableA("RTW3MP_TEST_CPU", buf, sizeof(buf)) > 0 && buf[0]) {  // TEST ONLY
        v = std::string(buf).substr(0, 16);
        return v;
    }
    int r[4];
    __cpuid(r, 0);
    char s[13];
    memcpy(s, &r[1], 4);
    memcpy(s + 4, &r[3], 4);
    memcpy(s + 8, &r[2], 4);
    s[12] = 0;
    std::string raw = s;
    v = raw == "GenuineIntel" ? "Intel" : raw == "AuthenticAMD" ? "AMD" : raw == "HygonGenuine" ? "Hygon"
        : raw == "CentaurHauls" || raw == "  Shanghai  " ? "Zhaoxin" : raw.empty() ? "unknown" : raw;
    for (auto& c : v)
        if (c == ' ' || c == ';' || c == '=' || (unsigned char)c < 0x20) c = '_';
    return v;
}

bool Install() {
    if (g_tried) return g_ok;
    g_tried = true;
    char buf[32] = {0};
    if (GetEnvironmentVariableA("RTW3MP_TEST_X87", buf, sizeof(buf)) > 0 && buf[0]) {  // TEST ONLY
        std::string t = buf;
        g_pert = t.compare(0, 3, "ppm") == 0 ? 3 : t.compare(0, 3, "dbl") == 0 ? 2 : 1;
        size_t c = t.find(':');
        g_pertEvery = c == std::string::npos ? 1 : std::max(1L, atol(t.c_str() + c + 1));
        g_testText = t;
    }
    std::string found;
    int ok = 0;
    void* targets[fCount] = {};
    for (int i = 0; i < fCount; i++) {  // all of them first: a hooked routine's bytes no longer match its twin's
        auto all = FindAll(Unhex(kSites[i].sig));
        int need = (i == fSinCos || i == fSinCos2 || i == fArcTan2 || i == fArcTan2b) ? 2 : 1;
        if ((int)all.size() != need) {
            found += std::string(" ") + kSites[i].name + "(" + std::to_string(all.size()) + "x)";
            continue;
        }
        targets[i] = all[kSites[i].copy];
    }
    for (int i = 0; i < fCount && found.empty(); i++) {
        void* target = targets[i];
        g_target[i] = target;
        MH_STATUS st = MH_CreateHook(target, kDetours[i], &o_fn[i]);
        if (st == MH_OK) st = MH_EnableHook(target);
        if (st != MH_OK) {
            found += std::string(" ") + kSites[i].name + "(hook " + std::to_string((int)st) + ")";
            continue;
        }
        ok++;
    }
    g_ok = ok == fCount;
    Log("x87: %d of %d transcendental routines hooked%s%s; CPU %s", ok, (int)fCount, found.empty() ? "" : ", missing:",
        found.c_str(), CpuVendor().c_str());
    if (g_ok) {
        std::string chk = StubCheck();
        Log("x87: %s; %s", chk.c_str(), SelfTest().c_str());
        if (chk.compare(0, 14, "stub check ok ") != 0) g_ok = false;  // (live battles keep each CPU's own maths)
    }
    if (g_pert)
        Log("TEST: RTW3MP_TEST_X87=%s: this copy's x87 results are nudged by one unit in the last place of %s (every "
            "%ld. result)", g_testText.c_str(), g_pert == 3 ? "2^20 (ppm: about 1e-6 relative)" : g_pert == 2 ? "a double" : "the 64-bit mantissa", g_pertEvery);
    return g_ok;
}

bool Installed() { return g_ok; }

// While a live battle uses the software maths, this thread's MXCSR is the guard's (every SSE exception masked, round
// to nearest): the game's RTL leaves it at 0x1900 (invalid, zero-divide and overflow unmasked; its x87 code doesn't use
// SSE), which would cost every call two LDMXCSR. Put back at the battle's end.
unsigned g_savedCsr = 0;
bool g_csrSet = false;

void SetSoft(bool on) {
    if (on && !g_ok) return;
    if ((g_soft != 0) != on) Log("x87: the battle's transcendental maths: %s", on ? "software (CPU-independent)" : "this CPU's");
    g_soft = on ? 1 : 0;
    unsigned m = _mm_getcsr();
    if (on && !g_csrSet && (m & 0xFFC0u) != 0x1F80u) {
        g_savedCsr = m;
        g_csrSet = true;
        _mm_setcsr(0x1F80u | (m & 0x3Fu));
        Log("x87: MXCSR %04x -> %04x for the battle", m, _mm_getcsr());
    } else if (!on && g_csrSet) {
        g_csrSet = false;
        _mm_setcsr(g_savedCsr);
    }
}

bool Soft() { return g_soft != 0; }

std::string Counts() {
    std::string s;
    for (int i = 0; i < fCount; i++) {
        if (!g_count[i]) continue;
        s += std::string(s.empty() ? "" : ",") + kSites[i].name + "=" + std::to_string(g_count[i]);
    }
    return s.empty() ? "none" : s;
}

void ResetCounts() {
    for (auto& c : g_count) c = 0;
}

long long TotalCalls() {
    long long n = 0;
    for (auto& c : g_count) n += c;
    return n;
}

std::string TestSwitch() {
    if (!g_pert) return "";
    return g_testText + " nudged=" + std::to_string(g_pertDone);
}

void SetPerturb(const std::string& mode, long every) {
    g_pert = mode == "ext" ? 1 : mode == "dbl" ? 2 : mode == "ppm" ? 3 : 0;
    g_pertEvery = std::max(1L, every);
    g_pertSeen = g_pertDone = 0;
    g_testText = g_pert ? mode + ":" + std::to_string(g_pertEvery) : "";
    Log("TEST: x87 perturbation %s", g_pert ? g_testText.c_str() : "off");
}

// The hooks called as the game calls them, with this CPU's routines and with the software: registers and stack kept,
// the software's results bit-exact, this CPU's close to them (a wrong argument would be far off).
std::string StubCheck() {
    if (!g_ok) return "not installed";
    long soft = g_soft, pert = g_pert;
    std::string bad;
    const struct {
        int i;
        double x, y;
    } cases[] = {{fExp, 0.7, 0},     {fCos, 0.7, 0},    {fSin, 0.7, 0},       {fSinCos, 0.7, 0},
                 {fLn, 0.7, 0},      {fArcTan, 0.7, 0}, {fArcSinS, 0.375, 0}, {fArcTan2, 0.3, -0.7},
                 {fTan, 0.7, 0},     {fSinCos2, 0.7, 0}, {fArcTan2b, 0.3, -0.7}};
    for (auto& c : cases) {
        double h = 0, h2 = 0, s = 0, s2 = 0, e = 0, e2 = 0;
        g_pert = 0;
        g_soft = 0;
        bool ok1 = CallGame(c.i, c.x, c.y, &h, &h2);
        g_soft = 1;
        bool ok2 = CallGame(c.i, c.x, c.y, &s, &s2);
        switch (c.i) {
            case fExp: e = softmath::Exp(c.x, Pc()); break;
            case fCos: e = softmath::Cos(c.x); break;
            case fSin: e = softmath::Sin(c.x); break;
            case fSinCos:
            case fSinCos2: softmath::SinCos(c.x, &e, &e2); break;
            case fLn: e = softmath::Log(c.x); break;
            case fArcTan: e = softmath::Atan(c.x); break;
            case fArcSinS: e = softmath::Atan2(c.x, _mm_cvtsd_f64(_mm_sqrt_sd(_mm_set_sd(1 - c.x * c.x), _mm_set_sd(1 - c.x * c.x)))); break;
            case fArcTan2:
            case fArcTan2b: e = softmath::Atan2(c.y, c.x); break;
            default: e = softmath::Tan(c.x); break;
        }
        if (!ok1 || !ok2) bad += std::string(" ") + kSites[c.i].name + "(registers/stack)";
        if (s != e || s2 != e2) bad += std::string(" ") + kSites[c.i].name + "(software)";
        if (Ulps(h, e) > 64 || Ulps(h2, e2) > 64) bad += std::string(" ") + kSites[c.i].name + "(this CPU)";
    }
    g_soft = soft;
    g_pert = pert;
    ResetCounts();
    return bad.empty() ? "stub check ok (11 routines: registers, stack, results)" : "stub check FAILED:" + bad;
}

// The software against this CPU's instructions under the battle's control word, 2000 fixed arguments per routine: for
// the trigonometric ones also exact double multiples of pi/2, multiples of 45 degrees and arguments near them, and
// large ones; for ArcTan2 every quadrant, small ratios and the axes. The largest difference in units of a double's last
// place.
std::string SelfTest() {
    static const int fns[] = {fExp, fCos, fSin, fLn, fArcTan, fTan, fSinCos, fArcTan2};
    static const char* names[] = {"exp", "cos", "sin", "ln", "atan", "tan", "sincos", "atan2"};
    std::string out = "software vs this CPU (max ulps):";
    unsigned short cw = kBattleCw, old;
    __asm {
        fnstcw old
        fldcw cw
    }
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    auto next = [&seed]() {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return (double)(seed >> 11) / 9007199254740992.0;  // [0, 1)
    };
    for (int k = 0; k < 8; k++) {
        int fn = fns[k];
        double worst = 0;
        for (int i = 0; i < 2000; i++) {
            double u = next(), v = next(), x = 0, y = 0;
            if (fn == fLn)
                x = 1e-3 + u * 1e3;
            else if (fn == fExp)
                x = (u - 0.5) * 40;
            else if (fn == fArcTan)
                x = (u - 0.5) * 12.0;
            else if (fn == fArcTan2) {
                y = (u - 0.5) * 200;
                x = (v - 0.5) * 200;
                switch (i & 7) {
                    case 4: x *= 1e-12; break;
                    case 5: y *= 1e-12; break;
                    case 6: y = (i & 8) ? 0.0 : -0.0; break;
                    case 7: x = (i & 8) ? 0.0 : -0.0; break;
                }
            } else {
                switch (i & 3) {
                    case 0: x = (u - 0.5) * 12.0; break;
                    case 1: x = (double)((int)(u * 129) - 64) * 1.5707963267948966; break;  // k*pi/2
                    case 2:  // k*45 degrees, exactly or nearly
                        x = (double)((int)(u * 33) - 16) * 45.0 * (3.141592653589793 / 180) + ((i & 4) ? 0 : (v - 0.5) * 1e-6);
                        break;
                    default: x = (u - 0.5) * 2e6; break;
                }
            }
            double s, s2 = 0, h, h2 = 0;
            if (fn == fSinCos) {
                {
                    MxGuard g;
                    softmath::SinCos(x, &s, &s2);
                }
                HwSinCos(x, &h, &h2);
            } else if (fn == fArcTan2) {
                s = SoftAtan2(x, y);
                h = HwAtan2(y, x);
            } else {
                s = SoftUnary(fn, x);
                h = HwUnary(fn, x);
            }
            double d = std::max(Ulps(s, h), Ulps(s2, h2));
            if (d > worst) worst = d;
        }
        char b[32];
        snprintf(b, sizeof(b), " %s %.0f", names[k], worst);
        out += b;
    }
    __asm fldcw old
    return out;
}

std::string Bench() {
    static const int fns[] = {fSin, fCos, fArcTan, fExp, fLn};
    static const char* names[] = {"sin", "cos", "atan", "exp", "ln"};
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    std::string out;
    volatile double acc = 0;
    for (int k = 0; k < 5; k++) {
        const int n = 200000;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < n; i++) acc = acc + HwUnary(fns[k], 0.5 + (i & 63) * 0.07);
        QueryPerformanceCounter(&t1);
        double hw = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / n;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < n; i++) acc = acc + SoftUnary(fns[k], 0.5 + (i & 63) * 0.07);
        QueryPerformanceCounter(&t1);
        double sw = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / n;
        char b[64];
        snprintf(b, sizeof(b), "%s%s %.1f/%.1f", out.empty() ? "" : " ", names[k], hw, sw);
        out += b;
    }
    return out + " (ns per call, this CPU/software)";
}

}  // namespace x87
