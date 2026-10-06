#include "saveio.h"

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
static bool IsDiplomacyKey(const std::string& line) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    std::string k = line.substr(0, eq);
    if (k == "Tension" || k == "Allied") return true;
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
            std::map<std::string, std::string> keep;  // host's diplomacy values ([NationK] only)
            if (fmt == kNationSections[0])
                for (auto& l : hs->lines)
                    if (IsDiplomacyKey(l)) keep[l.substr(0, l.find('='))] = l;
            hs->lines = cs->lines;
            for (auto& l : hs->lines) {
                if (!IsDiplomacyKey(l)) continue;
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

}  // namespace saveio
