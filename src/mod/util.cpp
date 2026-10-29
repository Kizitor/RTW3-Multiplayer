#include "common.h"
#include <shlobj.h>
#include <cstdarg>
#include <cwctype>
#include "../../third_party/minhook/include/MinHook.h"

static CRITICAL_SECTION g_logCs;
static volatile LONG g_logCsInit = 0;
static volatile DWORD g_logOwner = 0;
static FILE* g_log = nullptr;
static std::wstring g_logPath, g_logPrev;
static unsigned long long g_logBytes = 0;
// 0.7.1: a log is at most this long; then it becomes the .prev.log and a new one starts (a session of many months and
// live battles could otherwise fill the disk: at most two files of this size per log tag).
static const unsigned long long kLogMax = 32ull * 1024 * 1024;

static void LogLockInit() {
    if (InterlockedCompareExchange(&g_logCsInit, 1, 0) == 0) InitializeCriticalSection(&g_logCs);
}

void LogInit(const std::wstring& dir) {
    LogLockInit();
    EnsureDir(dir);
    wchar_t tag[32] = {};
    GetEnvironmentVariableW(L"RTW3MP_LOG_TAG", tag, 32);
    std::wstring base = tag[0] ? L"rtw3mp_" + std::wstring(tag) : L"rtw3mp";
    g_logPath = dir + base + L".log";
    g_logPrev = dir + base + L".prev.log";
    DeleteFileW(g_logPrev.c_str());
    MoveFileW(g_logPath.c_str(), g_logPrev.c_str());
    g_log = _wfopen(g_logPath.c_str(), L"wb");
}

// (inside the log's lock)
static void LogRotate() {
    fclose(g_log);
    DeleteFileW(g_logPrev.c_str());
    MoveFileW(g_logPath.c_str(), g_logPrev.c_str());
    g_log = _wfopen(g_logPath.c_str(), L"wb");
    g_logBytes = 0;
    if (g_log) {
        fputs("(the log reached its size limit: the earlier lines are in the .prev.log)\r\n", g_log);
        fflush(g_log);
    }
}

void Log(const char* fmt, ...) {
    if (!g_logCsInit) return;
    DWORD me = GetCurrentThreadId();
    if (g_logOwner == me) return;  // re-entered from an exception handler while writing: drop
    char buf[4096];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d [%5lu] ", st.wHour, st.wMinute, st.wSecond,
                     st.wMilliseconds, me);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_logCs);
    g_logOwner = me;
    if (g_log) {
        fputs(buf, g_log);
        fputs("\r\n", g_log);
        fflush(g_log);
        g_logBytes += strlen(buf) + 2;
        if (g_logBytes > kLogMax) LogRotate();
    }
    g_logOwner = 0;
    LeaveCriticalSection(&g_logCs);
}

std::string W2U(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring U2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t p = 0;
    while (true) {
        size_t q = s.find(sep, p);
        if (q == std::string::npos) {
            out.push_back(s.substr(p));
            break;
        }
        out.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    return out;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 256 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(h, &out[0], (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    return ok && got == out.size();
}

// Antivirus scanners, OneDrive or search indexing can hold a freshly written save file open for a moment, which
// makes opening or replacing it fail with a sharing error. Such errors are retried for a few seconds.
static bool TransientFileError(DWORD e) {
    return e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION || e == ERROR_ACCESS_DENIED ||
           e == ERROR_USER_MAPPED_FILE;
}

static bool WriteWhole(const std::wstring& path, const std::string& data, DWORD& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = GetLastError();
        return false;
    }
    DWORD put = 0;
    BOOL ok = data.empty() ? TRUE : WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr);
    err = ok ? 0 : GetLastError();
    CloseHandle(h);
    if (ok && put != data.size()) err = ERROR_WRITE_FAULT;
    return ok && put == data.size();
}

static bool DiskFullError(DWORD e) { return e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL; }
static thread_local bool t_writeDiskFull = false;  // sticky until ResetWriteFailed
bool WriteFailedDiskFull() { return t_writeDiskFull; }
void ResetWriteFailed() { t_writeDiskFull = false; }

bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".mptmp";
    DWORD err = 0;
    const DWORD kDeadline = GetTickCount() + 3000;
    for (int attempt = 0;; attempt++) {
        if (WriteWhole(tmp, data, err)) {
            if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                if (attempt) Log("write %s: succeeded after %d retries", W2U(path).c_str(), attempt);
                return true;
            }
            err = GetLastError();
        }
        if (!TransientFileError(err) || (int)(GetTickCount() - kDeadline) >= 0) break;
        Sleep(attempt < 5 ? 50 : 200);
    }
    // A full disk (0.7.1): the file stays as it was (writing it in place would cut it short first).
    if (DiskFullError(err)) {
        DeleteFileW(tmp.c_str());
        t_writeDiskFull = true;
        Log("write %s FAILED: the disk is full (error %lu; %lld MB free); the file is left as it was",
            W2U(path).c_str(), err, FreeSpaceMB(path));
        SetLastError(err);
        return false;
    }
    // Last resort: write the file in place (works when the other program allows shared writing).
    DWORD direct = 0;
    if (WriteWhole(path, data, direct)) {
        DeleteFileW(tmp.c_str());
        Log("write %s: replaced in place after error %lu", W2U(path).c_str(), err);
        return true;
    }
    DeleteFileW(tmp.c_str());
    if (DiskFullError(direct)) t_writeDiskFull = true;
    Log("write %s FAILED: error %lu (in place: %lu)", W2U(path).c_str(), err, direct);
    SetLastError(err);
    return false;
}

// ---------------------------------------------------------------------------------------------
// 0.7.1: the disk.
static volatile long long g_testFreeMB = -1;
void SetTestFreeSpaceMB(long long mb) { g_testFreeMB = mb; }

long long FreeSpaceMB(const std::wstring& dir) {
    if (g_testFreeMB >= 0) return g_testFreeMB;
    if (dir.empty()) return -1;
    std::wstring d = dir;
    size_t p = d.find_last_of(L"\\/");
    if (p != std::wstring::npos && p + 1 < d.size() && d.find(L'.', p) != std::wstring::npos) d = d.substr(0, p + 1);
    ULARGE_INTEGER avail = {};
    if (!GetDiskFreeSpaceExW(d.c_str(), &avail, nullptr, nullptr)) return -1;
    return (long long)(avail.QuadPart / (1024 * 1024));
}

// TEST ONLY: a full disk for the files under the game's Save folder. WriteFile (kernel32) is hooked on the first `on`;
// while on, a write to such a file fails with ERROR_DISK_FULL and writes nothing.
namespace {
typedef BOOL(WINAPI* WriteFile_t)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
WriteFile_t o_WriteFile = nullptr;
volatile bool g_diskFull = false;
std::wstring g_saveRoot;  // "\\?\...\My Games\Rule the Waves 3\Save\" (upper case), as GetFinalPathNameByHandleW gives it

bool UnderSave(HANDLE h) {
    wchar_t buf[1024];
    DWORD n = GetFinalPathNameByHandleW(h, buf, 1024, FILE_NAME_NORMALIZED);
    if (n == 0 || n >= 1024) return false;
    std::wstring s(buf, n);
    for (auto& c : s) c = towupper(c);
    return s.compare(0, g_saveRoot.size(), g_saveRoot) == 0;
}

BOOL WINAPI d_WriteFile(HANDLE h, LPCVOID data, DWORD n, LPDWORD written, LPOVERLAPPED ov) {
    if (g_diskFull && n > 0 && GetFileType(h) == FILE_TYPE_DISK && UnderSave(h)) {
        if (written) *written = 0;
        SetLastError(ERROR_DISK_FULL);
        return FALSE;
    }
    return o_WriteFile(h, data, n, written, ov);
}
}  // namespace

bool SetTestDiskFull(bool on) {
    if (on && !o_WriteFile) {
        std::wstring d = ModDataDir();  // "...\My Games\Rule the Waves 3\RTW3MP\"
        std::wstring root = d.substr(0, d.size() - 7) + L"Save\\";
        wchar_t full[1024];
        HANDLE h = CreateFileW(root.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        DWORD n = h != INVALID_HANDLE_VALUE ? GetFinalPathNameByHandleW(h, full, 1024, FILE_NAME_NORMALIZED) : 0;
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (n == 0 || n >= 1024) return false;
        g_saveRoot.assign(full, n);
        if (g_saveRoot.back() != L'\\') g_saveRoot += L'\\';
        for (auto& c : g_saveRoot) c = towupper(c);
        void* target = nullptr;
        MH_STATUS st = MH_CreateHookApiEx(L"kernel32", "WriteFile", (void*)&d_WriteFile, (void**)&o_WriteFile, &target);
        if (st == MH_OK) st = MH_EnableHook(target);
        Log("TEST: the full-disk simulation for %s: %s", W2U(g_saveRoot).c_str(), st == MH_OK ? "ready" : "FAILED");
        if (st != MH_OK) {
            o_WriteFile = nullptr;
            return false;
        }
    }
    g_diskFull = on;
    Log("TEST: the full-disk simulation is %s", on ? "ON (writes under Save\\ fail with ERROR_DISK_FULL)" : "off");
    return true;
}

bool TestDiskFull() { return g_diskFull; }

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void EnsureDir(const std::wstring& path) {
    std::wstring p = path;
    for (size_t i = 3; i < p.size(); i++) {
        if (p[i] == L'\\' || p[i] == L'/') {
            std::wstring sub = p.substr(0, i);
            CreateDirectoryW(sub.c_str(), nullptr);
        }
    }
    CreateDirectoryW(p.c_str(), nullptr);
}

std::wstring ModDataDir() {
    wchar_t docs[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs);
    std::wstring d = std::wstring(docs) + L"\\My Games\\Rule the Waves 3\\RTW3MP\\";
    EnsureDir(d);
    return d;
}

std::wstring GameDir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring s = exe;
    size_t p = s.find_last_of(L"\\/");
    return p == std::wstring::npos ? L".\\" : s.substr(0, p + 1);
}

std::string KVEncode(const KV& kv) {
    std::string s;
    for (auto& it : kv) {
        std::string v = it.second;
        for (auto& c : v)
            if (c == '\n' || c == '\r') c = ' ';
        s += it.first + "=" + v + "\n";
    }
    return s;
}

KV KVDecode(const std::string& s) {
    KV kv;
    for (auto& line : Split(s, '\n')) {
        size_t e = line.find('=');
        if (e == std::string::npos) continue;
        std::string v = line.substr(e + 1);
        if (!v.empty() && v.back() == '\r') v.pop_back();
        kv[line.substr(0, e)] = v;
    }
    return kv;
}

int KVInt(const KV& kv, const char* key, int def) {
    auto it = kv.find(key);
    if (it == kv.end() || it->second.empty()) return def;
    return atoi(it->second.c_str());
}

std::string KVStr(const KV& kv, const char* key, const std::string& def) {
    auto it = kv.find(key);
    return it == kv.end() ? def : it->second;
}

int AnnouncedLsLevel() {
    static volatile LONG level = 0;
    if (level == 0) {
        char buf[16] = {};
        int v = GetEnvironmentVariableA("RTW3MP_TEST_LS_LEVEL", buf, sizeof(buf)) > 0 ? atoi(buf) : 0;
        InterlockedExchange(&level, v > 0 ? v : MP_LS_LEVEL);
    }
    return (int)level;
}
