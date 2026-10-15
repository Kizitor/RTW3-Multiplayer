#include "saveio.h"
#include <algorithm>

// ---------------------------------------------------------------------------------------------
// INI

static bool KeyIs(const std::string& line, const std::string& key) {
    return line.size() > key.size() && line[key.size()] == '=' && line.compare(0, key.size(), key) == 0;
}

IniSection* IniDoc::Find(const std::string& name) {
    for (auto& s : sections)
        if (s.name == name) return &s;
    return nullptr;
}

const IniSection* IniDoc::Find(const std::string& name) const {
    for (auto& s : sections)
        if (s.name == name) return &s;
    return nullptr;
}

std::string IniDoc::Get(const std::string& sec, const std::string& key, const std::string& def) const {
    const IniSection* s = Find(sec);
    if (!s) return def;
    for (auto& l : s->lines)
        if (KeyIs(l, key)) return l.substr(key.size() + 1);
    return def;
}

void IniDoc::Set(const std::string& sec, const std::string& key, const std::string& value) {
    IniSection* s = Find(sec);
    if (!s) {
        sections.push_back({sec, {}});
        s = &sections.back();
    }
    for (auto& l : s->lines) {
        if (KeyIs(l, key)) {
            l = key + "=" + value;
            return;
        }
    }
    s->lines.push_back(key + "=" + value);
}

IniDoc ParseIni(const std::string& bytes) {
    IniDoc d;
    size_t p = 0;
    if (bytes.size() >= 3 && (uint8_t)bytes[0] == 0xEF && (uint8_t)bytes[1] == 0xBB && (uint8_t)bytes[2] == 0xBF) {
        d.bom = true;
        p = 3;
    }
    IniSection* cur = nullptr;
    while (p < bytes.size()) {
        size_t e = bytes.find('\n', p);
        if (e == std::string::npos) e = bytes.size();
        std::string line = bytes.substr(p, e - p);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        p = e + 1;
        if (line.size() >= 2 && line[0] == '[' && line.back() == ']') {
            d.sections.push_back({line.substr(1, line.size() - 2), {}});
            cur = &d.sections.back();
            continue;
        }
        if (cur)
            cur->lines.push_back(line);
        else
            d.head.push_back(line);
    }
    return d;
}

std::string WriteIni(const IniDoc& d) {
    std::string out;
    out.reserve(1 << 20);
    if (d.bom) out += "\xEF\xBB\xBF";
    for (auto& l : d.head) out += l + "\r\n";
    for (auto& s : d.sections) {
        out += "[" + s.name + "]\r\n";
        for (auto& l : s.lines) out += l + "\r\n";
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Bundle

const std::string* Bundle::Get(const std::string& name) const {
    for (auto& f : files)
        if (f.first == name) return &f.second;
    return nullptr;
}

static void PutU32(std::string& s, uint32_t v) { s.append((const char*)&v, 4); }

std::string Bundle::Pack() const {
    std::string s = "RTW3MPB1";
    PutU32(s, (uint32_t)files.size());
    for (auto& f : files) {
        PutU32(s, (uint32_t)f.first.size());
        s += f.first;
        PutU32(s, (uint32_t)f.second.size());
        s += f.second;
    }
    return s;
}

bool Bundle::Unpack(const std::string& d) {
    files.clear();
    if (d.size() < 12 || d.compare(0, 8, "RTW3MPB1") != 0) return false;
    size_t p = 8;
    auto rd = [&](uint32_t& v) {
        if (p + 4 > d.size()) return false;
        memcpy(&v, d.data() + p, 4);
        p += 4;
        return true;
    };
    uint32_t n;
    if (!rd(n) || n > 256) return false;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t nl, dl;
        if (!rd(nl) || nl > 260 || p + nl > d.size()) return false;
        std::string name = d.substr(p, nl);
        p += nl;
        if (!rd(dl) || p + dl > d.size()) return false;
        files.emplace_back(name, d.substr(p, dl));
        p += dl;
    }
    return true;
}

size_t Bundle::TotalBytes() const {
    size_t t = 0;
    for (auto& f : files) t += f.second.size();
    return t;
}

namespace saveio {

static std::wstring SlotStr(int slot) { return std::to_wstring(slot); }

static bool SafeName(const std::string& n) {
    if (n.empty() || n.size() > 100) return false;
    for (char c : n)
        if (!(isalnum((unsigned char)c) || c == '_' || c == '.' || c == '{' || c == '}' || c == '-')) return false;
    return n.find("..") == std::string::npos;
}

// Save files of a slot we transfer: RTWGame<slot>.*, MapData<slot>.dat, DesignFiles<n>.des
static bool IsSaveFile(const std::wstring& name, int slot) {
    std::wstring s = SlotStr(slot);
    if (name.rfind(L"RTWGame" + s + L".", 0) == 0) return true;
    if (name == L"MapData" + s + L".dat") return true;
    if (name.rfind(L"DesignFiles", 0) == 0 && name.size() > 15 && name.substr(name.size() - 4) == L".des") return true;
    return false;
}

static std::string Normalize(const std::wstring& name, int slot) {
    std::wstring s = SlotStr(slot);
    std::wstring n = name;
    for (const wchar_t* prefix : {L"RTWGame", L"MapData"}) {
        std::wstring pre = std::wstring(prefix) + s + L".";
        if (n.rfind(pre, 0) == 0) n = std::wstring(prefix) + L"{S}." + n.substr(pre.size());
    }
    return W2U(n);
}

static std::wstring Denormalize(const std::string& name, int slot) {
    std::wstring n = U2W(name);
    size_t p = n.find(L"{S}");
    if (p != std::wstring::npos) n.replace(p, 3, SlotStr(slot));
    return n;
}

bool BuildStateBundle(const std::wstring& slotDir, int slot, int idOffset, Bundle& out, std::string& err) {
    out.files.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((slotDir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        err = "save folder not found";
        return false;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        if (!IsSaveFile(name, slot)) continue;
        std::string data;
        if (!ReadFileBytes(slotDir + name, data)) {
            err = "cannot read " + W2U(name);
            FindClose(h);
            return false;
        }
        std::string norm = Normalize(name, slot);
        if (norm == "RTWGame{S}.bcs" && idOffset) {
            IniDoc d = ParseIni(data);
            long long id = atoll(d.Get("General", "IDNo", "0").c_str());
            d.Set("General", "IDNo", std::to_string(id + idOffset));
            data = WriteIni(d);
        }
        out.Add(norm, data);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!out.Get("RTWGame{S}.bcs")) {
        err = "no RTWGame save in slot";
        return false;
    }
    return true;
}

bool ApplyStateBundle(const Bundle& b, const std::wstring& slotDir, int slot, std::string& err) {
    if (slot < 50) {
        err = "refusing to overwrite a regular save slot";
        return false;
    }
    EnsureDir(slotDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((slotDir + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName;
            if (IsSaveFile(name, slot) || name.rfind(L"Autosave", 0) == 0) DeleteFileW((slotDir + name).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (auto& f : b.files) {
        if (!SafeName(f.first)) {
            err = "bad file name in bundle";
            return false;
        }
        if (!WriteFileBytes(slotDir + Denormalize(f.first, slot), f.second)) {
            err = "cannot write " + f.first;
            return false;
        }
    }
    return true;
}

static const char* kNationSections[] = {"Nation%d", "Nation%dShips", "Nation%dCoastalArtillery", "Nation%dSubmarines"};

bool BuildSubmission(const std::wstring& slotDir, int slot, int nationIdx, long long idStart, Bundle& out,
                     std::string& err) {
    out.files.clear();
    std::string bcs;
    std::wstring s = SlotStr(slot);
    if (!ReadFileBytes(slotDir + L"RTWGame" + s + L".bcs", bcs)) {
        err = "cannot read own save";
        return false;
    }
    IniDoc d = ParseIni(bcs);
    IniDoc mini;
    mini.bom = d.bom;
    mini.Set("General", "IDNo", d.Get("General", "IDNo", "0"));
    mini.Set("General", "IDNoStart", std::to_string(idStart));
    mini.Set("General", "MPNation", std::to_string(nationIdx));
    mini.Set("General", "Year", d.Get("General", "Year"));
    mini.Set("General", "Month", d.Get("General", "Month"));
    for (const char* fmt : kNationSections) {
        char name[64];
        snprintf(name, sizeof(name), fmt, nationIdx);
        const IniSection* sec = d.Find(name);
        if (sec) mini.sections.push_back(*sec);
    }
    out.Add("nation.bcs", WriteIni(mini));
    std::string des;
    if (ReadFileBytes(slotDir + L"DesignFiles" + std::to_wstring(nationIdx) + L".des", des)) out.Add("designs.des", des);
    std::string map;
    if (ReadFileBytes(slotDir + L"MapData" + s + L".dat", map)) out.Add("mapdata.dat", map);
    return true;
}

// Copies base improvements for possessions owned by `owner` from the client's map data.
static int MergePossessions(IniDoc& host, const IniDoc& client, const std::string& owner) {
    IniSection* hs = host.Find("MapAreas");
    const IniSection* cs = client.Find("MapAreas");
    if (!hs || !cs) return 0;
    std::map<std::string, std::string> cv;
    for (auto& l : cs->lines) {
        size_t e = l.find('=');
        if (e != std::string::npos) cv[l.substr(0, e)] = l.substr(e + 1);
    }
    std::map<std::string, size_t> hidx;
    for (size_t i = 0; i < hs->lines.size(); i++) {
        size_t e = hs->lines[i].find('=');
        if (e != std::string::npos) hidx[hs->lines[i].substr(0, e)] = i;
    }
    int merged = 0;
    for (auto& kv : hidx) {
        const std::string& key = kv.first;
        if (key.size() < 6 || key.compare(key.size() - 5, 5, "Owner") != 0) continue;
        std::string val = hs->lines[kv.second].substr(key.size() + 1);
        if (val != owner) continue;
        std::string prefix = key.substr(0, key.size() - 5);  // MapArea<i>Possession<j>
        auto cn = cv.find(prefix + "Name");
        auto hn = hidx.find(prefix + "Name");
        if (cn == cv.end() || hn == hidx.end()) continue;
        if (hs->lines[hn->second].substr(prefix.size() + 5) != cn->second) continue;
        for (const char* field : {"BaseValue", "BuildingBase"}) {
            auto c = cv.find(prefix + field);
            auto hl = hidx.find(prefix + field);
            if (c != cv.end() && hl != hidx.end()) {
                hs->lines[hl->second] = prefix + field + "=" + c->second;
                merged++;
            }
        }
    }
    return merged;
}

// Diplomacy is decided on the host: these [NationK] keys keep the host's values when a player's turn is merged
// (the other side of each relation lives in the other nations' sections).
// Keys of [NationK] the host keeps when it merges a player's turn: the diplomacy values, and the victory points (the
// host credits them during the month; the player's copy is the older value).
static bool IsHostOwnedKey(const std::string& line) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    std::string k = line.substr(0, eq);
    if (k == "Tension" || k == "Allied" || k == "VP") return true;
    for (const char* p : {"AITension", "AIAlliance"}) {
        size_t n = strlen(p);
        if (k.size() == n + 1 && k.compare(0, n, p) == 0 && isdigit((unsigned char)k[n])) return true;
    }
    return false;
}

bool MergeSubmission(const std::wstring& slotDir, int slot, int nationIdx, const Bundle& sub, std::string& err) {
    std::wstring s = SlotStr(slot);
    std::wstring bcsPath = slotDir + L"RTWGame" + s + L".bcs";
    std::string hostBytes;
    if (!ReadFileBytes(bcsPath, hostBytes)) {
        err = "cannot read host save";
        return false;
    }
    const std::string* nb = sub.Get("nation.bcs");
    if (!nb) {
        err = "submission without nation data";
        return false;
    }
    IniDoc host = ParseIni(hostBytes);
    IniDoc cli = ParseIni(*nb);
    if (atoi(cli.Get("General", "MPNation", "-1").c_str()) != nationIdx) {
        err = "submission is for another nation";
        return false;
    }
    if (cli.Get("General", "Year") != host.Get("General", "Year") ||
        cli.Get("General", "Month") != host.Get("General", "Month")) {
        err = "submission is for another month";
        return false;
    }
    int replaced = 0;
    for (const char* fmt : kNationSections) {
        char name[64];
        snprintf(name, sizeof(name), fmt, nationIdx);
        const IniSection* cs = cli.Find(name);
        IniSection* hs = host.Find(name);
        if (cs && hs) {
            std::map<std::string, std::string> keep;  // host-owned values ([NationK] only)
            if (fmt == kNationSections[0])
                for (auto& l : hs->lines)
                    if (IsHostOwnedKey(l)) keep[l.substr(0, l.find('='))] = l;
            hs->lines = cs->lines;
            for (auto& l : hs->lines) {
                if (!IsHostOwnedKey(l)) continue;
                auto it = keep.find(l.substr(0, l.find('=')));
                if (it != keep.end()) l = it->second;
            }
            replaced++;
        }
    }
    long long hostId = atoll(host.Get("General", "IDNo", "0").c_str());
    long long cliId = atoll(cli.Get("General", "IDNo", "0").c_str());
    long long cliStart = atoll(cli.Get("General", "IDNoStart", "0").c_str());
    // Clients allocate ids from their own range; only move the counter if the client used any.
    if (cliId > cliStart && cliId > hostId) host.Set("General", "IDNo", std::to_string(cliId));

    // All new file contents first, then write them; if one write fails, the files already written get their old
    // contents back, so a turn is either merged completely or not at all.
    struct Out {
        std::wstring path;
        std::string data, original;
        bool hadOriginal;
        const char* what;
    };
    std::vector<Out> outs;
    outs.push_back({bcsPath, WriteIni(host), hostBytes, true, "host save"});
    if (const std::string* des = sub.Get("designs.des")) {
        Out o{slotDir + L"DesignFiles" + std::to_wstring(nationIdx) + L".des", *des, "", false, "designs"};
        o.hadOriginal = ReadFileBytes(o.path, o.original);
        outs.push_back(o);
    }
    int poss = 0;
    if (const std::string* md = sub.Get("mapdata.dat")) {
        std::wstring mapPath = slotDir + L"MapData" + s + L".dat";
        std::string hostMap;
        if (ReadFileBytes(mapPath, hostMap)) {
            IniDoc hm = ParseIni(hostMap);
            IniDoc cm = ParseIni(*md);
            std::string owner = host.Get("Nation" + std::to_string(nationIdx), "Name");
            poss = MergePossessions(hm, cm, owner);
            if (poss) outs.push_back({mapPath, WriteIni(hm), hostMap, true, "map data"});
        }
    }
    for (size_t i = 0; i < outs.size(); i++) {
        if (WriteFileBytes(outs[i].path, outs[i].data)) continue;
        DWORD e = GetLastError();
        for (size_t j = 0; j < i; j++) {
            bool restored = outs[j].hadOriginal ? WriteFileBytes(outs[j].path, outs[j].original)
                                                : DeleteFileW(outs[j].path.c_str()) != 0;
            if (!restored) Log("merge nation %d: could not restore %s", nationIdx, outs[j].what);
        }
        err = std::string("cannot write ") + outs[i].what + " (Windows error " + std::to_string(e) + ")";
        Log("merge nation %d rejected: %s", nationIdx, err.c_str());
        return false;
    }
    Log("merge nation %d: %d sections, %d possession fields, IDNo %lld/%lld", nationIdx, replaced, poss, hostId, cliId);
    return replaced > 0;
}

bool ReadDate(const std::wstring& slotDir, int slot, int& year, int& month) {
    std::string bytes;
    if (!ReadFileBytes(slotDir + L"RTWGame" + std::to_wstring(slot) + L".bcs", bytes)) return false;
    // [General] is first; parse just the head of the file.
    IniDoc d = ParseIni(bytes.substr(0, std::min<size_t>(bytes.size(), 4096)));
    year = atoi(d.Get("General", "Year", "0").c_str());
    month = atoi(d.Get("General", "Month", "0").c_str());
    return year > 0;
}

long long ReadIdNo(const std::wstring& slotDir, int slot) {
    std::string bytes;
    if (!ReadFileBytes(slotDir + L"RTWGame" + std::to_wstring(slot) + L".bcs", bytes)) return -1;
    IniDoc d = ParseIni(bytes.substr(0, std::min<size_t>(bytes.size(), 8192)));
    std::string v = d.Get("General", "IDNo", "");
    return v.empty() ? -1 : atoll(v.c_str());
}

// ---------------------------------------------------------------------------------------------
// A joined player's own copy of the campaign. The game always plays nation 0 of a save; nation K and nation 0 trade
// places everywhere the save refers to nations by number, and the relations are rebuilt from K's point of view
// (the same model as the joined player's events, game::RunClientEvent).

static int SwapIdx(int v, int K) { return v == 0 ? K : v == K ? 0 : v; }

static bool SplitKV(const std::string& line, std::string& k, std::string& v) {
    size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    k = line.substr(0, eq);
    v = line.substr(eq + 1);
    return true;
}

static bool AllDigits(const std::string& s, size_t from, size_t to) {
    if (from >= to) return false;
    for (size_t i = from; i < to; i++)
        if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

static bool IsInt(const std::string& v) { return AllDigits(v, !v.empty() && v[0] == '-' ? 1 : 0, v.size()); }

// "<prefix><number><suffix>"
static bool KeyShape(const std::string& k, const char* prefix, const char* suffix) {
    size_t p = strlen(prefix), s = strlen(suffix);
    return k.size() > p + s && k.compare(0, p, prefix) == 0 && k.compare(k.size() - s, s, suffix) == 0 &&
           AllDigits(k, p, k.size() - s);
}

// Air units ("AU<n>Nation") and aircraft types ("AT<n>Nation") name their nation by number.
static bool AirNationKey(const std::string& k) {
    if (k.size() < 9 || k.compare(k.size() - 6, 6, "Nation") != 0) return false;
    size_t e = k.size() - 6, b = e;
    while (b > 0 && isdigit((unsigned char)k[b - 1])) b--;
    return b < e && b >= 2 && (k.compare(b - 2, 2, "AU") == 0 || k.compare(b - 2, 2, "AT") == 0);
}

// "MapArea<a>MineLevel<n>": the mines each nation laid in a sea area.
static bool MineKey(const std::string& k, std::string& area, int& n) {
    if (k.compare(0, 7, "MapArea") != 0) return false;
    size_t p = 7;
    while (p < k.size() && isdigit((unsigned char)k[p])) p++;
    if (p == 7 || k.compare(p, 9, "MineLevel") != 0 || !AllDigits(k, p + 9, k.size())) return false;
    area = k.substr(0, p);
    n = atoi(k.c_str() + p + 9);
    return true;
}

static bool SetExisting(IniDoc& d, const std::string& sec, const std::string& key, const std::string& value) {
    IniSection* s = d.Find(sec);
    if (!s) return false;
    for (auto& l : s->lines)
        if (KeyIs(l, key)) {
            l = key + "=" + value;
            return true;
        }
    return false;
}

bool TransformNationPlayer(PlayerCopyFiles& f, int K, const std::vector<int>& eventCooldowns, std::string& summary,
                           std::string& err) {
    if (K < 1 || K > 8) {
        err = "not a joined player's nation";
        return false;
    }
    IniDoc d = ParseIni(f.bcs);
    const std::string n0 = "Nation0", nk = "Nation" + std::to_string(K);
    if (!d.Find(n0) || !d.Find(nk) || !d.Find("General")) {
        err = "the save has no " + nk;
        return false;
    }
    auto geti = [&](const std::string& sec, const std::string& key) { return atoi(d.Get(sec, key, "0").c_str()); };
    auto sec = [](int i) { return "Nation" + std::to_string(i); };

    // Relations before the swap: rel(a, b) with nation 0 = the other nation's Tension/Allied, between nations
    // 1..8 = AITension<b>/AIAlliance<b>.
    int T[9], Al[9], AIT[9][9] = {}, AIA[9][9] = {};
    for (int i = 0; i < 9; i++) {
        T[i] = geti(sec(i), "Tension");
        Al[i] = geti(sec(i), "Allied");
        for (int j = 1; j < 9; j++) {
            AIT[i][j] = geti(sec(i), "AITension" + std::to_string(j));
            AIA[i][j] = geti(sec(i), "AIAlliance" + std::to_string(j));
        }
    }
    auto rel = [&](int a, int b, bool alliance) {
        if (a == b) return 0;
        if (a == 0) return alliance ? Al[b] : T[b];
        if (b == 0) return alliance ? Al[a] : T[a];
        return alliance ? AIA[a][b] : AIT[a][b];
    };
    // The player nation's own values of these keys (unused by the game) stay with index 0.
    std::map<std::string, std::string> playerOwn;
    for (auto& l : d.Find(n0)->lines) {
        std::string k, v;
        if (SplitKV(l, k, v) && (k == "Tension" || k == "Allied" || k == "TechSharing" || KeyShape(k, "AITension", "") ||
                                 KeyShape(k, "AIAlliance", "")))
            playerOwn[k] = v;
    }

    // Nation sections ([Nation0], [Nation0Ships], [Nation0Losses], ...) trade places with nation K's.
    int swapped = 0;
    for (auto& s : d.sections) {
        if (s.name.compare(0, 7, "Nation0") != 0 || (s.name.size() > 7 && isdigit((unsigned char)s.name[7]))) continue;
        IniSection* other = d.Find(nk + s.name.substr(7));
        if (!other) continue;
        std::swap(s.lines, other->lines);
        swapped++;
    }

    for (int x = 1; x < 9; x++) {
        int sx = SwapIdx(x, K);
        SetExisting(d, sec(x), "Tension", std::to_string(rel(sx, K, false)));
        SetExisting(d, sec(x), "Allied", std::to_string(rel(sx, K, true)));
        SetExisting(d, sec(x), "TechSharing", "0");  // tech-sharing deals were the host's
        for (int y = 1; y < 9; y++) {
            int sy = SwapIdx(y, K);
            SetExisting(d, sec(x), "AITension" + std::to_string(y), std::to_string(x == y ? 0 : rel(sx, sy, false)));
            SetExisting(d, sec(x), "AIAlliance" + std::to_string(y), std::to_string(x == y ? 0 : rel(sx, sy, true)));
        }
    }
    for (auto& p : playerOwn) SetExisting(d, n0, p.first, p.second);

    // Nation numbers inside the sections: where ships were built, the air units' and aircraft types' nations. The
    // host's ships (now nation K) lose their commanders: officers belong to the player.
    int commanders = 0;
    for (auto& s : d.sections) {
        bool hostShips = s.name.compare(0, nk.size(), nk) == 0 && s.name.size() > nk.size() &&
                         !isdigit((unsigned char)s.name[nk.size()]);
        for (auto& l : s.lines) {
            std::string k, v;
            if (!SplitKV(l, k, v) || !IsInt(v)) continue;
            if (KeyShape(k, "Ship", "BuildingNationIdx") || AirNationKey(k)) {
                l = k + "=" + std::to_string(SwapIdx(atoi(v.c_str()), K));
            } else if (hostShips && KeyShape(k, "Ship", "CommanderId") && v != "-1") {
                l = k + "=-1";
                commanders++;
            }
        }
    }

    // [General]: the player's war state and score belong to nation K now.
    bool kAtWar = false;
    int enemyVP = 0;
    for (int x = 1; x < 9; x++)
        if (geti(sec(x), "Tension") > 40) {
            kAtWar = true;
            enemyVP += std::max(0, geti(sec(x), "VP"));
        }
    int war = geti("General", "War");
    bool hostAtWar = war > 0;
    SetExisting(d, "General", "War", std::to_string(kAtWar ? (war > 0 ? war : 1) : (war > 0 ? -12 : war)));
    SetExisting(d, "General", "EnemyVP", std::to_string(enemyVP));
    SetExisting(d, "General", "ScenarioInProgress", "-1");  // a battle in progress was the host's
    for (const char* key : {"BattleAlliedNationIdx", "BattleEnemyAlliedNationIdx"}) {
        std::string v = d.Get("General", key, "");
        if (IsInt(v)) SetExisting(d, "General", key, std::to_string(SwapIdx(atoi(v.c_str()), K)));
    }
    // The host's invasion plans and army offensives (the game's end-of-war values).
    SetExisting(d, "General", "InvasionTargetName", "XXX");
    SetExisting(d, "General", "EnemyInvasionTargetName", "XXX");
    SetExisting(d, "General", "ArmyOffensive", "0");
    SetExisting(d, "General", "RevolutionPending", "0");
    SetExisting(d, "General", "LastMissionIdx", "20");
    if (hostAtWar && !kAtWar) {  // the host's war is over for this campaign: as the game's DoPeace leaves it
        SetExisting(d, "General", "TotalWar", "0");
        SetExisting(d, "General", "TFW", "1");
        SetExisting(d, "General", "HasBeenBattle", "0");
        SetExisting(d, "General", "AutoBuildSubs", "0");
    }
    if (eventCooldowns.size() == 100 && d.Find("EUT"))
        for (int j = 0; j < 100; j++) SetExisting(d, "EUT", "EUT" + std::to_string(j), std::to_string(eventCooldowns[j]));

    // The new player's ships (for its divisions).
    std::set<std::string> playerShipIds;
    if (const IniSection* ps = d.Find("Nation0Ships"))
        for (auto& l : ps->lines) {
            std::string k, v;
            if (SplitKV(l, k, v) && KeyShape(k, "Ship", "Id")) playerShipIds.insert(v);
        }
    std::string playerName = d.Get(n0, "Name"), hostName = d.Get(nk, "Name");
    f.bcs = WriteIni(d);

    // Map: mines per nation, and the nation a possession was taken from.
    int mapFields = 0;
    if (f.hasMap) {
        IniDoc m = ParseIni(f.map);
        auto fix = [&](std::vector<std::string>& lines) {
            for (auto& l : lines) {
                std::string k, v, area;
                int n;
                if (!SplitKV(l, k, v)) continue;
                if (MineKey(k, area, n) && (n == 0 || n == K)) {
                    l = area + "MineLevel" + std::to_string(SwapIdx(n, K)) + "=" + v;
                    mapFields++;
                } else if (k.compare(0, 7, "MapArea") == 0 && k.size() > 9 &&
                           k.compare(k.size() - 9, 9, "TakenFrom") == 0 && IsInt(v)) {
                    int t = atoi(v.c_str());
                    if (t == 0 || t == K) {
                        l = k + "=" + std::to_string(SwapIdx(t, K));
                        mapFields++;
                    }
                }
            }
        };
        fix(m.head);
        for (auto& s : m.sections) fix(s.lines);
        f.map = WriteIni(m);
    }

    // Officers stay (the player's); divisions are kept only if all their ships are the new player's.
    int divKept = 0, divDropped = 0;
    if (f.hasOff) {
        IniDoc o = ParseIni(f.off);
        if (IniSection* cd = o.Find("CampaignDivisions")) {
            std::map<int, std::vector<std::pair<std::string, std::string>>> divs;  // number -> (field, value)
            std::vector<std::string> other;
            for (auto& l : cd->lines) {
                std::string k, v;
                size_t p = 7;
                if (SplitKV(l, k, v) && k.compare(0, 7, "CampDiv") == 0) {
                    while (p < k.size() && isdigit((unsigned char)k[p])) p++;
                    if (p > 7 && p < k.size()) {
                        divs[atoi(k.substr(7, p - 7).c_str())].push_back({k.substr(p), v});
                        continue;
                    }
                    if (k == "CampDivNo") continue;
                }
                other.push_back(l);
            }
            std::vector<std::string> lines;
            for (auto& dv : divs) {
                bool mine = true;
                for (auto& fv : dv.second)
                    if (KeyShape(fv.first, "ShipId", "") && !playerShipIds.count(fv.second)) mine = false;
                if (!mine) {
                    divDropped++;
                    continue;
                }
                for (auto& fv : dv.second) lines.push_back("CampDiv" + std::to_string(divKept) + fv.first + "=" + fv.second);
                divKept++;
            }
            if (divs.size() || !other.empty()) {
                std::vector<std::string> out = {"CampDivNo=" + std::to_string(divKept)};
                out.insert(out.end(), lines.begin(), lines.end());
                for (auto& l : other)
                    if (!l.empty()) out.push_back(l);
                cd->lines = out;
            }
            f.off = WriteIni(o);
        }
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s is the player nation, %s is nation %d (AI); %d sections, %d commanders, %d map "
             "fields, divisions %d kept / %d dropped, %s",
             playerName.c_str(), hostName.c_str(), K, swapped, commanders, mapFields, divKept, divDropped,
             kAtWar ? "at war" : "at peace");
    summary = buf;
    return true;
}

bool MakeNationPlayer(const std::wstring& slotDir, int slot, int K, const std::vector<int>& eventCooldowns,
                      std::string& summary, std::string& err) {
    std::wstring s = SlotStr(slot);
    std::wstring bcsPath = slotDir + L"RTWGame" + s + L".bcs", mapPath = slotDir + L"MapData" + s + L".dat",
                 offPath = slotDir + L"RTWGame" + s + L".off";
    PlayerCopyFiles f;
    if (!ReadFileBytes(bcsPath, f.bcs)) {
        err = "cannot read the copied save";
        return false;
    }
    f.hasMap = ReadFileBytes(mapPath, f.map);
    f.hasOff = ReadFileBytes(offPath, f.off);
    if (!TransformNationPlayer(f, K, eventCooldowns, summary, err)) return false;
    // The ship designs are stored per nation number too.
    std::wstring des0 = slotDir + L"DesignFiles0.des", desK = slotDir + L"DesignFiles" + std::to_wstring(K) + L".des",
                 tmp = slotDir + L"DesignFiles_swap.tmp";
    bool has0 = FileExists(des0), hasK = FileExists(desK);
    bool ok = (!has0 || MoveFileExW(des0.c_str(), tmp.c_str(), MOVEFILE_REPLACE_EXISTING)) &&
              (!hasK || MoveFileExW(desK.c_str(), des0.c_str(), MOVEFILE_REPLACE_EXISTING)) &&
              (!has0 || MoveFileExW(tmp.c_str(), desK.c_str(), MOVEFILE_REPLACE_EXISTING));
    if (!ok) {
        err = "cannot swap the design files (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (!WriteFileBytes(bcsPath, f.bcs) || (f.hasMap && !WriteFileBytes(mapPath, f.map)) ||
        (f.hasOff && !WriteFileBytes(offPath, f.off))) {
        err = "cannot write the copied save";
        return false;
    }
    // A battle in progress (the host's) can't be resumed in the copy.
    for (const wchar_t* n : {L"Autosave.sac", L"Autosave.off", L"BattleInfo.bbi"}) DeleteFileW((slotDir + n).c_str());
    Log("player copy in slot %d: %s", slot, summary.c_str());
    return true;
}

// ---------------------------------------------------------------------------------------------
// Joined players' battles: results

// "Ship<n><Field>": the ship number and the field (which starts with a letter).
static bool ShipKey(const std::string& line, int& n, std::string& field) {
    if (line.compare(0, 4, "Ship") != 0) return false;
    size_t p = 4;
    while (p < line.size() && isdigit((unsigned char)line[p])) p++;
    if (p == 4 || p >= line.size() || !isalpha((unsigned char)line[p])) return false;
    n = atoi(line.c_str() + 4);
    field = line.substr(p);  // "Field=value"
    return true;
}

struct ShipBlock {
    std::string id;
    std::vector<std::string> fields;  // "Field=value"
};

// A ship section as blocks in their order, plus its other lines (e.g. CACount=).
static void ParseShips(const IniSection& s, std::vector<ShipBlock>& ships, std::vector<std::string>& other) {
    std::map<int, size_t> at;
    for (auto& l : s.lines) {
        int n;
        std::string f;
        if (!ShipKey(l, n, f)) {
            if (!l.empty()) other.push_back(l);
            continue;
        }
        auto it = at.find(n);
        if (it == at.end()) {
            at[n] = ships.size();
            ships.push_back({});
            it = at.find(n);
        }
        ShipBlock& b = ships[it->second];
        if (f.compare(0, 3, "Id=") == 0) b.id = f.substr(3);
        b.fields.push_back(f);
    }
}

static std::vector<std::string> ShipLines(const std::vector<ShipBlock>& ships, const std::vector<std::string>& other) {
    std::vector<std::string> out;
    for (auto& l : other) out.push_back(l);
    for (size_t i = 0; i < ships.size(); i++)
        for (auto& f : ships[i].fields) out.push_back("Ship" + std::to_string(i) + f);
    return out;
}

static const char* kBattleSections[] = {"Ships", "Losses", "CoastalArtillery"};

static std::string JoinLines(const std::vector<std::string>& lines) {
    std::string s;
    for (auto& l : lines) s += l + "\n";
    return s;
}

static std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('\n', p);
        if (e == std::string::npos) e = s.size();
        std::string l = s.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) out.push_back(l);
        p = e + 1;
    }
    return out;
}

bool ExtractBattleResult(const std::string& beforeBcs, const std::string& afterBcs, int K, int J, Bundle& out,
                         std::string& summary, std::string& err) {
    out.files.clear();
    if (K < 1 || K > 8 || J < 1 || J > 8 || J == K) {
        err = "bad nations";
        return false;
    }
    IniDoc before = ParseIni(beforeBcs), after = ParseIni(afterBcs);
    // In the battle slot K is nation 0, the host's nation is K and every other nation keeps its number. Besides K and
    // J, the nations allied to either side in this battle ([General] Battle(Enemy)AlliedNationIdx) are sent too; the
    // host only takes those that are AI nations.
    auto toHost = [K](int slotIdx) { return slotIdx == 0 ? K : slotIdx == K ? 0 : slotIdx; };
    std::vector<int> slotNations = {0, J};
    for (const char* key : {"BattleAlliedNationIdx", "BattleEnemyAlliedNationIdx"}) {
        int s = atoi(after.Get("General", key, "-1").c_str());
        if (s > 0 && s <= 8 && s != K && std::find(slotNations.begin(), slotNations.end(), s) == slotNations.end())
            slotNations.push_back(s);
    }
    for (int slotIdx : slotNations) {
        int hostIdx = toHost(slotIdx);
        for (const char* suffix : kBattleSections) {
            const IniSection* s = after.Find("Nation" + std::to_string(slotIdx) + suffix);
            if (!s) continue;
            std::vector<std::string> lines = s->lines;
            for (auto& l : lines) {
                int n;
                std::string f;
                if (!ShipKey(l, n, f) || f.compare(0, 18, "BuildingNationIdx=") != 0) continue;
                l = "Ship" + std::to_string(n) + "BuildingNationIdx=" + std::to_string(toHost(atoi(f.c_str() + 18)));
            }
            out.Add("sec:Nation" + std::to_string(hostIdx) + suffix, JoinLines(lines));
        }
    }
    auto num = [](const IniDoc& d, const std::string& sec, const std::string& key) {
        return atoi(d.Get(sec, key, "0").c_str());
    };
    KV kv;
    kv["dVP_K"] = std::to_string(num(after, "Nation0", "VP") - num(before, "Nation0", "VP"));
    kv["dPrestige_K"] = std::to_string(num(after, "Nation0", "Prestige") - num(before, "Nation0", "Prestige"));
    kv["dMorale_K"] = std::to_string(num(after, "Nation0", "FleetMorale") - num(before, "Nation0", "FleetMorale"));
    kv["dVP_J"] = std::to_string(num(after, "General", "EnemyVP") - num(before, "General", "EnemyVP"));
    out.Add("deltas", KVEncode(kv));
    summary = "VP " + kv["dVP_K"] + "/" + kv["dVP_J"] + ", prestige " + kv["dPrestige_K"] + ", morale " + kv["dMorale_K"];
    return true;
}

static void AddToKey(IniDoc& d, const std::string& sec, const std::string& key, int delta) {
    if (!delta || !d.Find(sec)) return;
    d.Set(sec, key, std::to_string(atoi(d.Get(sec, key, "0").c_str()) + delta));
}

// Ship fields seen from the player's side (what the player knows about a ship); they are not the ship's state.
static bool IntelField(const std::string& f) {
    for (const char* k : {"PlayerIntel=", "EnemyClassName=", "EnemySpeed=", "EnemyBelt="})
        if (f.compare(0, strlen(k), k) == 0) return true;
    return false;
}

static std::string FieldKey(const std::string& f) { return f.substr(0, f.find('=')); }

bool ApplyBattleResult(IniDoc& hostOut, const std::string& sentBcs, int K, int J, const Bundle& result,
                       const std::vector<int>& allowed, std::string& summary, std::string& err) {
    IniDoc sent = ParseIni(sentBcs);
    IniDoc host = hostOut;  // committed only if everything applies
    std::vector<std::pair<std::string, const std::string*>> secs;
    int skipped = 0;
    for (auto& f : result.files) {
        if (f.first.compare(0, 4, "sec:") != 0) continue;
        std::string name = f.first.substr(4);
        int idx = name.size() > 6 && name.compare(0, 6, "Nation") == 0 && isdigit((unsigned char)name[6])
                      ? atoi(name.c_str() + 6)
                      : -1;
        std::string suffix = idx >= 0 ? name.substr(6 + std::to_string(idx).size()) : "";
        bool known = false;
        for (const char* s : kBattleSections) known |= suffix == s;
        if (idx < 1 || idx > 8 || !known) {
            err = "unexpected section " + name;
            return false;
        }
        if (std::find(allowed.begin(), allowed.end(), idx) == allowed.end()) {  // e.g. an ally that is a player
            skipped++;
            continue;
        }
        secs.push_back({name, &f.second});
    }
    int changedShips = 0, removedShips = 0, changedFields = 0;
    for (auto& sp : secs) {
        const std::string& name = sp.first;
        int idx = atoi(name.c_str() + 6);
        IniSection* hs = host.Find(name);
        const IniSection* os = sent.Find(name);
        if (!hs || !os) continue;
        IniSection rs{name, SplitLines(*sp.second)};
        bool ships = name.find("Losses") == std::string::npos;
        if (!ships) {  // Losses: counters, add what the battle added
            for (auto& l : rs.lines) {
                size_t eq = l.find('=');
                if (eq == std::string::npos) continue;
                std::string key = l.substr(0, eq);
                AddToKey(host, name, key, atoi(l.c_str() + eq + 1) - atoi(sent.Get(name, key, "0").c_str()));
            }
            continue;
        }
        std::vector<ShipBlock> hShips, oShips, rShips;
        std::vector<std::string> hOther, oOther, rOther;
        ParseShips(*hs, hShips, hOther);
        ParseShips(*os, oShips, oOther);
        ParseShips(rs, rShips, rOther);
        std::map<std::string, const ShipBlock*> orig, res;
        for (auto& b : oShips)
            if (!b.id.empty()) orig[b.id] = &b;
        for (auto& b : rShips)
            if (!b.id.empty()) res[b.id] = &b;
        if (orig.size() != oShips.size() || res.size() != rShips.size()) {
            err = "ships without an id in " + name;
            return false;
        }
        std::vector<ShipBlock> merged;
        for (auto& b : hShips) {
            auto o = orig.find(b.id);
            if (o == orig.end()) {  // not in the battle's game
                merged.push_back(b);
                continue;
            }
            auto r = res.find(b.id);
            if (r == res.end()) {  // sunk in the battle (no longer in the list)
                removedShips++;
                continue;
            }
            // Only the fields the battle changed (relative to what was sent), never the player's intel.
            std::map<std::string, std::string> was;
            for (auto& f : o->second->fields) was[FieldKey(f)] = f;
            ShipBlock nb = b;
            int n = 0;
            for (auto& f : r->second->fields) {
                std::string key = FieldKey(f);
                auto w = was.find(key);
                if ((w != was.end() && w->second == f) || IntelField(f)) continue;
                bool set = false;
                for (auto& hf : nb.fields)
                    if (FieldKey(hf) == key) {
                        hf = f;
                        set = true;
                    }
                if (!set) nb.fields.push_back(f);
                n++;
            }
            if (n) {
                changedShips++;
                changedFields += n;
            }
            merged.push_back(nb);
        }
        // The counts of the section's own lines (CACount=) or of the nation's ShipCount.
        if (name.find("CoastalArtillery") != std::string::npos) {
            for (auto& l : hOther)
                if (l.compare(0, 8, "CACount=") == 0) l = "CACount=" + std::to_string(merged.size());
        } else {
            host.Set("Nation" + std::to_string(idx), "ShipCount", std::to_string(merged.size()));
        }
        hs->lines = ShipLines(merged, hOther);
    }
    const std::string* d = result.Get("deltas");
    KV kv = d ? KVDecode(*d) : KV();
    std::string nk = "Nation" + std::to_string(K), nj = "Nation" + std::to_string(J);
    AddToKey(host, nk, "VP", KVInt(kv, "dVP_K"));
    AddToKey(host, nk, "Prestige", KVInt(kv, "dPrestige_K"));
    AddToKey(host, nk, "FleetMorale", KVInt(kv, "dMorale_K"));
    AddToKey(host, nj, "VP", KVInt(kv, "dVP_J"));
    hostOut = host;
    summary = std::to_string(changedShips) + " ships changed (" + std::to_string(changedFields) + " fields), " +
              std::to_string(removedShips) + " removed, VP " + KVStr(kv, "dVP_K", "0") + "/" + KVStr(kv, "dVP_J", "0") +
              ", prestige " + KVStr(kv, "dPrestige_K", "0") +
              (skipped ? ", " + std::to_string(skipped) + " section(s) of other players skipped" : "");
    return true;
}

}  // namespace saveio
