// version.dll proxy for Rule the Waves 3.
// Forwards every export to the real system version.dll and, only inside RTW3.exe,
// loads RTW3MP.dll from the same folder on a separate thread (after the loader lock is released).
// The Steam DRM stub is left untouched: the mod waits until the game has unpacked itself.
#include <windows.h>
#include <stdarg.h>

static HMODULE g_real = nullptr;
extern "C" FARPROC g_fn[17] = {};
static const char* g_names[17] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
    "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"};

static void LoadRealVersionDll() {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 20) return;
    lstrcatW(path, L"\\version.dll");
    g_real = LoadLibraryW(path);
    if (!g_real) return;
    for (int i = 0; i < 17; i++) g_fn[i] = GetProcAddress(g_real, g_names[i]);
}

#define FWD(i, name) \
    extern "C" __declspec(naked) void Proxy_##name() { __asm { jmp dword ptr [g_fn + i * 4] } }

FWD(0, GetFileVersionInfoA)
FWD(1, GetFileVersionInfoByHandle)
FWD(2, GetFileVersionInfoExA)
FWD(3, GetFileVersionInfoExW)
FWD(4, GetFileVersionInfoSizeA)
FWD(5, GetFileVersionInfoSizeExA)
FWD(6, GetFileVersionInfoSizeExW)
FWD(7, GetFileVersionInfoSizeW)
FWD(8, GetFileVersionInfoW)
FWD(9, VerFindFileA)
FWD(10, VerFindFileW)
FWD(11, VerInstallFileA)
FWD(12, VerInstallFileW)
FWD(13, VerLanguageNameA)
FWD(14, VerLanguageNameW)
FWD(15, VerQueryValueA)
FWD(16, VerQueryValueW)

static wchar_t g_dir[MAX_PATH];
static HANDLE g_diagFile = INVALID_HANDLE_VALUE;

static void Diag(const char* fmt, ...) {
    if (g_diagFile == INVALID_HANDLE_VALUE) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    char line[600];
    int m = wsprintfA(line, "%lu [%lu] %s\r\n", GetTickCount(), GetCurrentThreadId(), buf);
    DWORD put;
    WriteFile(g_diagFile, line, m, &put, nullptr);
    (void)n;
}

static LONG CALLBACK DiagVeh(EXCEPTION_POINTERS* ep) {
    static volatile LONG count = 0;
    if (InterlockedIncrement(&count) > 50) return EXCEPTION_CONTINUE_SEARCH;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    // For MSVC C++ exceptions, parameter 2 is the ThrowInfo inside the throwing module.
    const void* where = r->ExceptionCode == 0xE06D7363 && r->NumberParameters >= 3 ? (const void*)r->ExceptionInformation[2]
                                                                                    : r->ExceptionAddress;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)where, &mod))
        GetModuleFileNameA(mod, name, MAX_PATH);
    Diag("exception %08lx at %p module %s", r->ExceptionCode, r->ExceptionAddress, name);
    return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL CALLBACK FindVclAppWindow(HWND h, LPARAM l) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    wchar_t cls[32];
    GetClassNameW(h, cls, 32);
    if (lstrcmpW(cls, L"TApplication") == 0) {
        *(bool*)l = true;
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI LoadModThread(LPVOID) {
    wchar_t path[MAX_PATH];
    lstrcpyW(path, g_dir);
    lstrcatW(path, L"RTW3MP.dll");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return 0;
    // Loading a DLL while the Steam DRM stub is still unpacking makes the game quit silently, so wait
    // until the real program has created its VCL application window.
    bool ready = false;
    for (int i = 0; i < 3000 && !ready; i++) {
        EnumWindows(FindVclAppWindow, (LPARAM)&ready);
        if (!ready) Sleep(100);
    }
    Diag("app window ready=%d", (int)ready);
    if (ready) {
        HMODULE m = LoadLibraryW(path);
        Diag("LoadLibrary RTW3MP -> %p (err %lu)", m, GetLastError());
    }
    return 0;
}

static bool IsGameProcess() {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const wchar_t* name = exe;
    for (const wchar_t* p = exe; *p; p++)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    lstrcpynW(g_dir, exe, (int)(name - exe) + 1);
    return lstrcmpiW(name, L"RTW3.exe") == 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        LoadRealVersionDll();
        if (IsGameProcess()) {
            if (GetEnvironmentVariableW(L"RTW3MP_DIAG", nullptr, 0) > 0) {
                wchar_t p[MAX_PATH];
                lstrcpyW(p, g_dir);
                lstrcatW(p, L"rtw3mp_loader.log");
                g_diagFile = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
                AddVectoredExceptionHandler(1, DiagVeh);
                Diag("proxy attached");
            }
            HANDLE t = CreateThread(nullptr, 0, LoadModThread, nullptr, 0, nullptr);
            if (t) CloseHandle(t);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        Diag("proxy detach (process exiting)");
    }
    return TRUE;
}
