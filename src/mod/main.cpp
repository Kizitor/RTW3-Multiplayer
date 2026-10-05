#include "common.h"
#include "delphi.h"
#include "game.h"
#include "net.h"
#include "session.h"
#include "ui.h"

void BridgeStart();  // bridge.cpp

static HINSTANCE g_inst;
static DWORD g_mainTid = 0;
static HWND g_dispatcher = nullptr;
static HHOOK g_msgHook = nullptr;
static std::mutex g_callMx;
static std::deque<std::function<void()>> g_calls;

DWORD MainThreadId() { return g_mainTid; }
HWND DispatcherHwnd() { return g_dispatcher; }

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
    try {
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

static LONG CALLBACK DiagVeh(EXCEPTION_POINTERS* ep) {
    static volatile LONG count = 0;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x40010006 || code == 0x4001000A || code == 0x406D1388) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&count) <= 20) {
        Log("exception %08lx at %p (thread %lu) info %p %p", ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress, GetCurrentThreadId(),
            (void*)ep->ExceptionRecord->ExceptionInformation[0], (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI InitThread(LPVOID) {
    LogInit(ModDataDir());
    Log("RTW3MP %s starting (protocol %d)", MP_MOD_VERSION, MP_PROTOCOL);
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
