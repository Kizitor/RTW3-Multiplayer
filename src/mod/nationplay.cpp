#include "nationplay.h"
#include "game.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <climits>
#include <random>

// See nationplay.h. Game facts (1.01.44; docs/GAME-INTERNALS.md "Intelligence, trade protection and foreign
// stations"):
// - TBuilderNation.IntelligenceSpending / IntelEffort are kept on the target nation: what nation 0 spends on it and
//   has achieved against it. The politics panel shows them under the flags of nations 1..8 (IntelToString) and its +/-
//   buttons change them (SpeedButton1Click -> ChangeIntel). GetIntelCostForPlayerNation (the budget's Intelligence
//   line) sums nations 1..8; GetMonthlyBalance subtracts it for PlayerNation and ADDS ComputeAINationIntelCost for any
//   other nation, joined players' nations included.
// - The monthly event slot (CheckTurnEvents) runs CheckIntelligence for the player only; AddIntelReport writes to the
//   one global TfrmBuildCamp.IntelReports list.
// - PlayerChecksBeforeTurn asks about trade protection (at war) and foreign stations (CheckForceOnForeignStations(True),
//   which also rolls world events: independence and rebellions of any nation's possessions); the month runs the
//   player's part of CheckShipsOnForeignStations (peace) and CheckPatrols (war) for PlayerNation only.

namespace nplay {

static struct Api {
    int fSpend = -1, fEffort = -1, fTension = -1, fAllied = -1, fTechSharing = -1, fBlockaded = -1, fPrestige = -1;
    int fNationName = -1, fPlayer = -1, fNations = -1, fIntelReports = -1, fMapAreas = -1, fStIntelligence = -1;
    int fShipStatus = -1, fPossName = -1, fPossOwner = -1, fPossRebellion = -1, fPossNatural = -1;
    int fSLList = -1, fSLCount = -1, fTag = -1, fHandle = -1, fSpeedButton[17] = {};
    void *mCheckIntel = nullptr, *mCostPlayer = nullptr, *mCostAI = nullptr, *mIntelToString = nullptr,
         *mChangeIntel = nullptr, *mSpeedClick = nullptr, *mPatrolReq = nullptr, *mCountPatrol = nullptr,
         *mHasForce = nullptr, *mReqTonnage = nullptr, *mIsOperational = nullptr, *mColonialTonnage = nullptr,
         *mRandomPossession = nullptr, *mLeaderTitle = nullptr, *mShipsOnFS = nullptr, *mMessageDlg = nullptr,
         *mGetTextBuf = nullptr;
    int* fleetSize = nullptr;
    int* warVar = nullptr;
    int** yearCell = nullptr;
    int** monthCell = nullptr;
    bool view = false, intel = false, trade = false;
} A;

static const int kNations = 9;  // nation 0 (the host's) and the eight others; nation 9 never takes part

static bool InImage(uintptr_t a) { return a >= dl::g_base && a < dl::g_base + dl::g_size; }
static void* CallTarget(uint8_t* e8) { return e8 + 5 + *(int32_t*)(e8 + 1); }

bool Resolve() {
    A.fSpend = dl::Field("TBuilderNation", "IntelligenceSpending");
    A.fEffort = dl::Field("TBuilderNation", "IntelEffort");
    A.fTension = dl::Field("TBuilderNation", "Tension");
    A.fAllied = dl::Field("TBuilderNation", "Allied");
    A.fTechSharing = dl::Field("TBuilderNation", "TechSharing");
    A.fBlockaded = dl::Field("TBuilderNation", "Blockaded");
    A.fPrestige = dl::Field("TBuilderNation", "Prestige");
    A.fNationName = dl::Field("TBuilderNation", "NationName");
    A.fPlayer = dl::Field("TfrmBuildCamp", "PlayerNation");
    A.fNations = dl::Field("TfrmBuildCamp", "BuilderNations");
    A.fIntelReports = dl::Field("TfrmBuildCamp", "IntelReports");
    A.fMapAreas = dl::Field("TfrmBuildCamp", "MapAreas");
    A.fStIntelligence = dl::Field("TfrmBuildCamp", "stIntelligence");
    A.fShipStatus = dl::Field("TCampaignShip", "Status");
    A.fPossName = dl::Field("TPossession", "Name");
    A.fPossOwner = dl::Field("TPossession", "Owner");
    A.fPossRebellion = dl::Field("TPossession", "Rebellion");
    A.fPossNatural = dl::Field("TPossession", "NaturalOwnerName");
    A.fSLList = dl::Field("TStringList", "FList");
    A.fSLCount = dl::Field("TStringList", "FCount");
    A.fTag = dl::Field("TComponent", "FTag");
    A.fHandle = dl::Field("TWinControl", "FHandle");
    for (int i = 1; i <= 16; i++) A.fSpeedButton[i] = dl::Field("TfrmBuildCamp", ("SpeedButton" + std::to_string(i)).c_str());
    A.mCheckIntel = dl::Method("TfrmBuildCamp", "CheckIntelligence");
    A.mCostPlayer = dl::Method("TfrmBuildCamp", "GetIntelCostForPlayerNation");
    A.mCostAI = dl::Method("TfrmBuildCamp", "ComputeAINationIntelCost");
    A.mIntelToString = dl::Method("TfrmBuildCamp", "IntelToString");
    A.mChangeIntel = dl::Method("TfrmBuildCamp", "ChangeIntel");
    A.mSpeedClick = dl::Method("TfrmBuildCamp", "SpeedButton1Click");
    A.mPatrolReq = dl::Method("TfrmBuildCamp", "GetPatrolRequirement");
    A.mCountPatrol = dl::Method("TBuilderNation", "CountShipsOnPatrol");
    A.mHasForce = dl::Method("TBuilderNation", "HasRequiredForceInArea");
    A.mReqTonnage = dl::Method("TBuilderNation", "GetRequiredTonnageInArea");
    A.mIsOperational = dl::Method("TCampaignShip", "IsOperational");
    A.mColonialTonnage = dl::Method("TCampaignShip", "GetColonialTonnageValue");
    A.mRandomPossession = dl::Method("TfrmBuildCamp", "GetRandomPossession");
    A.mLeaderTitle = dl::Method("TBuilderNation", "GetLeaderTitle");
    A.mShipsOnFS = dl::Method("TfrmBuildCamp", "CheckShipsOnForeignStations");
    A.mGetTextBuf = dl::Method("TControl", "GetTextBuf");  // tests: a control's text, also without a window handle
    // The war counter (save key General/War): WarStartsWithNation `cmp dword ptr [War],0; jg +n; mov dword ptr [War],1`.
    if (void* ws = dl::Method("TfrmBuildCamp", "WarStartsWithNation")) {
        uint8_t* hit = dl::FindPattern((uint8_t*)ws, 0x60, {0x83, 0x3D, -1, -1, -1, -1, 0x00, 0x7F, -1, 0xC7, 0x05,
                                                              -1, -1, -1, -1, 0x01, 0x00, 0x00, 0x00});
        if (hit && *(uint32_t*)(hit + 2) == *(uint32_t*)(hit + 11) && InImage(*(uint32_t*)(hit + 2)))
            A.warVar = (int*)(uintptr_t) * (uint32_t*)(hit + 2);
    }
    // FleetSize ([General] FleetSize): GetIntelCostForPlayerNation `imul dword ptr [FleetSize]`.
    if (A.mCostPlayer) {
        uint8_t* hit = dl::FindPattern((uint8_t*)A.mCostPlayer, 0x40, {0xF7, 0x2D, -1, -1, -1, -1});
        if (hit && InImage(*(uint32_t*)(hit + 2))) A.fleetSize = (int*)(uintptr_t) * (uint32_t*)(hit + 2);
    }
    // The campaign's year and month: CheckForceOnForeignStations `mov eax,[YearCell]; cmp dword ptr [eax],1900` and
    // `mov eax,[MonthCell]; cmp dword ptr [eax],1` (cells holding the variables' addresses).
    if (void* cf = dl::Method("TfrmBuildCamp", "CheckForceOnForeignStations")) {
        uint8_t* y = dl::FindPattern((uint8_t*)cf, 0x800, {0xA1, -1, -1, -1, -1, 0x81, 0x38, 0x6C, 0x07, 0x00, 0x00});
        uint8_t* m = dl::FindPattern((uint8_t*)cf, 0x800, {0xA1, -1, -1, -1, -1, 0x83, 0x38, 0x01});
        if (y && InImage(*(uint32_t*)(y + 1))) A.yearCell = (int**)(uintptr_t) * (uint32_t*)(y + 1);
        if (m && InImage(*(uint32_t*)(m + 1))) A.monthCell = (int**)(uintptr_t) * (uint32_t*)(m + 1);
    }
    // The game's Yes/No question (Vcl.Dialogs MessageDlgPosHelp), as PlayerChecksBeforeTurn calls it:
    // push 0; push -1; push -1; push 0; movzx ecx, word ptr [buttons]; mov dl, mtConfirmation; call X
    if (void* pc = dl::Method("TfrmBuildCamp", "PlayerChecksBeforeTurn")) {
        uint8_t* hit = dl::FindPattern((uint8_t*)pc, 0x400, {0x6A, 0x00, 0x6A, 0xFF, 0x6A, 0xFF, 0x6A, 0x00, 0x0F, 0xB7,
                                                              0x0D, -1, -1, -1, -1, 0xB2, 0x03, 0xE8});
        if (hit && InImage((uintptr_t)CallTarget(hit + 17))) A.mMessageDlg = CallTarget(hit + 17);
    }
    bool rel = A.fTension >= 0 && A.fAllied >= 0 && A.fTechSharing >= 0 && A.fPlayer >= 0 && A.fNations >= 0 && A.warVar;
    A.view = rel;  // and game::DiplomacyAvailable() at the time of use
    A.intel = A.view && A.fSpend >= 0 && A.fEffort >= 0 && A.fIntelReports >= 0 && A.fSLList >= 0 && A.fSLCount >= 0 &&
              A.fPrestige >= 0 && A.fTag >= 0 && A.mCheckIntel && A.mCostPlayer && A.mCostAI && A.mIntelToString &&
              A.mChangeIntel && A.fleetSize;
    A.trade = A.view && A.fMapAreas >= 0 && A.fShipStatus >= 0 && A.fBlockaded >= 0 && A.fPrestige >= 0 &&
              A.fPossOwner >= 0 && A.fPossName >= 0 && A.fPossRebellion >= 0 && A.fPossNatural >= 0 && A.mPatrolReq &&
              A.mCountPatrol && A.mHasForce && A.mReqTonnage && A.mIsOperational && A.mColonialTonnage &&
              A.mRandomPossession && A.mLeaderTitle && A.mShipsOnFS && A.yearCell && A.monthCell;
    Log("Per-player intelligence: %s; trade protection and foreign stations: %s (war=%p fleetSize=%p year=%p month=%p "
        "messageDlg=%p)",
        A.intel ? "ok" : "UNAVAILABLE", A.trade ? "ok" : "UNAVAILABLE", A.warVar, A.fleetSize, A.yearCell, A.monthCell,
        A.mMessageDlg);
    return A.intel || A.trade;
}

// ---------------------------------------------------------------------------------------------
// Small game helpers

static void* Frm() { return game::BuildCamp(); }
static void** NationArr() {
    void* f = Frm();
    void* list = f && A.fNations >= 0 ? dl::At<void*>(f, A.fNations) : nullptr;
    return list && dl::ListCount(list) >= kNations ? dl::At<void**>(list, 4) : nullptr;
}
static int& IntAt(void* o, int off) { return dl::At<int>(o, off); }
static int FleetSize() { return A.fleetSize ? *A.fleetSize : 0; }
static int Year() { return A.yearCell && *A.yearCell ? **A.yearCell : 0; }
static int Month() { return A.monthCell && *A.monthCell ? **A.monthCell : 0; }
static std::wstring NameOf(void* nation) {
    return nation && A.fNationName >= 0 ? dl::ReadUStr(dl::At<void*>(nation, A.fNationName)) : L"?";
}

static int SLCount(void* sl) { return sl ? dl::At<int>(sl, A.fSLCount) : 0; }
static std::wstring SLItem(void* sl, int i) {
    void** items = dl::At<void**>(sl, A.fSLList);  // TStringItem = {FString, FObject}
    return items ? dl::ReadUStr(items[i * 2]) : L"";
}
static void* IntelList() {
    void* f = Frm();
    void* l = f && A.fIntelReports >= 0 ? dl::At<void*>(f, A.fIntelReports) : nullptr;
    const char* cls = l ? dl::ClassNameOf(l) : nullptr;
    return cls && strcmp(cls, "TStringList") == 0 ? l : nullptr;
}

static std::string LeaderTitle(void* nation) {
    void* res = nullptr;
    if (A.mLeaderTitle && nation) dl::Call(A.mLeaderTitle, (uint32_t)(uintptr_t)nation, (uint32_t)(uintptr_t)&res);
    std::string t = W2U(dl::ReadUStr(res));
    return t.empty() ? "head of government" : t;
}

static std::string Leader(const char* text, void* nation) {  // the game's InsertLeaderName: "YY" -> the leader's title
    std::string s = text, t = LeaderTitle(nation);
    size_t p = s.find("YY");
    if (p != std::string::npos) s.replace(p, 2, t);
    return s;
}

static std::string OneLine(std::string s) {
    for (char& c : s)
        if (c == '\r' || c == '\n' || c == '\x1e' || c == '\x1f') c = ' ';
    return s;
}

static std::mt19937 g_rng((unsigned)(GetTickCount() * 2246822519u + 7));
static int Rnd(int n) { return n <= 0 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(g_rng); }

// ---------------------------------------------------------------------------------------------
// The view: nation K seen as "the player" (as the events of joined players do, see game::RunClientEvent): the nation
// list's entries 0 and K swapped, every nation's Tension/Allied (its relation with nation 0) holding its relation with K
// (TechSharing 0), the war counter from K's wars and, on the host, PlayerNation = K. Plain data, so __try blocks can use
// it; End() puts everything back.

struct View {
    bool active;
    int K;
    void** arr;
    void* obj[kNations];
    int origT[kNations], origA[kNations], origTS[kNations], tmpT[kNations];
    int war;
    void* prevPlayer;
    bool setPlayer;
};
static View g_v;

bool ViewActive() { return g_v.active; }

static bool ViewBegin(int K, bool setPlayer) {
    void* f = Frm();
    void** arr = NationArr();
    if (!A.view || g_v.active || game::EventActive() || !game::DiplomacyAvailable() || !f || !arr || K < 1 || K > 8)
        return false;
    // On a joined player's PC nation 0 is the host's: if the player's nation is at index 0 already, another view (the
    // relations view of a display routine) has the list swapped; never swap it twice.
    if (!setPlayer && arr[0] == game::PlayerNation()) return false;
    View& v = g_v;
    v.K = K;
    v.arr = arr;
    int tmpA[kNations];
    bool kAtWar = false;
    for (int i = 0; i < kNations; i++) {
        v.obj[i] = arr[i];
        v.origT[i] = IntAt(v.obj[i], A.fTension);
        v.origA[i] = IntAt(v.obj[i], A.fAllied);
        v.origTS[i] = IntAt(v.obj[i], A.fTechSharing);
        v.tmpT[i] = i == K ? 0 : game::Tension(K, i);
        tmpA[i] = i == K ? 0 : game::AllianceMonths(K, i);
        if (i != K && v.tmpT[i] > 40) kAtWar = true;
    }
    v.war = *A.warVar;
    for (int i = 0; i < kNations; i++) {
        IntAt(v.obj[i], A.fTension) = v.tmpT[i];
        IntAt(v.obj[i], A.fAllied) = tmpA[i];
        IntAt(v.obj[i], A.fTechSharing) = 0;
    }
    *A.warVar = kAtWar ? (v.war > 0 ? v.war : 1) : (v.war > 0 ? -12 : v.war);
    arr[0] = v.obj[K];
    arr[K] = v.obj[0];
    v.setPlayer = setPlayer;
    v.prevPlayer = dl::At<void*>(f, A.fPlayer);
    if (setPlayer) dl::At<void*>(f, A.fPlayer) = v.obj[K];
    v.active = true;
    return true;
}

static void ViewEnd() {
    View& v = g_v;
    if (!v.active) return;
    v.arr[0] = v.obj[0];
    v.arr[v.K] = v.obj[v.K];
    for (int i = 0; i < kNations; i++) {
        IntAt(v.obj[i], A.fTension) = v.origT[i];
        IntAt(v.obj[i], A.fAllied) = v.origA[i];
        IntAt(v.obj[i], A.fTechSharing) = v.origTS[i];
    }
    *A.warVar = v.war;
    if (v.setPlayer) {
        void* f = Frm();
        if (f) dl::At<void*>(f, A.fPlayer) = v.prevPlayer;
    }
    v.active = false;
}

static bool ViewAtWar() { return g_v.active && *A.warVar > 0; }

// Calls into the game inside a view: a game exception that unwinds through here puts the view back first.
static uint32_t ViewCall(void* fn, uint32_t eax, uint32_t edx = 0, uint32_t ecx = 0) {
    uint32_t r = 0;
    __try {
        r = dl::Call(fn, eax, edx, ecx);
    } __finally {
        if (AbnormalTermination()) ViewEnd();
    }
    return r;
}

// Foreign stations as CheckForceOnForeignStations computes them for its player: per map area the force the nation is
// short of (HasRequiredForceInArea(area, False) < 0, summed), the required tonnage of those areas, and the colonial
// tonnage of its operational ships with Foreign Stations status (ssColonies = 6), which covers any area.
struct Stations {
    int shortfall = 0, required = 0, fsTonnage = 0, areas = 0;
    bool insufficient = false;
};

static Stations StationsOf(void* nation) {
    Stations s;
    void* f = Frm();
    void* areas = f ? dl::At<void*>(f, A.fMapAreas) : nullptr;
    for (int i = 0; i < dl::ListCount(areas); i++) {
        void* area = dl::ListItem(areas, i);
        int h = (int)dl::Call(A.mHasForce, (uint32_t)(uintptr_t)nation, (uint32_t)(uintptr_t)area, 0);
        if (h >= 0) continue;
        s.shortfall += h;
        s.required += (int)dl::Call(A.mReqTonnage, (uint32_t)(uintptr_t)nation, (uint32_t)(uintptr_t)area);
        s.areas++;
    }
    for (int i = 0; i < game::ShipCount(nation); i++) {
        void* ship = game::Ship(nation, i);
        if (!ship || dl::At<uint8_t>(ship, A.fShipStatus) != 6) continue;
        if (!(dl::Call(A.mIsOperational, (uint32_t)(uintptr_t)ship) & 0xFF)) continue;
        s.fsTonnage += (int)dl::Call(A.mColonialTonnage, (uint32_t)(uintptr_t)ship);
    }
    s.insufficient = -s.shortfall > s.fsTonnage;
    return s;
}

// The game's Yes/No question (TMessageForm "Confirm", mtConfirmation, [mbYes, mbNo]); true = Yes.
static bool AskYesNo(const std::string& text) {
    std::wstring w = U2W(text);
    for (auto& c : w)
        if (c == L'\n') c = L'\r';
    if (A.mMessageDlg) {
        dl::ConstUStr s(w);
        uint32_t stack[4] = {0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0};  // HelpCtx, X, Y, HelpFileName
        int r = (int)dl::Call(A.mMessageDlg, s.ptr(), 3, 3, 4, stack);
        return r == 6;  // mrYes
    }
    return MessageBoxW(game::FormHwnd(Frm()), w.c_str(), L"Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES;
}

// ---------------------------------------------------------------------------------------------
// Host: the records of the joined players' nations

struct Rec {
    int spend[kNations] = {};
    int effort[kNations] = {};
    std::vector<std::string> reports;  // as the game's list holds them: "<Mon yyyy>  <nation> - <text>"
    int fsWarned = 0;                  // the game's LeaderWarnedAboutForeignStations, for this nation
    bool hasDivisions = false;         // 0.6.1: the nation's campaign divisions from its last turn that sent them
    std::vector<std::string> divisions;  // ... as [CampaignDivisions] lines (validated, renumbered)
};
static std::map<int, Rec> g_recs;
static std::wstring g_file;
static std::string g_key;
static bool g_attached = false;
static std::function<void(int, const std::string&)> g_news;

void SetNewsSink(std::function<void(int, const std::string&)> fn) { g_news = fn; }

static std::string Join(const int* v) {
    std::string s;
    for (int i = 0; i < kNations; i++) s += (i ? "," : "") + std::to_string(v[i]);
    return s;
}

static void SplitInts(const std::string& s, int* v) {
    auto f = Split(s, ',');
    for (int i = 0; i < kNations; i++) v[i] = i < (int)f.size() ? atoi(f[i].c_str()) : 0;
}

static std::vector<std::string> ReportLines(const IniSection* s) {
    std::vector<std::string> out;
    if (!s) return out;
    for (auto& l : s->lines) {
        size_t e = l.find('=');
        if (e == std::string::npos || l.compare(0, 5, "Intel") != 0) continue;
        std::string v = l.substr(e + 1);
        if (!v.empty()) out.push_back(v);
    }
    return out;
}

static void PutReports(IniDoc& d, const std::string& sec, const std::vector<std::string>& reports) {
    IniSection* s = d.Find(sec);
    if (!s) {
        d.sections.push_back({sec, {}});
        s = &d.sections.back();
    }
    s->lines.clear();
    s->lines.push_back("ReportNo=" + std::to_string(reports.size()));
    for (size_t i = 0; i < reports.size(); i++) s->lines.push_back("Intel" + std::to_string(i) + "=" + reports[i]);
}

static void HostSave() {
    if (!g_attached || g_file.empty()) return;
    IniDoc d;
    d.Set("General", "Campaign", g_key);
    for (auto& r : g_recs) {
        std::string sec = "Nation" + std::to_string(r.first);
        d.Set(sec, "IntelSpend", Join(r.second.spend));
        d.Set(sec, "IntelEffort", Join(r.second.effort));
        d.Set(sec, "ForeignStationsWarned", std::to_string(r.second.fsWarned));
        PutReports(d, sec + "IntelReports", r.second.reports);
        if (r.second.hasDivisions) d.sections.push_back({sec + "Divisions", r.second.divisions});
    }
    if (!WriteFileBytes(g_file, WriteIni(d))) Log("nations file: write FAILED (%s)", W2U(g_file).c_str());
}

void HostAttach(const std::wstring& slotDir, const std::string& campaignKey) {
    std::wstring file = slotDir + L"rtw3mp_nations.ini";
    if (g_attached && file == g_file && campaignKey == g_key) return;
    g_recs.clear();
    g_file = file;
    g_key = campaignKey;
    g_attached = true;
    std::string bytes;
    if (!ReadFileBytes(file, bytes)) return;
    IniDoc d = ParseIni(bytes);
    if (d.Get("General", "Campaign") != campaignKey) {
        Log("nations file belongs to another campaign; ignored");
        return;
    }
    for (int K = 1; K <= 8; K++) {
        std::string sec = "Nation" + std::to_string(K);
        if (!d.Find(sec)) continue;
        Rec& r = g_recs[K];
        SplitInts(d.Get(sec, "IntelSpend"), r.spend);
        SplitInts(d.Get(sec, "IntelEffort"), r.effort);
        r.fsWarned = atoi(d.Get(sec, "ForeignStationsWarned", "0").c_str());
        r.reports = ReportLines(d.Find(sec + "IntelReports"));
        if (const IniSection* ds = d.Find(sec + "Divisions")) {
            r.hasDivisions = true;
            for (auto& l : ds->lines)
                if (!l.empty()) r.divisions.push_back(l);
        }
    }
    Log("nations file: %d joined nations' records loaded", (int)g_recs.size());
}

void HostDetach() {
    g_recs.clear();
    g_file.clear();
    g_key.clear();
    g_attached = false;
}

static Rec& RecOf(int K) { return g_recs[K]; }

void HostTransformState(Bundle& b, int K) {
    if (!A.intel || !g_attached || K < 1 || K > 8) return;
    for (auto& f : b.files) {
        if (f.first != "RTWGame{S}.bcs") continue;
        IniDoc d = ParseIni(f.second);
        Rec& r = RecOf(K);
        for (int j = 0; j <= 9; j++) {
            std::string sec = "Nation" + std::to_string(j);
            if (!d.Find(sec)) continue;
            bool mine = j != K && j < kNations;
            d.Set(sec, "IntelligenceSpending", std::to_string(mine ? r.spend[j] : 0));
            d.Set(sec, "IntelEffort", std::to_string(mine ? r.effort[j] : 0));
        }
        PutReports(d, "IntelReports", r.reports);
        f.second = WriteIni(d);
        Log("intel: nation %d's own intelligence in its month (spending %s, effort %s, %d reports)", K,
            Join(r.spend).c_str(), Join(r.effort).c_str(), (int)r.reports.size());
        return;
    }
}

void HostTakeSubmission(const Bundle& sub, int K) {
    if (!A.intel || !g_attached || K < 1 || K > 8) return;
    const std::string* nb = sub.Get("nation.bcs");
    if (!nb) return;
    IniDoc d = ParseIni(*nb);
    if (!d.Find("MPIntel")) return;  // a player whose mod doesn't send it (0.4.0): its record stays
    Rec& r = RecOf(K);
    SplitInts(d.Get("MPIntel", "Spend"), r.spend);
    SplitInts(d.Get("MPIntel", "Effort"), r.effort);
    r.spend[K] = r.effort[K] = 0;
    for (int j = 0; j < kNations; j++) {
        r.spend[j] = std::max(0, std::min(3, r.spend[j]));
        r.effort[j] = std::max(0, std::min(3, r.effort[j]));
    }
    if (d.Find("IntelReports")) r.reports = ReportLines(d.Find("IntelReports"));
    HostSave();
    int cost = 0;
    for (int j = 0; j < kNations; j++) cost += r.spend[j];
    Log("intel: nation %d's turn: spending %s (cost %d a month), effort %s, %d reports", K, Join(r.spend).c_str(),
        cost * 10 * FleetSize(), Join(r.effort).c_str(), (int)r.reports.size());
}

void HostTakeIntelEvent(const KV& kv, int K, std::string& applied) {
    if (!A.intel || !g_attached || K < 1 || K > 8 || !KVInt(kv, "intel")) return;
    Rec& r = RecOf(K);
    int added = 0;
    for (auto& line : Split(KVStr(kv, "ireports"), '\x1e')) {
        std::string l = OneLine(line);
        if (l.empty() || std::find(r.reports.begin(), r.reports.end(), l) != r.reports.end()) continue;
        r.reports.push_back(l);
        added++;
    }
    if (!KVStr(kv, "ieffort").empty()) {
        SplitInts(KVStr(kv, "ieffort"), r.effort);
        r.effort[K] = 0;
        for (int j = 0; j < kNations; j++) r.effort[j] = std::max(0, std::min(3, r.effort[j]));
    }
    HostSave();
    applied += " intel: " + std::to_string(added) + " reports, effort " + Join(r.effort);
}

// Campaign divisions. A joined player's game loads the divisions of its slot against its nation (game.cpp,
// d_LoadDivisions), saves them with its turn, and the host keeps them for the nation: the host's own month only ever
// sees (and changes) the host's divisions.
void HostTakeDivisions(const Bundle& sub, int K, bool fresh) {
    if (!g_attached || K < 1 || K > 8) return;
    const std::string* f = sub.Get("divisions.ini");
    IniDoc d = f ? ParseIni(*f) : IniDoc();
    const IniSection* s = f ? d.Find("CampaignDivisions") : nullptr;
    std::vector<saveio::CampDiv> divs;
    std::string err = f ? "no [CampaignDivisions]" : "none sent";
    if (!s || !saveio::ParseDivisions(s->lines, divs, true, err)) {
        if (fresh) {  // a legacy turn: the stored list names the replaced fleet's ships
            Rec& r = RecOf(K);
            r.divisions = saveio::DivisionLines({});
            r.hasDivisions = true;
            HostSave();
            Log("divisions of nation %d: the legacy turn's list unusable (%s); none stored", K, err.c_str());
            return;
        }
        // (no list: a player's mod that doesn't send them, 0.6.0 and older)
        if (f) Log("divisions of nation %d: the turn's list rejected (%s); the stored one stays", K, err.c_str());
        return;
    }
    saveio::DivPrune pr = saveio::PruneDivisions(divs, nullptr);
    Rec& r = RecOf(K);
    r.divisions = saveio::DivisionLines(divs);
    r.hasDivisions = true;
    HostSave();
    Log("divisions of nation %d: %d stored%s (repeats dropped: %d divisions, %d ship ids; %d lead divisions reset)", K,
        (int)divs.size(), fresh ? " for its legacy fleet" : "", pr.divisions, pr.ships, pr.leads);
}

int HostPutDivisions(Bundle& b, int K, bool own, const char* what) {
    if (K < 1 || K > 8) return -1;
    std::string* off = nullptr;
    const std::string* bcs = nullptr;
    for (auto& f : b.files) {
        if (f.first == "RTWGame{S}.off") off = &f.second;
        if (f.first == "RTWGame{S}.bcs") bcs = &f.second;
    }
    if (!off || !bcs) {
        Log("divisions of nation %d: the %s has no .off; left as it is", K, what);
        return -1;
    }
    auto it = g_recs.find(K);
    bool stored = own && g_attached && it != g_recs.end() && it->second.hasDivisions;
    std::vector<saveio::CampDiv> divs;
    std::string err, summary;
    if (stored) saveio::ParseDivisions(it->second.divisions, divs, false, err);
    int n = saveio::PutDivisions(*off, *bcs, K, divs, summary);
    if (!own)
        Log("divisions of nation %d: an empty list in its %s (the player's mod can't load the nation's own)", K, what);
    else
        Log("divisions of nation %d: %d in its %s (%s)", K, n, what, stored ? summary.c_str() : "none stored yet");
    return n;
}

static std::string DivisionListText(const std::vector<saveio::CampDiv>& divs) {
    std::string s;
    for (size_t i = 0; i < divs.size(); i++) {
        std::string name = divs[i].Get("Name");
        for (char& c : name)
            if (c == '|' || c == ':') c = '_';
        std::string ids;
        for (size_t j = 0; j < divs[i].ships.size(); j++) ids += (j ? "." : "") + divs[i].ships[j];
        s += (i ? "|" : "") + divs[i].Get("CDivId") + ":" + name + ":" + std::to_string(divs[i].ships.size()) + ":" + ids;
    }
    return s;
}

std::string HostDivisionsInfo(int K) {
    if (!g_attached) return "error the joined nations' records are not loaded (host only)";
    auto it = g_recs.find(K);
    std::vector<saveio::CampDiv> divs;
    std::string err;
    if (it != g_recs.end() && it->second.hasDivisions) saveio::ParseDivisions(it->second.divisions, divs, false, err);
    return "ok n=" + std::to_string(divs.size()) + " list=" + DivisionListText(divs);
}

static bool HostIntelCost(int K, int& cost) {
    if (!A.intel || !g_attached || K < 1 || K > 8 || !hookcb::IsHumanRemoteNation(K)) return false;
    Rec& r = RecOf(K);
    int levels = 0;
    for (int j = 0; j < kNations; j++)
        if (j != K) levels += r.spend[j];
    cost = levels * 10 * FleetSize();
    return true;
}

std::string HostDebug() {
    std::string s;
    for (auto& r : g_recs)
        s += " rec" + std::to_string(r.first) + "=spend:" + Join(r.second.spend) + "/effort:" + Join(r.second.effort) +
             "/reports:" + std::to_string(r.second.reports.size()) + "/fsWarned:" + std::to_string(r.second.fsWarned);
    return s;
}

// The month (host): the player's part of CheckShipsOnForeignStations (peace) and of CheckPatrols (war), for each joined
// player's nation, as the game does it for its player. The game shows these as messages and changes the player's own
// prestige and possessions; here they go to the player as the month's news.
struct MonthCheck {
    bool atWar, blockaded;
    int patrols, required;
    Stations st;
};

// K's trade protection (patrols and the requirement, at war) and foreign stations, with K seen as the player. `what`:
// 1 = patrols when at war, 2 = stations when at peace, 4 = stations unless at war and blockaded, 8 = everything.
static bool EvalAsPlayer(int K, bool setPlayer, int what, MonthCheck& c) {
    if (!ViewBegin(K, setPlayer)) return false;
    void* f = Frm();
    void* n = g_v.obj[K];
    c.atWar = ViewAtWar();
    c.blockaded = dl::At<uint8_t>(n, A.fBlockaded) != 0;
    c.patrols = c.required = 0;
    __try {
        if ((what & 8) || ((what & 1) && c.atWar)) {
            c.patrols = (int)dl::Call(A.mCountPatrol, (uint32_t)(uintptr_t)n, 0);
            c.required = (int)dl::Call(A.mPatrolReq, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)n);
        }
        if ((what & 8) || ((what & 2) && !c.atWar) || ((what & 4) && !(c.atWar && c.blockaded))) c.st = StationsOf(n);
    } __finally {
        ViewEnd();
    }
    return true;
}

static bool MonthCheckIn(int K, MonthCheck& c) { return EvalAsPlayer(K, true, 1 | 2, c); }

static int g_newsLines = 0;

static void News(int K, const std::string& line) {
    Log("nation %d month: %s", K, line.c_str());
    g_newsLines++;
    if (g_news) g_news(K, line);
}

static void HostMonthCheck(int K) {
    MonthCheck c = {};
    if (!MonthCheckIn(K, c)) return;
    void* n = game::Nation(K);
    Rec& r = RecOf(K);
    if (c.atWar) {
        // CheckPatrols: shortfall > Random(40) -> the leader's complaint (the game changes nothing else here).
        int lack = c.required - c.patrols;
        Log("nation %d month: at war, %d ships on trade protection of %d required", K, c.patrols, c.required);
        if (lack > 0 && lack > Rnd(40))
            News(K, Leader("The YY is annoyed that you cannot safeguard our coasts with adequate patrols. Your prestige "
                           "suffers. (Not enough ships on trade protection: ", n) +
                        std::to_string(c.patrols) + " of " + std::to_string(c.required) + ")");
        return;
    }
    Log("nation %d month: at peace, foreign stations short of %d (required %d) in %d areas, ships on foreign stations %d%s",
        K, -c.st.shortfall, c.st.required, c.st.areas, c.st.fsTonnage, c.st.insufficient ? ": insufficient" : "");
    if (!c.st.insufficient) return;
    // CheckForceOnForeignStations(Warn=False), the player's part (the world events after it are the host's game's).
    int war = game::WarCounter(), y = Year(), m = Month();
    bool any = false;
    for (int j = 0; j < kNations; j++)
        if (j != K && game::AtWar(K, j)) any = true;
    int ownWar = any ? 1 : war > 0 ? -12 : war;
    auto quiet = [&](const char* why) {
        Log("nation %d month: no consequence this month (%s; war counter %d, %d/%02d)", K, why, ownWar, y, m);
    };
    if (ownWar >= -4 && Rnd(100) >= 20) return quiet("a recent war: one month in five");
    if (m == 1) return quiet("never in January");
    bool early = (y == 1900 || y == 1890) && m < 3;
    int lack = -c.st.shortfall, T = c.st.required;
    if (Rnd(2 * T) < lack) {
        if (r.fsWarned) {
            if (early) return quiet("not before March 1900");
            int& p = IntAt(n, A.fPrestige);
            int before = p;
            p = std::max(0, p - 1);
            News(K, Leader("The YY is displeased with the lack of ships on foreign stations! Your prestige suffers.", n) +
                        " (Prestige " + std::to_string(before) + " -> " + std::to_string(p) + ")");
            r.fsWarned = 0;
        } else {
            News(K, Leader("The YY has been made aware of a lack of ships on foreign stations. He strongly urges you to "
                           "remedy the situation.", n));
            r.fsWarned = 1;
        }
        HostSave();
    } else if (lack > Rnd(2 * T)) {
        if (early) return quiet("not before March 1900");
        void* area = nullptr;
        void* poss = (void*)(uintptr_t)dl::Call(A.mRandomPossession, (uint32_t)(uintptr_t)Frm(), (uint32_t)(uintptr_t)n,
                                                (uint32_t)(uintptr_t)&area);
        if (!poss || !area) return quiet("rebellion roll: no possession");
        if ((int)dl::Call(A.mHasForce, (uint32_t)(uintptr_t)n, (uint32_t)(uintptr_t)area, 1) >= 0)
            return quiet("rebellion roll: the possession's area has its force");
        if (IntAt(poss, A.fPossRebellion) != 0) return quiet("rebellion roll: already in rebellion");
        void* owner = dl::At<void*>(poss, A.fPossOwner);
        if (!owner || dl::ReadUStr(dl::At<void*>(poss, A.fPossNatural)) == NameOf(owner))
            return quiet("rebellion roll: a home possession");
        IntAt(poss, A.fPossRebellion) = Rnd(10) + 2;
        News(K, "A rebellion has broken out in our possession " + W2U(dl::ReadUStr(dl::At<void*>(poss, A.fPossName))) +
                    " ! The lack of naval forces on station has contributed to the spread of unrest and sedition.");
    } else if (lack > Rnd(T) && Rnd(100) < 50) {
        News(K, Rnd(100) < 50 ? "Influential trading companies are complaining that our naval presence in the colonies "
                                "is insufficient to keep order."
                              : "The navy league is pushing for more ships on foreign stations.");
    } else {
        quiet("the rolls");
    }
}

static void HostMonthChecks() {
    if (!A.trade || !hookcb::InSession()) return;
    for (int K = 1; K <= 8 && K < game::NationCount(); K++)
        if (hookcb::IsHumanRemoteNation(K)) HostMonthCheck(K);
}

// ---------------------------------------------------------------------------------------------
// Client

static int g_clientK = 0, g_clientSlot = 0;
static bool g_hostIntel = false;

// The player's and the host's nation objects (a load makes new ones; inside a view, the ones it swapped).
static void* KObj() {
    if (g_v.active) return g_v.obj[g_v.K];
    void** arr = NationArr();
    return arr && g_clientK >= 1 && g_clientK <= 8 ? arr[g_clientK] : nullptr;
}
static void* HostObj() {
    if (g_v.active) return g_v.obj[0];
    void** arr = NationArr();
    return arr ? arr[0] : nullptr;
}

void ClientSetMonth(int K, bool hostIntel, int clientSlot) {
    g_clientK = K;
    g_clientSlot = clientSlot;
    g_hostIntel = hostIntel && A.intel;
    Log("client month: nation %d, per-player intelligence %s", K,
        g_hostIntel ? "on" : hostIntel ? "UNAVAILABLE on this PC" : "off (the host's mod keeps the 0.4.0 behaviour)");
}

void ClientReset() {
    g_clientK = g_clientSlot = 0;
    g_hostIntel = false;
}

static bool ClientViewOk() {
    return g_clientK > 0 && game::CurrentSlot() == g_clientSlot && KObj() && game::PlayerNation() == KObj();
}

bool ClientIntelActive() { return g_hostIntel && ClientViewOk(); }

static int IndexOfObj(void* o) {
    void** arr = NationArr();
    for (int i = 0; arr && i < kNations; i++)
        if (arr[i] == o) return i;
    return -1;
}

static const char* kOwnFields[] = {"Prestige", "BudgetModifier", "BaseResources", "Funds", "UnrestLevel", "TechLeakRisk",
                                   "Corruption"};
static const int kOwnCount = 7;

// CheckIntelligence with K seen as the player; tmpT/afterT: each nation's relation with K before/after (the game's
// spy events raise it), by the nation's index outside the view.
static bool RunCheckIntelAsPlayer(int K, int* tmpT, int* afterT, BOOL* hit) {
    void* f = Frm();
    if (!ViewBegin(K, false)) return false;
    for (int i = 0; i < kNations; i++) tmpT[i] = g_v.tmpT[i];
    __try {
        *hit = (dl::Call(A.mCheckIntel, (uint32_t)(uintptr_t)f) & 0xFF) != 0;
    } __finally {
        for (int i = 0; i < kNations; i++) afterT[i] = IntAt(g_v.obj[i], A.fTension);
        ViewEnd();
    }
    return true;
}

bool ClientRunIntel(int K, KV& out, std::vector<std::string>& newReports, std::string& summary) {
    if (!ClientIntelActive() || K != g_clientK) {
        summary = "unavailable";
        return false;
    }
    void* sl = IntelList();
    void** arr = NationArr();
    int ownOff[kOwnCount];
    for (int i = 0; i < kOwnCount; i++) ownOff[i] = dl::Field("TBuilderNation", kOwnFields[i]);
    if (!sl || !arr || std::count(ownOff, ownOff + kOwnCount, -1)) {
        summary = "unavailable";
        return false;
    }
    int n0 = SLCount(sl), effBefore[kNations], own[kOwnCount];
    void* obj[kNations];
    for (int j = 0; j < kNations; j++) {
        obj[j] = arr[j];
        effBefore[j] = IntAt(obj[j], A.fEffort);
    }
    for (int i = 0; i < kOwnCount; i++) own[i] = IntAt(obj[K], ownOff[i]);
    int tmpT[kNations], afterT[kNations];
    BOOL hit = FALSE;
    if (!RunCheckIntelAsPlayer(K, tmpT, afterT, &hit)) {
        summary = "busy";
        return false;
    }
    std::string ten, delta, reps;
    for (int i = 0; i < kNations; i++)
        if (i != K && afterT[i] != tmpT[i]) ten += std::to_string(i) + ":" + std::to_string(afterT[i] - tmpT[i]) + ";";
    for (int i = 0; i < kOwnCount; i++) {
        int d = IntAt(obj[K], ownOff[i]) - own[i];
        if (d) delta += std::string(kOwnFields[i]) + ":" + std::to_string(d) + ";";
    }
    bool effChanged = false;
    int effAfter[kNations];
    for (int j = 0; j < kNations; j++) {
        effAfter[j] = j == K ? 0 : IntAt(obj[j], A.fEffort);
        if (j != K && effAfter[j] != effBefore[j]) effChanged = true;
    }
    for (int i = n0; i < SLCount(sl); i++) {
        std::string s = OneLine(W2U(SLItem(sl, i)));
        if (s.empty()) continue;
        newReports.push_back(s);
        reps += (reps.empty() ? "" : "\x1e") + s;
    }
    summary = std::string(hit ? "spy event" : "no spy event") + ", " + std::to_string(newReports.size()) +
              " new reports, effort " + Join(effAfter) + (ten.empty() ? "" : ", tension " + ten) +
              (delta.empty() ? "" : ", own " + delta);
    if (ten.empty() && delta.empty() && newReports.empty() && !effChanged) return false;
    out["tension"] = ten;
    out["alliance"] = "";
    out["delta"] = delta;
    out["set"] = "";
    out["intel"] = "1";
    out["ireports"] = reps;
    out["ieffort"] = Join(effAfter);
    return true;
}

bool ClientTurnWarnings(int K) {
    if (!A.trade || !ClientViewOk() || K != g_clientK) return true;
    MonthCheck c = {};
    if (!EvalAsPlayer(K, false, 1 | 4, c)) return true;
    Log("turn checks for nation %d: %s, trade protection %d of %d, foreign stations short of %d, covered %d%s", K,
        c.atWar ? "at war" : "at peace", c.patrols, c.required, -c.st.shortfall, c.st.fsTonnage,
        c.st.insufficient ? " (insufficient)" : "");
    // PlayerChecksBeforeTurn's questions, in its order (asked with the view put back).
    if (c.atWar && c.patrols < c.required) {
        std::string q = "You have " + std::to_string(c.patrols) + " ships on trade protection duties, but " +
                        std::to_string(c.required) +
                        " are required.\rThis will give VP to the enemy and might hurt your prestige. Continue anyway?";
        bool yes = AskYesNo(q);
        Log("turn checks: trade protection question -> %s", yes ? "Yes" : "No");
        if (!yes) return false;
    }
    if (c.st.insufficient) {
        bool yes = AskYesNo("There are insufficient ships on foreign stations! Continue anyway?\rTip: You can set ships "
                            "to Foreign Stations status to satisfy the requirement without delay.");
        Log("turn checks: foreign stations question -> %s", yes ? "Yes" : "No");
        if (!yes) return false;
    }
    return true;
}

void ClientAddSubmission(Bundle& b, const std::wstring& slotDir, int slot, int K) {
    if (!g_hostIntel || K < 1 || K > 8) return;
    std::string bcs;
    if (!ReadFileBytes(slotDir + L"RTWGame" + std::to_wstring(slot) + L".bcs", bcs)) return;
    IniDoc own = ParseIni(bcs);
    int spend[kNations] = {}, effort[kNations] = {};
    for (int j = 0; j < kNations; j++) {
        if (j == K) continue;
        std::string sec = "Nation" + std::to_string(j);
        spend[j] = atoi(own.Get(sec, "IntelligenceSpending", "0").c_str());
        effort[j] = atoi(own.Get(sec, "IntelEffort", "0").c_str());
    }
    for (auto& f : b.files) {
        if (f.first != "nation.bcs") continue;
        IniDoc d = ParseIni(f.second);
        d.Set("MPIntel", "Spend", Join(spend));
        d.Set("MPIntel", "Effort", Join(effort));
        const IniSection* rep = own.Find("IntelReports");
        if (rep) d.sections.push_back(*rep);
        f.second = WriteIni(d);
        Log("submit: own intelligence (spending %s, effort %s, %d reports)", Join(spend).c_str(), Join(effort).c_str(),
            (int)ReportLines(rep).size());
        return;
    }
}

// ---------------------------------------------------------------------------------------------
// Hooks

extern "C" void* o_np_CostAI = nullptr;
extern "C" void* o_np_CostPlayer = nullptr;
extern "C" void* o_np_IntelToString = nullptr;
extern "C" void* o_np_ChangeIntel = nullptr;
extern "C" void* o_np_PatrolReq = nullptr;
extern "C" void* o_np_ShipsOnFS = nullptr;

// ComputeAINationIntelCost(Self, aNation): Integer, which GetMonthlyBalance ADDS to every nation's balance but the
// player's. A joined player's nation pays its own spending instead (returned negative), as the player does.
extern "C" int __stdcall NP_CostAI(void* nation, int* out) {
    __try {
        int cost = 0;
        if (HostIntelCost(game::NationIndex(nation), cost)) {
            *out = -cost;
            return 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

extern "C" __declspec(naked) void d_np_CostAI() {
    __asm {
        push eax
        push edx
        push ecx
        sub esp, 4
        push esp       // &out
        push edx       // aNation
        call NP_CostAI
        test eax, eax
        pop eax        // out
        pop ecx
        pop edx
        jnz mine
        pop eax
        jmp dword ptr [o_np_CostAI]
    mine:
        add esp, 4
        ret
    }
}

// GetIntelCostForPlayerNation(Self): Integer. On a joined player's PC: the player's own spending on every other nation,
// the host's included (the game sums nations 1..8).
extern "C" int __stdcall NP_CostPlayer(int* out) {
    __try {
        if (!ClientIntelActive()) return 0;
        void** arr = NationArr();
        void* me = game::PlayerNation();
        int levels = 0;
        for (int i = 0; arr && i < kNations; i++)
            if (arr[i] != me) levels += IntAt(arr[i], A.fSpend);
        *out = levels * 10 * FleetSize();
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

extern "C" __declspec(naked) void d_np_CostPlayer() {
    __asm {
        push eax
        push edx
        push ecx
        sub esp, 4
        push esp
        call NP_CostPlayer
        test eax, eax
        pop eax
        pop ecx
        pop edx
        jnz mine2
        pop eax
        jmp dword ptr [o_np_CostPlayer]
    mine2:
        add esp, 4
        ret
    }
}

// IntelToString(Self, aNationIdx, var Result): the label under a flag. On a joined player's PC the flag that names the
// player's own nation stands for the host's nation (spying on oneself means nothing; the panel has no other slot for
// the host): its label shows the player's spending on the host.
extern "C" int __stdcall NP_IntelIdx(int idx) {
    __try {
        if (!ClientIntelActive()) return idx;
        void** arr = NationArr();
        if (!arr || idx < 0 || idx >= kNations || arr[idx] != game::PlayerNation()) return idx;
        int h = IndexOfObj(HostObj());
        return h >= 0 ? h : idx;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return idx;
}

extern "C" __declspec(naked) void d_np_IntelToString() {
    __asm {
        push eax
        push ecx
        push edx
        call NP_IntelIdx
        mov edx, eax
        pop ecx
        pop eax
        jmp dword ptr [o_np_IntelToString]
    }
}

// ChangeIntel(Self, Sender): the +/- buttons (Tag 1..8 raise, 9..16 lower the spending on nation Tag or Tag-8, 0..3).
// On a joined player's PC the button under the player's own flag changes its spending on the host's nation.
static int ChangeIntelCore(void* sender, void** target, int* before, int* after) {
    __try {
        if (!ClientIntelActive() || !sender) return 0;
        int tag = IntAt(sender, A.fTag);
        if (tag < 1 || tag > 16) return 0;
        int idx = tag > 8 ? tag - 8 : tag;
        void** arr = NationArr();
        if (!arr) return 0;
        void* t = arr[idx];
        if (t == game::PlayerNation()) t = HostObj();
        if (!t) return 0;
        int& s = IntAt(t, A.fSpend);
        *before = s;
        if (tag <= 8 && s < 3) s++;
        if (tag > 8 && s > 0) s--;
        *after = s;
        *target = t;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

extern "C" int __stdcall NP_ChangeIntel(void* sender) {
    void* target = nullptr;
    int before = 0, after = 0;
    if (!ChangeIntelCore(sender, &target, &before, &after)) return 0;
    try {
        Log("intel: spending on %s %d -> %d", W2U(NameOf(target)).c_str(), before, after);
    } catch (...) {
    }
    return 1;
}

extern "C" __declspec(naked) void d_np_ChangeIntel() {
    __asm {
        push eax
        push ecx
        push edx
        push edx
        call NP_ChangeIntel
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz done
        jmp dword ptr [o_np_ChangeIntel]
    done:
        ret
    }
}

// GetPatrolRequirement(Self, aNation): Integer. For PlayerNation it counts the submarines of the nations at war with
// nation 0 (their Tension); on a joined player's PC it is called for the player's nation (the budget panel's "N of M"),
// so the requirement is computed with the player seen as nation 0: its own enemies.
extern "C" int __stdcall NP_PatrolReq(void* self, void* nation, int* out) {
    if (ViewActive() || !A.trade || !ClientViewOk() || nation != game::PlayerNation()) return 0;
    if (!ViewBegin(g_clientK, false)) return 0;
    *out = (int)ViewCall(o_np_PatrolReq, (uint32_t)(uintptr_t)self, (uint32_t)(uintptr_t)nation);
    ViewEnd();
    return 1;
}

extern "C" __declspec(naked) void d_np_PatrolReq() {
    __asm {
        push eax
        push edx
        push ecx
        sub esp, 4
        push esp
        push edx
        push eax
        call NP_PatrolReq
        test eax, eax
        pop eax
        pop ecx
        pop edx
        jnz mine3
        pop eax
        jmp dword ptr [o_np_PatrolReq]
    mine3:
        add esp, 4
        ret
    }
}

// CheckShipsOnForeignStations(Self): the month's foreign stations (and DoStrategicTurn's CheckPatrols right after it).
extern "C" void __stdcall NP_AfterShipsOnFS() {
    __try {
        HostMonthChecks();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("exception in the joined nations' month checks");
    }
}

extern "C" __declspec(naked) void d_np_ShipsOnFS() {
    __asm {
        call dword ptr [o_np_ShipsOnFS]
        call NP_AfterShipsOnFS
        ret
    }
}

static void HookOne(void* target, void* detour, void** orig, const char* name) {
    if (!target) return;
    MH_STATUS st = MH_CreateHook(target, detour, orig);
    if (st != MH_OK)
        Log("hook %s: MH_CreateHook failed %d", name, (int)st);
    else
        Log("hook %s at rva %#x", name, (unsigned)((uintptr_t)target - dl::g_base));
}

void InstallHooks() {
    if (A.intel) {
        HookOne(A.mCostAI, (void*)&d_np_CostAI, &o_np_CostAI, "ComputeAINationIntelCost");
        HookOne(A.mCostPlayer, (void*)&d_np_CostPlayer, &o_np_CostPlayer, "GetIntelCostForPlayerNation");
        HookOne(A.mIntelToString, (void*)&d_np_IntelToString, &o_np_IntelToString, "IntelToString");
        HookOne(A.mChangeIntel, (void*)&d_np_ChangeIntel, &o_np_ChangeIntel, "ChangeIntel");
    }
    if (A.trade) {
        HookOne(A.mPatrolReq, (void*)&d_np_PatrolReq, &o_np_PatrolReq, "GetPatrolRequirement");
        HookOne(A.mShipsOnFS, (void*)&d_np_ShipsOnFS, &o_np_ShipsOnFS, "CheckShipsOnForeignStations");
    }
}

// ---------------------------------------------------------------------------------------------
// Tests

static std::string StaticText(void* ctl) {
    if (!ctl) return "";
    wchar_t buf[128] = {};
    if (A.mGetTextBuf) {
        dl::Call(A.mGetTextBuf, (uint32_t)(uintptr_t)ctl, (uint32_t)(uintptr_t)buf, 127);
    } else if (A.fHandle >= 0) {
        HWND h = dl::At<HWND>(ctl, A.fHandle);
        if (h) GetWindowTextW(h, buf, 127);
    }
    buf[127] = 0;
    return Trim(W2U(buf));
}

std::string IntelInfo() {
    void* f = Frm();
    void** arr = NationArr();
    if (!A.intel || !f || !arr) return "error unavailable";
    std::string s = "ok fields=";
    for (int i = 0; i < kNations; i++)
        s += std::to_string(i) + ":" + std::to_string(IntAt(arr[i], A.fSpend)) + "/" + std::to_string(IntAt(arr[i], A.fEffort)) +
             (i + 1 < kNations ? "," : "");
    s += " cost=" + std::to_string((int)dl::Call(A.mCostPlayer, (uint32_t)(uintptr_t)f));  // through the hook
    s += " budget=" + StaticText(A.fStIntelligence >= 0 ? dl::At<void*>(f, A.fStIntelligence) : nullptr);
    int fBal = dl::Field("TfrmBuildCamp", "stMonthlyBalance");
    s += " balance=" + StaticText(fBal >= 0 ? dl::At<void*>(f, fBal) : nullptr);
    s += " labels=";
    for (int i = 1; i <= 8; i++) {
        void* res = nullptr;
        dl::Call(A.mIntelToString, (uint32_t)(uintptr_t)f, (uint32_t)i, (uint32_t)(uintptr_t)&res);  // through the hook
        s += W2U(dl::ReadUStr(res)) + (i < 8 ? "|" : "");
    }
    void* sl = IntelList();
    s += " reports=" + std::to_string(SLCount(sl));
    s += " client=" + std::to_string(ClientIntelActive() ? 1 : 0);
    // Host: what the month adds to each joined nation's balance for intelligence (ComputeAINationIntelCost through the
    // hook; negative = its own spending) and its whole monthly balance (GetMonthlyBalance).
    void* bal = dl::Method("TBuilderNation", "GetMonthlyBalance");
    for (int K = 1; K <= 8 && K < game::NationCount(); K++) {
        if (!hookcb::IsHumanRemoteNation(K)) continue;
        s += " month" + std::to_string(K) + "=intel:" +
             std::to_string((int)dl::Call(A.mCostAI, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)arr[K])) +
             "/balance:" + (bal ? std::to_string((int)dl::Call(bal, (uint32_t)(uintptr_t)arr[K])) : std::string("?"));
    }
    s += HostDebug();
    for (char& c : s)
        if (c == ';') c = ',';
    return s;
}

std::string TradeCheck(int K) {
    if (!A.trade) return "error unavailable";
    if (K <= 0) K = g_clientK;
    if (K < 1 || K > 8) return "error no nation";
    bool onHost = game::PlayerNation() != game::Nation(K);
    MonthCheck c = {};
    if (!EvalAsPlayer(K, onHost, 8, c)) return "error busy";
    char buf[320];
    int fPatrol = dl::Field("TfrmBuildCamp", "stPatrol");
    void* f = Frm();
    std::string shown = f && fPatrol > 0 ? StaticText(dl::At<void*>(f, fPatrol)) : "";
    for (char& ch : shown)
        if (ch == ' ') ch = '_';
    snprintf(buf, sizeof(buf),
             "ok nation=%d war=%d blockaded=%d patrols=%d required=%d short=%d requiredTonnage=%d areas=%d fs=%d "
             "insufficient=%d prestige=%d fsWarned=%d year=%d month=%d panel=%s",
             K, c.atWar ? 1 : 0, c.blockaded ? 1 : 0, c.patrols, c.required, -c.st.shortfall, c.st.required, c.st.areas,
             c.st.fsTonnage, c.st.insufficient ? 1 : 0, IntAt(game::Nation(K), A.fPrestige),
             g_recs.count(K) ? g_recs[K].fsWarned : -1, Year(), Month(), shown.c_str());
    return buf;
}

std::string MonthCheckRun(int K, int count) {
    if (!A.trade || !g_attached || !hookcb::InSession()) return "error unavailable (host in a session only)";
    void* n = game::Nation(K);
    if (K < 1 || K > 8 || !n) return "error no nation";
    int p0 = IntAt(n, A.fPrestige), news0 = g_newsLines;
    for (int i = 0; i < count; i++) HostMonthCheck(K);
    return "ok runs=" + std::to_string(count) + " news=" + std::to_string(g_newsLines - news0) + " prestige=" +
           std::to_string(p0) + "->" + std::to_string(IntAt(n, A.fPrestige)) + " fsWarned=" + std::to_string(RecOf(K).fsWarned);
}

std::string ShipStatus(int nation, int ship, int status) {
    void* s = game::Ship(game::Nation(nation), ship);
    if (!s || A.fShipStatus < 0) return "error no such ship";
    if (status >= 0 && status <= 11) dl::At<uint8_t>(s, A.fShipStatus) = (uint8_t)status;
    return "ok status=" + std::to_string(dl::At<uint8_t>(s, A.fShipStatus));
}

std::string IntelButton(int tag) {
    void* f = Frm();
    if (!f || !A.mSpeedClick || tag < 1 || tag > 16) return "error unavailable";
    for (int i = 1; i <= 16; i++) {
        void* b = A.fSpeedButton[i] > 0 ? dl::At<void*>(f, A.fSpeedButton[i]) : nullptr;
        if (b && IntAt(b, A.fTag) == tag) {
            dl::Call(A.mSpeedClick, (uint32_t)(uintptr_t)f, (uint32_t)(uintptr_t)b);  // the button's OnClick
            return "ok SpeedButton" + std::to_string(i);
        }
    }
    return "error no button with that tag";
}

}  // namespace nplay
