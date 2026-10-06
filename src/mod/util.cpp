#include "common.h"
#include <shlobj.h>
#include <cstdarg>

static CRITICAL_SECTION g_logCs;
static volatile LONG g_logCsInit = 0;
static volatile DWORD g_logOwner = 0;
static FILE* g_log = nullptr;

static void LogLockInit() {
    if (InterlockedCompareExchange(&g_logCsInit, 1, 0) == 0) InitializeCriticalSection(&g_logCs);
}

void LogInit(const std::wstring& dir) {
    LogLockInit();
    EnsureDir(dir);
    wchar_t tag[32] = {};
    GetEnvironmentVariableW(L"RTW3MP_LOG_TAG", tag, 32);
    std::wstring base = tag[0] ? L"rtw3mp_" + std::wstring(tag) : L"rtw3mp";
    std::wstring path = dir + base + L".log";
    std::wstring old = dir + base + L".prev.log";
    DeleteFileW(old.c_str());
    MoveFileW(path.c_str(), old.c_str());
    g_log = _wfopen(path.c_str(), L"wb");
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
    // Last resort: write the file in place (works when the other program allows shared writing).
    DWORD direct = 0;
    if (WriteWhole(path, data, direct)) {
        DeleteFileW(tmp.c_str());
        Log("write %s: replaced in place after error %lu", W2U(path).c_str(), err);
        return true;
    }
    DeleteFileW(tmp.c_str());
    Log("write %s FAILED: error %lu (in place: %lu)", W2U(path).c_str(), err, direct);
    SetLastError(err);
    return false;
}

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
