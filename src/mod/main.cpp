#include "common.h"
#include "datafiles.h"
#include "delphi.h"
#include "economy.h"
#include "game.h"
#include "warfx.h"
#include "invasion.h"
#include "lockstep.h"
#include "net.h"
#include "session.h"
#include "ui.h"
#include <intrin.h>

void BridgeStart();  // bridge.cpp

static HINSTANCE g_inst;
static DWORD g_mainTid = 0;
static HWND g_dispatcher = nullptr;
static HHOOK g_msgHook = nullptr;
static std::mutex g_callMx;
static std::deque<std::function<void()>> g_calls;
static uint32_t g_dispBaseSp = 0, g_dispCurSp = 0;  // the dispatcher's outermost / current stack position

DWORD MainThreadId() { return g_mainTid; }
HWND DispatcherHwnd() { return g_dispatcher; }

// A nested message loop adds at least several hundred bytes of frames (the window procedure chain of the routine that
// runs it, the loop, DispatchMessage); the outermost loop reaches the dispatcher at the same depth every time.
bool AtOutermostDispatch() { return g_dispCurSp && g_dispCurSp + 256 >= g_dispBaseSp; }

std::string DispatchDepthText() {
    char b[48];
    snprintf(b, sizeof(b), "%ld/%08x", g_dispCurSp ? (long)(g_dispBaseSp - g_dispCurSp) : -1L, g_dispBaseSp);
    return b;
}

void RunOnMainThread(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(g_callMx);
        g_calls.push_back(std::move(fn));
    }
    if (g_dispatcher)
        PostMessageW(g_dispatcher, WM_MP_CALL, 0, 0);
    else if (g_mainTid)
        PostThreadMessageW(g_mainTid, WM_NULL, 0, 0);
}

static void DrainCalls() {
    while (true) {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lk(g_callMx);
            if (g_calls.empty()) return;
            fn = std::move(g_calls.front());
            g_calls.pop_front();
        }
        try {
            fn();
        } catch (...) {
            Log("exception in main-thread call");
        }
    }
}

static LRESULT CALLBACK DispatcherProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // The dispatcher's stack depth: the outermost message loop calls it at the highest address (it is reached the same
    // way every time); a nested loop (a modal dialog, a menu, ProcessMessages) calls it deeper.
    uint32_t sp = (uint32_t)(uintptr_t)_AddressOfReturnAddress();
    if (sp > g_dispBaseSp) g_dispBaseSp = sp;
    struct Depth {
        uint32_t prev;
        ~Depth() { g_dispCurSp = prev; }
    } depth{g_dispCurSp};
    g_dispCurSp = sp;
    try {
        lockstep::PruneStale();  // battle routines an exception unwound (their hook frames never closed)
        switch (m) {
            case WM_TIMER:
                mp::Tick();
                ui::Tick();
                return 0;
            case WM_MP_NET:
                mp::OnNet();
                return 0;
            case WM_MP_CALL:
                DrainCalls();
                return 0;
        }
    } catch (...) {
        Log("exception in dispatcher (msg %u)", m);
    }
    return DefWindowProcW(h, m, w, l);
}

static void CreateDispatcher() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DispatcherProc;
    wc.hInstance = g_inst;
    wc.lpszClassName = L"RTW3MP_Dispatcher";
    RegisterClassW(&wc);
    g_dispatcher = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
    SetTimer(g_dispatcher, 1, 250, nullptr);
    ui::Init(g_inst);
    mp::Init();
    Log("dispatcher ready on main thread %lu", GetCurrentThreadId());
    lockstep::ApplyTestSwitches("the mod's start");  // (TEST ONLY switches; nothing without them)
    if (!g_calls.empty()) PostMessageW(g_dispatcher, WM_MP_CALL, 0, 0);
}

static LRESULT CALLBACK GetMsgProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && w == PM_REMOVE) {
        MSG* msg = (MSG*)l;
        if (!g_dispatcher && GetCurrentThreadId() == g_mainTid) CreateDispatcher();
        if (msg->message == WM_KEYDOWN && msg->wParam == 'M' && (GetKeyState(VK_CONTROL) & 0x8000) &&
            (GetKeyState(VK_SHIFT) & 0x8000)) {
            ui::ToggleMpWindow();
            msg->message = WM_NULL;
        } else if (ui::PreTranslate(msg)) {
            msg->message = WM_NULL;
        }
    }
    return CallNextHookEx(g_msgHook, code, w, l);
}

static BOOL CALLBACK FindAppWindow(HWND h, LPARAM l) {
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    if (wcscmp(cls, L"TApplication") == 0) {
        *(DWORD*)l = tid;
        return FALSE;
    }
    return TRUE;
}

// RTW3MP_DIAG: an access violation in the game's code (the first 8) with its registers, the objects they point to and
// the game's return addresses on the stack (RVAs), to find where a bad pointer came from.
static bool DiagGameCode(uint32_t a) { return dl::g_base && a >= dl::g_base + 0x1000 && a < dl::g_base + dl::g_size; }

static bool DiagAfterCall(uint32_t a) {  // a return address: a call instruction ends right before it
    const uint8_t* p = (const uint8_t*)(uintptr_t)a;
    return p[-5] == 0xE8 || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) ||
           (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-4] == 0xFF && (p[-3] & 0x38) == 0x10) ||
           (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10);
}

static void DiagAccessViolation(EXCEPTION_POINTERS* ep) {
    CONTEXT* c = ep->ContextRecord;
    uint32_t base = (uint32_t)dl::g_base;
    char buf[1400];
    int n = 0;
    __try {
        n += snprintf(buf + n, sizeof(buf) - n, "diag: exception %08lx at rva %x (%s %08lx, thread %lu%s): eax=%08lx "
                      "ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
                      ep->ExceptionRecord->ExceptionCode, (uint32_t)c->Eip - base,
                      ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                      (DWORD)ep->ExceptionRecord->ExceptionInformation[1], GetCurrentThreadId(),
                      GetCurrentThreadId() == g_mainTid ? ", main" : "", c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi,
                      c->Edi, c->Ebp, c->Esp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("%s", buf);
    const char* names[] = {"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp"};
    DWORD regs[] = {c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp};
    for (int r = 0; r < 7; r++) {
        if (regs[r] < 0x10000) continue;
        n = 0;
        __try {
            const char* cls = dl::ClassNameOf((void*)(uintptr_t)regs[r]);
            n += snprintf(buf + n, sizeof(buf) - n, "diag:   %s %08lx (%s):", names[r], regs[r], cls);
            const DWORD* p = (const DWORD*)(uintptr_t)(regs[r] - 16);
            for (int i = 0; i < 16; i++) n += snprintf(buf + n, sizeof(buf) - n, " %08lx", p[i]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            n += snprintf(buf + n, sizeof(buf) - n, " (unreadable)");
        }
        Log("%s", buf);
    }
    n = 0;
    int found = 0;
    __try {
        NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
        uint32_t top = (uint32_t)(uintptr_t)tib->StackBase, sp = c->Esp;
        n += snprintf(buf + n, sizeof(buf) - n, "diag:   stack (return rvas from esp+0):");
        for (uint32_t a = sp; a + 4 <= top && a < sp + 0x6000 && found < 60; a += 4) {
            uint32_t v = *(const uint32_t*)(uintptr_t)a;
            if (!DiagGameCode(v) || !DiagAfterCall(v)) continue;
            n += snprintf(buf + n, sizeof(buf) - n, " +%x:%x", a - sp, v - base);
            found++;
            if (n > (int)sizeof(buf) - 40) {
                Log("%s", buf);
                n = snprintf(buf, sizeof(buf), "diag:   stack (more):");
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n += snprintf(buf + n, sizeof(buf) - n, " (stack unreadable)");
    }
    Log("%s", buf);
}

static LONG CALLBACK DiagVeh(EXCEPTION_POINTERS* ep) {
    static volatile LONG count = 0, avs = 0;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x40010006 || code == 0x4001000A || code == 0x406D1388) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t at = (uintptr_t)ep->ExceptionRecord->ExceptionAddress, me = (uintptr_t)g_inst;
    bool mine = me && at >= me &&
                at < me + ((IMAGE_NT_HEADERS32*)(me + ((IMAGE_DOS_HEADER*)me)->e_lfanew))->OptionalHeader.SizeOfImage;
    if (code == EXCEPTION_ACCESS_VIOLATION && mine) return EXCEPTION_CONTINUE_SEARCH;  // the mod's guarded reads
    if (InterlockedIncrement(&count) <= 40) {
        const char* cls = "";
        if (code == 0x0EEDFADE && ep->ExceptionRecord->NumberParameters >= 2)  // a Delphi exception: its object
            cls = dl::ClassNameOf((void*)ep->ExceptionRecord->ExceptionInformation[1]);
        Log("exception %08lx at %p (thread %lu) info %p %p %s", ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress, GetCurrentThreadId(),
            (void*)ep->ExceptionRecord->ExceptionInformation[0], (void*)ep->ExceptionRecord->ExceptionInformation[1],
            cls);
    }
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_INT_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
         code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION) &&
        DiagGameCode((uint32_t)ep->ContextRecord->Eip) && InterlockedIncrement(&avs) <= 8)
        DiagAccessViolation(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI InitThread(LPVOID) {
    LogInit(ModDataDir());
    Log("RTW3MP %s starting (protocol %d)", MP_MOD_VERSION, MP_PROTOCOL);
    datafiles::PocFileLog();  // (TEST ONLY: RTW3MP_TEST_FILELOG=1)
    char diag[8];
    if (GetEnvironmentVariableA("RTW3MP_DIAG", diag, sizeof(diag)) > 0) AddVectoredExceptionHandler(1, DiagVeh);
    if (GetEnvironmentVariableA("RTW3MP_NOINIT", diag, sizeof(diag)) > 0) {
        Log("RTW3MP_NOINIT set: idle");
        return 0;
    }
    if (!dl::InitImage()) return 0;
    // Touch nothing in the game image until the game's own code runs: the Steam DRM stub is still
    // unpacking at this point. The VCL application window only exists once the real program started.
    DWORD start = GetTickCount();
    while (!g_mainTid) {
        EnumWindows(FindAppWindow, (LPARAM)&g_mainTid);
        if (!g_mainTid) {
            if (GetTickCount() - start > 300000) {
                Log("game window never appeared; mod disabled");
                return 0;
            }
            Sleep(200);
        }
    }
    Log("game is running (main thread %lu), reading RTTI", g_mainTid);
    char delay[16];
    if (GetEnvironmentVariableA("RTW3MP_DELAY", delay, sizeof(delay)) > 0) {
        Log("delay %d ms", atoi(delay));
        Sleep(atoi(delay));
        Log("delay done");
    }
    start = GetTickCount();
    while (!dl::ScanClasses()) {
        if (GetTickCount() - start > 60000) {
            Log("game classes not found; mod disabled");
            return 0;
        }
        Sleep(250);
    }
    if (!game::Resolve()) {
        Log("this game version is not supported by RTW3MP %s; mod disabled", MP_MOD_VERSION);
        return 0;
    }
    if (!game::InstallHooks()) Log("some hooks failed; multiplayer may not work");
    economy::Install();
    warfx::Install();
    inv::Install();
    g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, g_inst, g_mainTid);
    PostThreadMessageW(g_mainTid, WM_NULL, 0, 0);
    Log("init done: build %s, main thread %lu, msg hook %p", game::BuildFingerprint().c_str(), g_mainTid, g_msgHook);
    BridgeStart();
    // Windows removes a hook when the thread that installed it exits, so this thread stays alive.
    while (true) {
        Sleep(1000);
        if (!g_dispatcher) PostThreadMessageW(g_mainTid, WM_NULL, 0, 0);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_inst = inst;
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
