#include "delphi.h"
#include "../../third_party/minhook/src/hde/hde32.h"

namespace dl {

uintptr_t g_base = 0;
uint32_t g_size = 0;
static std::map<std::string, uintptr_t> g_classes;

static const int VMT_SELF = -88, VMT_TYPEINFO = -72, VMT_FIELDS = -68, VMT_METHODS = -64, VMT_NAME = -56,
                 VMT_SIZE = -52, VMT_PARENT = -48;

static std::vector<uint8_t> g_readable;  // one flag per 4 KB page of the image

static void BuildReadableMap() {
    g_readable.assign((g_size + 0xFFF) / 0x1000, 0);
    uintptr_t a = g_base;
    while (a < g_base + g_size) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)a, &mbi, sizeof(mbi))) break;
        uintptr_t end = std::min<uintptr_t>((uintptr_t)mbi.BaseAddress + mbi.RegionSize, g_base + g_size);
        bool ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                  (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                  PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY));
        for (uintptr_t p = a; p < end; p += 0x1000) g_readable[(p - g_base) / 0x1000] = ok ? 1 : 0;
        a = end;
    }
}

static inline bool InImage(uintptr_t a, uint32_t n = 4) {
    if (a < g_base || a + n > g_base + g_size || a + n < a) return false;
    if (g_readable.empty()) return true;
    for (uintptr_t p = (a - g_base) / 0x1000; p <= (a + n - 1 - g_base) / 0x1000; p++)
        if (!g_readable[p]) return false;
    return true;
}
static inline uint32_t RD32(uintptr_t a) { return *(uint32_t*)a; }
static inline uint16_t RD16(uintptr_t a) { return *(uint16_t*)a; }

bool InitImage() {
    g_base = (uintptr_t)GetModuleHandleW(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)g_base;
    auto nt = (IMAGE_NT_HEADERS32*)(g_base + dos->e_lfanew);
    g_size = nt->OptionalHeader.SizeOfImage;
    return g_size != 0;
}

static bool ReadShortString(uintptr_t a, std::string& out) {
    if (!InImage(a, 1)) return false;
    uint8_t n = *(uint8_t*)a;
    if (!InImage(a + 1, n)) return false;
    out.assign((const char*)a + 1, n);
    return true;
}

static bool IsIdent(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!(isalnum((unsigned char)c) || c == '_' || c == '.' || c == '<' || c == '>' || c == ',')) return false;
    return isalpha((unsigned char)s[0]) || s[0] == '_';
}

// Returns 1 if `a` looks like a VMT and copies its class name into `out` (NUL-terminated), 0 if not, -1 on fault.
static int ProbeVmt(uintptr_t a, char* out) {
    __try {
        if (!InImage(a + VMT_SELF, 88) || RD32(a + VMT_SELF) != a) return 0;
        uint32_t np = RD32(a + VMT_NAME);
        if (!InImage(np, 1)) return 0;
        uint8_t n = *(uint8_t*)np;
        if (n == 0 || !InImage(np + 1, n)) return 0;
        memcpy(out, (const void*)(np + 1), n);
        out[n] = 0;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool ScanClasses() {
    BuildReadableMap();
    std::map<std::string, uintptr_t> found;
    char buf[260];
    for (uintptr_t a = g_base + 96; a + 4 <= g_base + g_size; a += 4) {
        if (!g_readable[(a - g_base) / 0x1000]) {
            a = ((a | 0xFFF) + 1) - 4;
            continue;
        }
        if (ProbeVmt(a, buf) != 1) continue;
        std::string name(buf);
        if (!IsIdent(name)) continue;
        if (!found.count(name)) found[name] = a;
    }
    if (!found.count("TfrmBuildCamp") || !found.count("TBuilderNation") || !found.count("TfrmSelectNation2"))
        return false;
    g_classes.swap(found);
    Log("RTTI: %u classes", (unsigned)g_classes.size());
    return true;
}

void* ClassVmt(const char* name) {
    auto it = g_classes.find(name);
    return it == g_classes.end() ? nullptr : (void*)it->second;
}

static uintptr_t ParentVmt(uintptr_t vmt) {
    uint32_t p = RD32(vmt + VMT_PARENT);
    if (!InImage(p)) return 0;
    uint32_t pv = RD32(p);
    return InImage(pv) ? pv : 0;
}

static uint32_t SafeVmtOf(void* obj) {
    __try {
        uintptr_t vmt = *(uint32_t*)obj;
        if (!InImage(vmt) || RD32(vmt + VMT_SELF) != vmt) return 0;
        return (uint32_t)vmt;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* ClassNameOf(void* obj) {
    // No thread_local here: implicit TLS in a late-loaded DLL makes this game exit at startup.
    static std::string s;
    uint32_t vmt = obj ? SafeVmtOf(obj) : 0;
    if (!vmt) return "?";
    if (!ReadShortString(RD32(vmt + VMT_NAME), s)) return "?";
    return s.c_str();
}

bool IsInstanceOf(void* obj, const char* cls) {
    if (!obj) return false;
    uintptr_t want = (uintptr_t)ClassVmt(cls);
    if (!want) return false;
    uintptr_t vmt = SafeVmtOf(obj);
    for (int depth = 0; vmt && depth < 64; depth++) {
        if (vmt == want) return true;
        vmt = ParentVmt(vmt);
    }
    return false;
}

static void* MethodInVmt(uintptr_t vmt, const char* name) {
    uint32_t mt = RD32(vmt + VMT_METHODS);
    if (!mt || !InImage(mt)) return nullptr;
    uint16_t cnt = RD16(mt);
    uintptr_t p = mt + 2;
    std::string nm;
    for (int i = 0; i < cnt; i++) {
        uint16_t len = RD16(p);
        if (ReadShortString(p + 6, nm) && nm == name) return (void*)(uintptr_t)RD32(p + 2);
        p += len;
    }
    uint16_t ex = RD16(p);
    p += 2;
    for (int i = 0; i < ex; i++) {
        uint32_t entry = RD32(p);
        if (InImage(entry) && ReadShortString(entry + 6, nm) && nm == name) return (void*)(uintptr_t)RD32(entry + 2);
        p += 8;
    }
    return nullptr;
}

void* Method(const char* cls, const char* name) {
    uintptr_t vmt = (uintptr_t)ClassVmt(cls);
    for (int depth = 0; vmt && depth < 64; depth++) {
        void* r = MethodInVmt(vmt, name);
        if (r) return r;
        vmt = ParentVmt(vmt);
    }
    return nullptr;
}

static int FieldInVmt(uintptr_t vmt, const char* name) {
    uint32_t ft = RD32(vmt + VMT_FIELDS);
    if (!ft || !InImage(ft)) return -1;
    uint16_t cnt = RD16(ft);
    uintptr_t p = ft + 6;
    std::string nm;
    for (int i = 0; i < cnt; i++) {
        uint32_t off = RD32(p);
        if (!ReadShortString(p + 6, nm)) return -1;
        if (nm == name) return (int)off;
        p += 7 + nm.size();
    }
    uint16_t ex = RD16(p);
    p += 2;
    for (int i = 0; i < ex; i++) {
        uint32_t off = RD32(p + 5);
        if (!ReadShortString(p + 9, nm)) return -1;
        if (nm == name) return (int)off;
        p += 10 + nm.size();
        p += RD16(p);  // attribute data (length includes itself)
    }
    return -1;
}

int Field(const char* cls, const char* name) {
    uintptr_t vmt = (uintptr_t)ClassVmt(cls);
    for (int depth = 0; vmt && depth < 64; depth++) {
        int r = FieldInVmt(vmt, name);
        if (r >= 0) return r;
        vmt = ParentVmt(vmt);
    }
    return -1;
}

extern "C" __declspec(naked) uint32_t __cdecl DelphiCallAsm(void* fn, uint32_t a, uint32_t d, uint32_t c, int n,
                                                            const uint32_t* s) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 24]
        mov esi, [ebp + 28]
        xor ebx, ebx
    pushloop:
        cmp ebx, ecx
        jge pushdone
        push dword ptr [esi + ebx * 4]
        inc ebx
        jmp pushloop
    pushdone:
        mov eax, [ebp + 12]
        mov edx, [ebp + 16]
        mov ecx, [ebp + 20]
        call dword ptr [ebp + 8]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

uint32_t Call(void* fn, uint32_t eax, uint32_t edx, uint32_t ecx, int nstack, const uint32_t* stack) {
    if (!fn) {
        Log("dl::Call: null function");
        return 0;
    }
    return DelphiCallAsm(fn, eax, edx, ecx, nstack, stack);
}

static int32_t SafeUStrLen(const void* p) {
    __try {
        int32_t n = *(const int32_t*)((const uint8_t*)p - 4);
        if (n < 0 || n > 1 << 20) return -1;
        volatile wchar_t last = n ? ((const wchar_t*)p)[n - 1] : 0;
        (void)last;
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

std::wstring ReadUStr(const void* p) {
    if (!p) return {};
    int32_t n = SafeUStrLen(p);
    if (n <= 0) return {};
    return std::wstring((const wchar_t*)p, (size_t)n);
}

ConstUStr::ConstUStr(const std::wstring& s) {
    size_t bytes = 12 + (s.size() + 1) * 2;
    m_mem = (uint8_t*)calloc(1, bytes + 4);
    *(uint16_t*)(m_mem + 0) = 1200;  // UTF-16 code page
    *(uint16_t*)(m_mem + 2) = 2;     // element size
    *(int32_t*)(m_mem + 4) = -1;     // refcount: literal
    *(int32_t*)(m_mem + 8) = (int32_t)s.size();
    m_chars = (wchar_t*)(m_mem + 12);
    memcpy(m_chars, s.c_str(), (s.size() + 1) * 2);
}

ConstUStr::~ConstUStr() { free(m_mem); }

void* ObjFromHwnd(HWND h) {
    static ATOM atom = 0;
    if (!atom) {
        DWORD tid = GetWindowThreadProcessId(h, nullptr);
        wchar_t name[64];
        swprintf(name, 64, L"ControlOfs%.8X%.8X", (unsigned)g_base, (unsigned)tid);
        atom = GlobalFindAtomW(name);
        if (!atom) return nullptr;
    }
    return (void*)GetPropW(h, MAKEINTATOM(atom));
}

int ListCount(void* list) {
    if (!list) return 0;
    return *(int*)((uint8_t*)list + 8);
}

void* ListItem(void* list, int i) {
    if (!list || i < 0 || i >= ListCount(list)) return nullptr;
    void** items = *(void***)((uint8_t*)list + 4);
    return items[i];
}

uint8_t* FindPattern(uint8_t* start, size_t len, const std::vector<int>& pat) {
    if (pat.empty() || len < pat.size()) return nullptr;
    for (size_t i = 0; i + pat.size() <= len; i++) {
        bool ok = true;
        for (size_t j = 0; j < pat.size(); j++) {
            if (pat[j] >= 0 && start[i + j] != (uint8_t)pat[j]) {
                ok = false;
                break;
            }
        }
        if (ok) return start + i;
    }
    return nullptr;
}

std::vector<uint8_t*> CallTargets(uint8_t* fn, size_t maxLen) {
    std::vector<uint8_t*> out;
    size_t off = 0;
    while (off < maxLen) {
        hde32s hs;
        unsigned int len = hde32_disasm(fn + off, &hs);
        if (len == 0 || (hs.flags & F_ERROR)) break;
        if (hs.opcode == 0xE8 && len == 5) out.push_back(fn + off + 5 + (int32_t)hs.imm.imm32);
        off += len;
    }
    return out;
}

}  // namespace dl
