// The game data files a live battle depends on (engine level 16).
//
// The battle's load and simulation read the game's own data (the battle's save comes from A): the `Data` folder (the
// probe RTW3MP_TEST_FILELOG saw a battle load read Gundata, HorPen, VerPen, Torpedodata, BasicData, MapData and the
// nations' WarInfo .dat, the WorldRTW, RTWShallow and NoGo map layers and coastal battery designs in Data\IDes; the
// minutes read no game file). Two PCs whose files differ load or play the same battle differently, and a live battle
// between them would only resync until it is given up. Every file of `Data` but its Graphics folder (about 1400 files,
// 40 MB) is hashed once on a background thread when a session starts. A's start of a live battle carries the combined hash; a B whose hash differs refuses the
// battle with its per-file list, A names the differing files to both players and fights the battle against the
// computer (lockstep.cpp).
#include "datafiles.h"

#include <algorithm>
#include <atomic>
#include <thread>

#include "common.h"
#include "../../third_party/minhook/include/MinHook.h"

namespace datafiles {
namespace {

struct Entry {
    std::string name;   // relative to the game folder, lower case, backslashes ("data\\ides\\foo.tdf")
    std::string shown;  // as on disk ("Data\\IDes\\Foo.tdf")
    uint64_t hash = 0;
};

const wchar_t* const kRoots[] = {L"Data"};
// Not hashed: the drawings of hulls, turrets, equipment and aircraft (the display only; a graphics mod keeps live battles).
const wchar_t* const kSkip[] = {L"Data\\Graphics"};

std::vector<Entry> g_files, g_scanned;  // (g_scanned: as read, for `ls testdatafile off`)
uint64_t g_combined = 0, g_scannedCombined = 0;
std::atomic<bool> g_done{false}, g_started{false};
HANDLE g_doneEvent = nullptr;

std::wstring GameDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring p(buf, n);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}

std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::string Lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

uint64_t Fnv(uint64_t h, const void* p, size_t n) {
    auto b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

bool HashFile(const std::wstring& path, uint64_t& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    uint64_t h = 1469598103934665603ull;
    std::vector<uint8_t> buf(1 << 16);
    DWORD got = 0;
    bool ok = true;
    while ((ok = ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr) != 0) && got) h = Fnv(h, buf.data(), got);
    CloseHandle(f);
    out = h;
    return ok;
}

void Walk(const std::wstring& dir, const std::wstring& rel, std::vector<Entry>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring r = rel + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            bool skip = false;
            for (auto s : kSkip) skip = skip || !_wcsicmp(r.c_str(), s);
            if (!skip) Walk(dir + L"\\" + n, r, out);
            continue;
        }
        Entry e;
        e.shown = Narrow(r);
        e.name = Lower(e.shown);
        if (!HashFile(dir + L"\\" + n, e.hash)) e.hash = 0;  // unreadable: hashed as 0 (still named if it differs)
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t p = 0;
    while (p <= s.size()) {
        size_t e = s.find(sep, p);
        if (e == std::string::npos) e = s.size();
        if (e > p) v.push_back(s.substr(p, e - p));
        p = e + 1;
    }
    return v;
}

// TEST ONLY: these names hash as if their content differed on this copy (names not in the list: extra files).
void ApplyTest(std::vector<Entry>& files, const std::string& list) {
    for (auto& n : Split(list, ';')) {
        std::string ln = Lower(n);
        bool hit = false;
        for (auto& e : files)
            if (e.name == ln) {
                e.hash ^= 0x5bd1e9955bd1e995ull;
                hit = true;
            }
        if (!hit) {
            Entry e;
            e.shown = n;
            e.name = ln;
            e.hash = 1;
            files.push_back(e);
        }
    }
}

// Sorts the list by name and returns the combined hash.
uint64_t Seal(std::vector<Entry>& files) {
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    uint64_t c = 1469598103934665603ull;
    for (auto& e : files) {
        c = Fnv(c, e.name.c_str(), e.name.size() + 1);
        c = Fnv(c, &e.hash, 8);
    }
    return c;
}

void Scan() {
    DWORD t0 = GetTickCount();
    std::wstring game = GameDir();
    std::vector<Entry> files;
    for (auto r : kRoots) Walk(game + L"\\" + r, r, files);
    char test[512] = {0};
    if (GetEnvironmentVariableA("RTW3MP_TEST_DATAFILE", test, sizeof(test)) > 0 && test[0]) {
        ApplyTest(files, test);
        Log("TEST: RTW3MP_TEST_DATAFILE=%s: these game data files hash as if they differed on this copy", test);
    }
    g_combined = g_scannedCombined = Seal(files);
    g_scanned = files;
    g_files.swap(files);
    g_done = true;
    SetEvent(g_doneEvent);
    Log("data files: %d files of Data (but Data\\Graphics) hashed in %lu ms: %016llx", (int)g_files.size(),
        GetTickCount() - t0, (unsigned long long)g_combined);
}

}  // namespace

void StartScan() {
    if (g_started.exchange(true)) return;
    g_doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread([] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        Scan();
    }).detach();
}

bool Ready(unsigned waitMs) {
    if (g_done) return true;
    if (!g_started || !waitMs || !g_doneEvent) return false;
    WaitForSingleObject(g_doneEvent, waitMs);
    return g_done;
}

std::string Combined() {
    if (!g_done) return "";
    char b[24];
    snprintf(b, sizeof(b), "%016llx", (unsigned long long)g_combined);
    return b;
}

int FileCount() { return g_done ? (int)g_files.size() : 0; }

void SetTestDiff(const std::string& names) {
    if (!g_done || names.empty()) return;
    if (names == "off") {
        g_files = g_scanned;
        g_combined = g_scannedCombined;
        Log("TEST: data files: as scanned again: %016llx", (unsigned long long)g_combined);
        return;
    }
    ApplyTest(g_files, names);
    g_combined = Seal(g_files);
    Log("TEST: data files: %s hash as if they differed on this copy: %016llx", names.c_str(),
        (unsigned long long)g_combined);
}

std::string Encoded() {
    if (!g_done) return "";
    std::string s;
    s.reserve(g_files.size() * 48);
    char h[24];
    for (auto& e : g_files) {
        snprintf(h, sizeof(h), ":%016llx|", (unsigned long long)e.hash);
        s += e.shown + h;
    }
    return s;
}

std::vector<std::string> Differ(const std::string& theirs) {
    std::vector<std::string> out;
    if (!g_done) return out;
    std::map<std::string, std::pair<std::string, uint64_t>> other;  // name -> (shown, hash)
    for (auto& line : Split(theirs, '|')) {
        size_t c = line.rfind(':');
        if (c == std::string::npos) continue;
        other[Lower(line.substr(0, c))] = {line.substr(0, c), strtoull(line.c_str() + c + 1, nullptr, 16)};
    }
    for (auto& e : g_files) {
        auto it = other.find(e.name);
        if (it == other.end()) {
            out.push_back(e.shown + " (only on one PC)");
            continue;
        }
        if (it->second.second != e.hash) out.push_back(e.shown);
        other.erase(it);
    }
    for (auto& o : other) out.push_back(o.second.first + " (only on one PC)");
    return out;
}

std::string NamesText(const std::vector<std::string>& names, size_t shown) {
    std::string s;
    for (size_t i = 0; i < names.size() && i < shown; i++) s += (i ? ", " : "") + names[i];
    if (names.size() > shown) s += " (+" + std::to_string(names.size() - shown) + " more)";
    return s;
}

// ---------------------------------------------------------------------------------------------
// TEST ONLY: the file-open probe (pocfilelog).
namespace {
std::mutex g_pocMu;
std::set<std::wstring> g_pocSeen;
std::string g_phase = "startup";
thread_local bool t_inPoc = false;
typedef HANDLE(WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
CreateFileW_t o_CreateFileW = nullptr;

HANDLE WINAPI d_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                            DWORD flags, HANDLE tmpl) {
    HANDLE h = o_CreateFileW(name, access, share, sa, disp, flags, tmpl);
    if (!t_inPoc && name && !(access & GENERIC_WRITE) && h != INVALID_HANDLE_VALUE) {
        t_inPoc = true;
        std::wstring n = name;
        std::string phase;
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lk(g_pocMu);
            fresh = g_pocSeen.insert(n).second;
            phase = g_phase;
        }
        if (fresh) Log("pocfilelog: [%s] %s", phase.c_str(), Narrow(n).c_str());
        t_inPoc = false;
    }
    return h;
}
}  // namespace

void PocFileLog() {
    char v[8] = {0};
    if (GetEnvironmentVariableA("RTW3MP_TEST_FILELOG", v, sizeof(v)) == 0 || v[0] != '1') return;
    static bool done = false;
    if (done) return;
    done = true;
    void* target = nullptr;
    MH_STATUS st = MH_Initialize();  // (the mod's own hooks come later: game::InstallHooks accepts this)
    if (st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED)
        st = MH_CreateHookApiEx(L"kernel32", "CreateFileW", (void*)&d_CreateFileW, (void**)&o_CreateFileW, &target);
    if (st == MH_OK) st = MH_EnableHook(target);
    Log("TEST: RTW3MP_TEST_FILELOG=1: pocfilelog %s (every file opened for reading, once per phase)",
        st == MH_OK ? "on" : "failed");
}

void SetPhase(const char* phase) {
    std::lock_guard<std::mutex> lk(g_pocMu);
    if (g_phase != phase) {
        g_phase = phase;
        g_pocSeen.clear();  // a file the battle opens again is logged again in its phase
    }
}

}  // namespace datafiles
