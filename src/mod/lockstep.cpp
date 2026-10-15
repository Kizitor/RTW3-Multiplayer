// Live player-vs-player tactical battles in lockstep (see lockstep.h and the battle investigation of 2026-10-14).
//
// Both PCs run the same battle: the authority A (side 0) snapshots the battle (the game's own battle save .sac + the
// campaign with ScenarioInProgress), sends it to B (side 1), and both load it with the same RNG seed. From then on only
// orders and clock decisions travel:
//   - the clock belongs to A: every battle minute is released by A ("rel") and runs on both PCs through the game's own
//     Run-one-minute routine; minute starts that A did not release are blocked;
//   - orders are field values of the divisions/ships/forces of the player's own side; a guard at every simulation step
//     turns local edits into numbered batches, and each release says which batches both apply before the minute;
//   - after every minute both PCs hash the battle state; B sends its hash with "done", A compares and resyncs from a new
//     snapshot when they differ;
//   - display code draws its random numbers from a local stream, the simulation from the shared seeded stream; the
//     battle's human side (PlayerSide) is canonical (0) inside the simulation and the player's own side in the display.
#include "lockstep.h"
#include "delphi.h"
#include "game.h"
#include "saveio.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <intrin.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <random>
#include <unordered_map>

namespace lockstep {

static bool InImage(uintptr_t a) { return a >= dl::g_base && a < dl::g_base + dl::g_size; }
static void* CallTarget(uint8_t* e8) { return e8 + 5 + *(int32_t*)(e8 + 1); }
static uint32_t U(const void* p) { return (uint32_t)(uintptr_t)p; }

// `mov reg, [cell]` in Delphi code loads the address of a unit variable from a cell; returns that address.
static void* VarFromCell(uint8_t* operand) {
    uint32_t cell = *(uint32_t*)operand;
    if (!InImage(cell)) return nullptr;
    uint32_t var = *(uint32_t*)(uintptr_t)cell;
    return InImage(var) ? (void*)(uintptr_t)var : nullptr;
}

static uint8_t* Find(void* fn, size_t len, const std::vector<int>& pat, int nth = 0) {
    if (!fn) return nullptr;
    uint8_t* p = (uint8_t*)fn;
    uint8_t* end = p + len;
    while (p < end) {
        uint8_t* hit = dl::FindPattern(p, end - p, pat);
        if (!hit) return nullptr;
        if (nth-- == 0) return hit;
        p = hit + 1;
    }
    return nullptr;
}

// A game call; a Delphi exception escaping it counts as a failure.
static uint32_t SafeCall(void* fn, uint32_t a, uint32_t d, uint32_t c, int n, const uint32_t* s, bool* ok) {
    __try {
        uint32_t r = dl::Call(fn, a, d, c, n, s);
        if (ok) *ok = true;
        return r;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (ok) *ok = false;
        return 0;
    }
}

// ---------------------------------------------------------------------------------------------
// The engine: the battle's routines, variables and fields (1.01.44; everything resolved at runtime)

static struct Engine {
    bool tried = false, ok = false;
    std::string missing;
    uint32_t* randCell = nullptr;  // System.Random32Proc
    void** frmMainVar = nullptr;   // the battle form (TfrmMain) unit variable
    void **forcesVar = nullptr, **divisionsVar = nullptr, **airVar = nullptr, **torpVar = nullptr, **allShipsVar = nullptr;
    int *playerSideVar = nullptr, *minuteVar = nullptr, *intervalSecVar = nullptr;
    double* clockVar = nullptr;  // battle date/time (TDateTime)
    uint8_t* prefs = nullptr;    // the game's preferences record (battle options)
    void *startRun = nullptr, *setSpeed = nullptr, *startMinute = nullptr, *eotp = nullptr, *moveTorps = nullptr,
         *moveShips = nullptr, *saveScenario = nullptr, *endBattle = nullptr, *msgDlg = nullptr;
    void *mSaveCampaign = nullptr, *mCreateForm = nullptr, *mFree = nullptr, *mDoBattle = nullptr, *mDetachShip = nullptr,
         *mShowPic = nullptr, *mExecGeneral = nullptr, *mFillShipTree = nullptr;
    int fMinutesLeft = -1, fFinished = -1, fPause = -1, fAutoRun = -1, fEndGame = -1, fGameSpeed = -1,
        fIntervals = -1, fEngaged = -1, fTvShips = -1;
    int fShips = -1, fShipName = -1, fDivName = -1, fShipSide = -1, fDivSide = -1, fForceSide = -1, fForceName = -1,
        fCShipRef = -1, fRunTimer = -1, fMoveTimer = -1, timerEnabled = -1, fBattleOpp = -1;
    int fFAI = -1, fDAI = -1, fHuman = -1, fSDAI = -1;
    // live sessions
    bool sessionOk = false;
    int* inProgVar = nullptr;      // what the Save button passes to SaveBuildCampaign (the battle in progress)
    void** buildCampVar = nullptr; // frmBuildCamp
    void** appVar = nullptr;       // Application
    void* vmtMain = nullptr;       // TfrmMain
    void *endNow = nullptr, *showResults = nullptr, *mCSDD = nullptr, *mHide = nullptr, *mClose = nullptr;
    int fCloseMain = -1;  // TfrmMain.CloseMainScreen: the results' "Exit battle" (EOTP then closes the screen)
    int* scenLenVar = nullptr;  // the scenario length (minutes; the absolute limit is length + min(length, 600))
    int fCbSpeed = -1, fTvShipsTree = -1;
} E;

static int g_speedGetIdx = -1;  // the speed combo box's GetItemIndex slot in its VMT (from cbGameSpeedChange)
static void* g_ustrAsg = nullptr;  // System._UStrAsg(var Dest; Source): refcounted string assignment
static int g_fIdType = -1, g_fIdClass = -1, g_fHidden = -1;
static bool DualOk() { return g_ustrAsg && g_fIdType >= 0 && g_fIdClass >= 0 && g_fHidden >= 0; }
// Sighting fields the battle screen's own drawing also writes (GlobeRender, for this PC's side): the simulation keeps its
// values, the display its own (and takes the simulation's new values), as for the identification fields.
struct GuardField {
    const char* name;
    int size;
    int off;
};
static GuardField g_guard[] = {
    {"SightingLevel", 4, -1}, {"RadarSightingLevel", 4, -1}, {"LastSighted", 4, -1}, {"SeenByHumanControlledShips", 1, -1}};
static const int kGuard = sizeof(g_guard) / sizeof(g_guard[0]);
static int g_guardHits = 0;  // display writes to these fields since the last boundary (kept out of the simulation)
static bool g_guardOn = true;  // tests (`ls guard 0`): the display's writes reach the simulation (as before 0.2.9)

static void Need(bool ok, const char* what) {
    if (!ok) {
        E.ok = false;
        E.missing += std::string(what) + " ";
    }
}

bool Resolve() {
    if (E.tried) return E.ok;
    E.tried = true;
    E.ok = true;
    uint8_t* hit = nullptr;
    // Random(range): push eax; call [Random32Proc]; pop edx; mul edx; mov eax, edx; ret
    hit = dl::FindPattern((uint8_t*)dl::g_base + 0x1000, dl::g_size - 0x1000,
                          {0x50, 0xFF, 0x15, -1, -1, -1, -1, 0x5A, 0xF7, 0xE2, 0x89, 0xD0, 0xC3});
    if (hit && InImage(*(uint32_t*)(hit + 3))) E.randCell = (uint32_t*)(uintptr_t) * (uint32_t*)(hit + 3);
    Need(E.randCell && InImage(*E.randCell), "Random32Proc");
    // MessageDlg: push ebp; mov ebp,esp; push ecx; mov [ebp-2],cx; mov ecx,[ebp+8]; push ecx; push -1; push -1; push 0;
    // movzx ecx,[ebp-2]; call MessageDlgPosHelp (every message box of the game goes through it)
    hit = dl::FindPattern((uint8_t*)dl::g_base + 0x1000, dl::g_size - 0x1000,
                          {0x55, 0x8B, 0xEC, 0x51, 0x66, 0x89, 0x4D, 0xFE, 0x8B, 0x4D, 0x08, 0x51, 0x6A, 0xFF, 0x6A, 0xFF,
                           0x6A, 0x00, 0x0F, 0xB7, 0x4D, 0xFE, 0xE8});
    if (hit) E.msgDlg = CallTarget(hit + 22);
    Need(E.msgDlg && InImage((uintptr_t)E.msgDlg), "MessageDlgPosHelp");

    E.mDoBattle = dl::Method("TfrmBuildCamp", "DoBattle");
    if ((hit = Find(E.mDoBattle, 0x200, {0x8B, 0x0D, -1, -1, -1, -1, 0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x8B, 0x15})))
        E.frmMainVar = (void**)VarFromCell(hit + 2);
    Need(E.frmMainVar != nullptr, "frmMain");

    void* autoRun = dl::Method("TfrmMain", "sbAutoRunBattleClick");
    if ((hit = Find(autoRun, 0x200, {0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x8B, 0x58, 0x08}, 0)))
        E.forcesVar = (void**)VarFromCell(hit + 1);
    if ((hit = Find(autoRun, 0x200, {0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x8B, 0x58, 0x08}, 1)))
        E.divisionsVar = (void**)VarFromCell(hit + 1);
    if ((hit = Find(autoRun, 0x200, {0x8B, 0x15, -1, -1, -1, -1, 0x3B, 0x02})))
        E.playerSideVar = (int*)VarFromCell(hit + 2);
    Need(E.forcesVar && E.divisionsVar && E.playerSideVar, "forces/divisions/playerSide");

    // All fighting ships: the first list the air formation dialog walks.
    void* airDlg = dl::Method("TdlgShowAirFormations", "UpdateListSortedByBase");
    if ((hit = Find(airDlg, 0x100, {0xA1, -1, -1, -1, -1, 0x8B, 0x00}))) E.allShipsVar = (void**)VarFromCell(hit + 1);
    // Air formations: the battle save routine that calls TAirFormation.SaveMe loads the list with `mov ebp, var`.
    if (uint8_t* saveMe = (uint8_t*)dl::Method("TAirFormation", "SaveMe")) {
        uint8_t* end = (uint8_t*)dl::g_base + dl::g_size - 5;
        for (uint8_t* p = (uint8_t*)dl::g_base + 0x1000; p < end && !E.airVar; p++) {
            if (*p != 0xE8 || CallTarget(p) != saveMe) continue;
            for (uint8_t* q = p - 0x90; q < p; q++)
                if (q[0] == 0xBD && InImage(*(uint32_t*)(q + 1)) && q[5] == 0xBA) {
                    E.airVar = (void**)(uintptr_t) * (uint32_t*)(q + 1);
                    break;
                }
        }
    }

    void* formCreate = dl::Method("TfrmMain", "FormCreate");
    if ((hit = Find(formCreate, 0x600, {0xA1, -1, -1, -1, -1, 0xDD, 0x18}))) E.clockVar = (double*)VarFromCell(hit + 1);
    Need(E.clockVar != nullptr, "clock");
    void* travel = dl::Method("TFightingShip", "GetTravelDistInGU");
    if ((hit = Find(travel, 0x40, {0xDB, 0x05, -1, -1, -1, -1})) && InImage(*(uint32_t*)(hit + 2)))
        E.intervalSecVar = (int*)(uintptr_t) * (uint32_t*)(hit + 2);

    void* play = dl::Method("TfrmMain", "sbPlayClick");
    if ((hit = Find(play, 0x40, {0xBA, 0x01, 0x00, 0x00, 0x00, 0x8B, 0xC3, 0xE8}))) E.startRun = CallTarget(hit + 7);
    void* speed = dl::Method("TfrmMain", "cbGameSpeedChange");
    if ((hit = Find(speed, 0x40, {0x89, 0x83, -1, -1, -1, -1, 0x8B, 0xC3, 0xE8}))) E.setSpeed = CallTarget(hit + 8);
    void* runTimer = dl::Method("TfrmMain", "tmrRunMinuteTimer");
    if ((hit = Find(runTimer, 0x40, {0x74, 0x07, 0x8B, 0xC3, 0xE8}))) E.startMinute = CallTarget(hit + 4);
    void* moveTimer = dl::Method("TfrmMain", "tmrMoveIntervalTimer");
    if ((hit = Find(moveTimer, 0x80, {0x7F, 0x08, 0x8B, 0x45, 0xFC, 0xE8}))) {
        E.moveTorps = CallTarget(hit + 5);
        if (hit[10] == 0x8B && hit[11] == 0x45 && hit[12] == 0xFC && hit[13] == 0xE8) E.moveShips = CallTarget(hit + 13);
    }
    if ((hit = Find(moveTimer, 0x200, {0x8B, 0x45, 0xFC, 0xE8, -1, -1, -1, -1, 0x8B, 0x45, 0xFC, 0x33, 0xD2, 0x89, 0x90})))
        E.eotp = CallTarget(hit + 3);
    if (E.moveTorps && (hit = Find(E.moveTorps, 0x30, {0x8B, 0x3D, -1, -1, -1, -1})))
        E.torpVar = (void**)VarFromCell(hit + 2);
    if (E.eotp && (hit = Find(E.eotp, 0x100, {0x8B, 0x0D, -1, -1, -1, -1, 0x8B, 0x09, 0xB8, 0x67, 0x66, 0x66, 0x66})))
        E.minuteVar = (int*)VarFromCell(hit + 2);
    // The preferences record: mov ecx, [prefs cell]; mov ecx, [ecx+46h] (the battle autosave interval) in EOTP.
    if (E.eotp && (hit = Find(E.eotp, 0x600, {0x8B, 0x0D, -1, -1, -1, -1, 0x8B, 0x49, 0x46})))
        E.prefs = (uint8_t*)VarFromCell(hit + 2);
    void* save = dl::Method("TfrmMain", "sbSaveClick");
    if ((hit = Find(save, 0x200, {0x8B, 0x55, 0xFC, 0x8B, 0x45, 0xF8, 0xE8}))) E.saveScenario = CallTarget(hit + 6);
    // sbEndGameClick: cmp byte ptr [eax+EndGame], 0; jne +6; call EndBattle; ret
    void* endClick = dl::Method("TfrmMain", "sbEndGameClick");
    if ((hit = Find(endClick, 0x20, {0x80, 0xB8, -1, -1, -1, -1, 0x00, 0x75, 0x06, 0xE8}))) E.endBattle = CallTarget(hit + 9);
    Need(E.startRun && E.setSpeed && E.startMinute && E.moveTorps && E.moveShips && E.eotp && E.saveScenario &&
             E.endBattle && E.minuteVar && E.prefs,
         "battle routines");

    E.mSaveCampaign = dl::Method("TfrmBuildCamp", "SaveBuildCampaign");
    E.mCreateForm = dl::Method("TApplication", "CreateForm");
    E.mFree = dl::Method("TObject", "Free");
    E.mDetachShip = dl::Method("TfrmMain", "DetachShip");
    E.mShowPic = dl::Method("TdlgPictureMessage", "ShowPicMessage");
    E.mExecGeneral = dl::Method("TdlgEventAnswer", "ExecuteGeneral");
    E.mFillShipTree = dl::Method("TfrmMain", "FillShipTree");
    Need(E.mSaveCampaign && E.mCreateForm && E.mFree && E.mDetachShip && E.mShowPic && E.mExecGeneral && E.mFillShipTree,
         "methods");

    E.fMinutesLeft = dl::Field("TfrmMain", "MinutesLeft");
    E.fFinished = dl::Field("TfrmMain", "Finished");
    E.fPause = dl::Field("TfrmMain", "PauseGame");
    E.fAutoRun = dl::Field("TfrmMain", "AutoRunBattle");
    E.fEndGame = dl::Field("TfrmMain", "EndGame");
    E.fGameSpeed = dl::Field("TfrmMain", "GameSpeed");
    E.fIntervals = dl::Field("TfrmMain", "Intervals");
    E.fEngaged = dl::Field("TfrmMain", "Engaged");
    E.fTvShips = dl::Field("TfrmMain", "tvShips");
    E.fRunTimer = dl::Field("TfrmMain", "tmrRunMinute");
    E.fMoveTimer = dl::Field("TfrmMain", "tmrMoveInterval");
    // sbPauseClick: mov edx, [eax+tmrRunMinute]; cmp byte ptr [edx+FEnabled], 0
    if ((hit = Find(dl::Method("TfrmMain", "sbPauseClick"), 0x20, {0x80, 0x7A, -1, 0x00}))) E.timerEnabled = hit[2];
    E.fShips = dl::Field("TDivision", "Ships");
    E.fDivName = dl::Field("TDivision", "Name");
    E.fDivSide = dl::Field("TDivision", "Side");
    E.fShipName = dl::Field("Tship", "Name");
    E.fShipSide = dl::Field("TFightingShip", "Side");
    E.fCShipRef = dl::Field("TFightingShip", "CShipRefId");
    E.fForceSide = dl::Field("TForce", "Side");
    E.fForceName = dl::Field("TForce", "Name");
    E.fFAI = dl::Field("TForce", "AIControlled");
    E.fDAI = dl::Field("TDivision", "AIControlled");
    E.fHuman = dl::Field("TFightingShip", "ForceIsHumanControlled");
    E.fSDAI = dl::Field("TFightingShip", "DivisionIsAIControlled");
    E.fBattleOpp = dl::Field("TfrmBuildCamp", "BattleOpponentIdx");
    Need(E.fMinutesLeft >= 0 && E.fFinished >= 0 && E.fPause >= 0 && E.fEndGame >= 0 && E.fGameSpeed >= 0 &&
             E.fShips >= 0 && E.fShipName >= 0 && E.fDivName >= 0 && E.fDivSide >= 0 && E.fShipSide >= 0 &&
             E.fForceSide >= 0 && E.fForceName >= 0 && E.fFAI >= 0 && E.fDAI >= 0 && E.fHuman >= 0 && E.fSDAI >= 0 &&
             E.timerEnabled >= 0 && E.fRunTimer >= 0 && E.fMoveTimer >= 0 && E.fBattleOpp >= 0,
         "fields");
    // Live sessions (the POC commands work without these).
    if ((hit = Find(save, 0x300, {0x6A, 0x00, 0x8B, 0x15, -1, -1, -1, -1, 0x8B, 0x12, 0xA1, -1, -1, -1, -1, 0x8B, 0x00,
                                  0x33, 0xC9, 0xE8}))) {
        E.inProgVar = (int*)VarFromCell(hit + 4);
        E.buildCampVar = (void**)VarFromCell(hit + 11);
        if (CallTarget(hit + 19) != E.mSaveCampaign) E.inProgVar = nullptr;
    }
    if ((hit = Find(E.mDoBattle, 0x200, {0x8B, 0x0D, -1, -1, -1, -1, 0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x8B, 0x15})))
        E.appVar = (void**)VarFromCell(hit + 7);
    E.vmtMain = dl::ClassVmt("TfrmMain");
    // the End button's routine: "Exit battle?" -> cmp eax, mrYes; jne ...; mov eax, ebx; call EndBattleNow
    if ((hit = Find(E.endBattle, 0x80, {0x83, 0xF8, 0x06, 0x0F, 0x85, -1, -1, -1, -1, 0x8B, 0xC3, 0xE8})))
        E.endNow = CallTarget(hit + 11);
    // EOTP's end of a battle: call SinkAllSinking; mov dl, 1; mov eax, [ebp-4]; call ShowResultScreen
    if ((hit = Find(E.eotp, 0x3000, {0xE8, -1, -1, -1, -1, 0xB2, 0x01, 0x8B, 0x45, 0xFC, 0xE8})))
        E.showResults = CallTarget(hit + 10);
    // EOTP's absolute time limit (tests: `ls endsoon`): cmp byte ptr [eax+EndGame], 0; jne; mov eax, [ScenarioLength];
    // cmp dword ptr [eax], 600
    if ((hit = Find(E.eotp, 0x3000, {0x80, 0xB8, -1, -1, 0x00, 0x00, 0x00, 0x75, -1, 0xA1, -1, -1, -1, -1, 0x81, 0x38,
                                     0x58, 0x02, 0x00, 0x00})))
        E.scenLenVar = (int*)VarFromCell(hit + 10);
    if (E.scenLenVar && (void*)E.scenLenVar == (void*)E.minuteVar) E.scenLenVar = nullptr;
    E.mCSDD = dl::Method("TfrmMain", "CheckSupportingDDToDetach");
    E.mHide = dl::Method("TCustomForm", "Hide");
    E.mClose = dl::Method("TCustomForm", "Close");
    E.fCloseMain = dl::Field("TfrmMain", "CloseMainScreen");
    // IdentifyClass: lea eax, [ebx+IdentifiedAsClass]; mov edx, [ebx+0Ch]; call System._UStrAsg
    if ((hit = Find(dl::Method("TFightingShip", "IdentifyClass"), 0x100,
                    {0x8D, 0x83, -1, -1, 0x00, 0x00, 0x8B, 0x53, 0x0C, 0xE8})))
        g_ustrAsg = CallTarget(hit + 9);
    g_fIdType = dl::Field("TFightingShip", "IdentifiedAsType");
    for (auto& f : g_guard) f.off = dl::Field("TFightingShip", f.name);
    g_fIdClass = dl::Field("TFightingShip", "IdentifiedAsClass");
    g_fHidden = dl::Field("TFightingShip", "Hidden");
    if (!DualOk()) E.missing += "(identification swap) ";
    E.fCbSpeed = dl::Field("TfrmMain", "cbGameSpeed");
    E.fTvShipsTree = E.fTvShips;
    // cbGameSpeedChange: mov eax, [ebx+cbGameSpeed]; mov edx, [eax]; call [edx+GetItemIndex]
    if ((hit = Find(speed, 0x40, {0x8B, 0x83, -1, -1, -1, -1, 0x8B, 0x10, 0xFF, 0x92, -1, -1, -1, -1})))
        g_speedGetIdx = *(int*)(hit + 10);
    E.sessionOk = E.ok && E.inProgVar && E.buildCampVar && E.appVar && E.vmtMain && E.endNow && E.showResults &&
                  E.mCSDD && E.mHide && E.fCbSpeed >= 0 && E.allShipsVar && E.fCShipRef >= 0;
    if (!E.sessionOk) E.missing += "(live session pieces) ";
    Log("lockstep resolve: %s session=%d rand=%p msgDlg=%p frmMain=%p divs=%p forces=%p ships=%p air=%p torps=%p side=%p "
        "clock=%p minute=%p prefs=%p startRun=%p startMinute=%p eotp=%p saveScn=%p endBattle=%p endNow=%p results=%p",
        E.ok ? "ok" : ("MISSING " + E.missing).c_str(), E.sessionOk ? 1 : 0, E.randCell, E.msgDlg, E.frmMainVar,
        E.divisionsVar, E.forcesVar, E.allShipsVar, E.airVar, E.torpVar, E.playerSideVar, E.clockVar, E.minuteVar,
        E.prefs, E.startRun, E.startMinute, E.eotp, E.saveScenario, E.endBattle, E.endNow, E.showResults);
    return E.ok;
}

std::string Missing() { return E.missing; }

static void* Frm() { return E.frmMainVar ? *E.frmMainVar : nullptr; }
static void* List(void** var) { return var ? *var : nullptr; }

void* BattleForm() {
    void* f = Frm();
    return f && dl::IsInstanceOf(f, "TfrmMain") ? f : nullptr;
}

bool BattleVisible() {
    void* f = BattleForm();
    return f && game::FormVisible(f);
}

int Minute() { return E.minuteVar ? *E.minuteVar : -1; }

std::string ClockText() {
    if (!E.clockVar) return "?";
    double t = *E.clockVar;
    int mins = (int)std::lround((t - std::floor(t)) * 1440.0);
    char b[32];
    snprintf(b, sizeof(b), "%02d:%02d", mins / 60 % 24, mins % 60);
    return b;
}

static bool TimerOn(void* form, int field) {
    void* t = form && field >= 0 && E.timerEnabled >= 0 ? dl::At<void*>(form, field) : nullptr;
    return t && dl::At<uint8_t>(t, E.timerEnabled) != 0;
}

// ---------------------------------------------------------------------------------------------
// The state hash (positions, damage, ammo, sighting, orders, air, torpedoes; identification is display state)

struct FieldSpec {
    const char* cls;
    const char* name;
    int size;  // bytes hashed (TPointLL: 8, its third dword is uninitialised in empty points); -1 string; -2 object
    int group;
    int off;
};
enum { G_POS, G_DMG, G_AMMO, G_SIGHT, G_IDENT, G_ORD, G_AIR, G_TORP, G_COUNT };
static const char* kGroupNames[G_COUNT] = {"pos", "dmg", "ammo", "sight", "ident", "ord", "air", "torp"};

static FieldSpec g_shipFields[] = {
    {"TFightingShip", "ShipLocation", 8, G_POS}, {"TFightingShip", "Course", 4, G_POS},
    {"TFightingShip", "CourseGU", 4, G_POS}, {"TFightingShip", "CourseHP", 4, G_POS},
    {"TFightingShip", "OrderedCourse", 4, G_POS}, {"TFightingShip", "OrderedSpeed", 4, G_POS},
    {"TFightingShip", "CurrentSpeed", 4, G_POS}, {"TFightingShip", "Evading", 1, G_POS},
    {"TFightingShip", "Turned", 4, G_POS}, {"TFightingShip", "DistToTurn", 4, G_POS},
    {"TFightingShip", "Sunk", 1, G_DMG}, {"TFightingShip", "Sinking", 4, G_DMG},
    {"TFightingShip", "CurrentFloatPoints", 4, G_DMG}, {"TFightingShip", "CurrentSuperstructurePoints", 4, G_DMG},
    {"TFightingShip", "Flooding", 4, G_DMG}, {"TFightingShip", "PermanentFlooding", 4, G_DMG},
    {"TFightingShip", "FireLevel", 4, G_DMG}, {"TFightingShip", "EnginePercentage", 4, G_DMG},
    {"TFightingShip", "RudderStatus", 4, G_DMG}, {"TFightingShip", "LightHits", 4, G_DMG},
    {"TFightingShip", "MediumHits", 4, G_DMG}, {"TFightingShip", "HeavyHits", 4, G_DMG},
    {"TFightingShip", "TorpedoHits", 4, G_DMG}, {"TFightingShip", "BombHits", 4, G_DMG},
    {"TFightingShip", "MissileHits", 4, G_DMG}, {"TFightingShip", "Survivors", 4, G_DMG},
    {"TFightingShip", "CurrentFCPositions", 4, G_DMG}, {"TFightingShip", "EngineDisabled", 1, G_DMG},
    {"TFightingShip", "CurrentSecondariesPort", 4, G_DMG}, {"TFightingShip", "CurrentSecondariesStarboard", 4, G_DMG},
    {"TFightingShip", "Ammo", 36, G_AMMO}, {"TFightingShip", "RoundsFired", 12, G_AMMO},
    {"TFightingShip", "HitsScored", 12, G_AMMO}, {"TFightingShip", "TorpedoHitsScored", 4, G_AMMO},
    {"TFightingShip", "SmokeFloats", 4, G_AMMO}, {"TFightingShip", "MakingSmoke", 1, G_AMMO},
    {"TFightingShip", "LayingSmoke", 4, G_AMMO},
    {"TFightingShip", "SightingLevel", 4, G_SIGHT}, {"TFightingShip", "RadarSightingLevel", 4, G_SIGHT},
    {"TFightingShip", "LastSighted", 4, G_SIGHT}, {"TFightingShip", "SeenByHumanControlledShips", 1, G_SIGHT},
    {"TFightingShip", "IdentifiedAsType", 1, G_IDENT}, {"TFightingShip", "IdentifiedAsClass", -1, G_IDENT},
    {"TFightingShip", "TargetMain", -2, G_ORD}, {"TFightingShip", "ManuallySelectedTarget", 1, G_ORD},
};
static FieldSpec g_divFields[] = {
    {"TDivision", "DivisionCourse", 4, G_ORD}, {"TDivision", "DivisionSpeed", 4, G_ORD},
    {"TDivision", "Side", 4, G_ORD}, {"TDivision", "Formation", 1, G_ORD}, {"TDivision", "AIControlled", 1, G_ORD},
    {"TDivision", "TacticalStance", 1, G_ORD}, {"TDivision", "TurnTogether", 1, G_ORD},
    {"TDivision", "TargetDiv", -2, G_ORD}, {"TDivision", "DivisionObjective", 8, G_ORD},
    {"TDivision", "WithdrawMe", 1, G_ORD}, {"TDivision", "InCombat", 1, G_ORD},
};
static FieldSpec g_airFields[] = {
    {"TAirFormation", "AFId", 4, G_AIR}, {"TAirFormation", "AircraftNumber", 4, G_AIR},
    {"TAirFormation", "Location", 8, G_AIR}, {"TAirFormation", "Course", 4, G_AIR},
    {"TAirFormation", "Status", 1, G_AIR}, {"TAirFormation", "RemainingFuel", 4, G_AIR},
    {"TAirFormation", "Altitude", 4, G_AIR}, {"TAirFormation", "AircraftDestroyed", 4, G_AIR},
};
static FieldSpec g_torpFields[] = {
    {"TTorpedo", "TorpedoLocation", 8, G_TORP}, {"TTorpedo", "CourseGU", 4, G_TORP},
    {"TTorpedo", "RemainingRangeGU", 4, G_TORP}, {"TTorpedo", "Side", 4, G_TORP},
};
static bool g_specsResolved = false;

static void ResolveSpecs() {
    if (g_specsResolved) return;
    g_specsResolved = true;
    for (auto& f : g_shipFields) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_divFields) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_airFields) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_torpFields) f.off = dl::Field(f.cls, f.name);
}

static void Fnv(uint64_t& h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
}
static void FnvStr(uint64_t& h, const std::wstring& s) {
    Fnv(h, s.data(), s.size() * 2);
    Fnv(h, "\0", 1);
}

static std::wstring ObjName(void* obj) {
    if (!obj) return L"-";
    if (dl::IsInstanceOf(obj, "TDivision")) return dl::ReadUStr(dl::At<void*>(obj, E.fDivName));
    if (dl::IsInstanceOf(obj, "Tship")) return dl::ReadUStr(dl::At<void*>(obj, E.fShipName));
    if (dl::IsInstanceOf(obj, "TForce")) return dl::ReadUStr(dl::At<void*>(obj, E.fForceName));
    return U2W(dl::ClassNameOf(obj));
}

static void HashFields(uint64_t* g, void* obj, const FieldSpec* fs, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const FieldSpec& f = fs[i];
        if (f.off < 0) continue;
        uint8_t* p = (uint8_t*)obj + f.off;
        if (f.size == -1)
            FnvStr(g[f.group], dl::ReadUStr(*(void**)p));
        else if (f.size == -2)
            FnvStr(g[f.group], ObjName(*(void**)p));
        else
            Fnv(g[f.group], p, f.size);
    }
}

struct HashResult {
    uint64_t all = 0, group[G_COUNT] = {};
    int ships = 0, sunk = 0, divisions = 0, air = 0, torps = 0, sides[2] = {};
};

static HashResult StateHash() {
    ResolveSpecs();
    HashResult r;
    for (auto& g : r.group) g = 1469598103934665603ull;
    void* divs = List(E.divisionsVar);
    int sunkOff = g_shipFields[10].off;
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* dv = dl::ListItem(divs, i);
        if (!dv) continue;
        r.divisions++;
        FnvStr(r.group[G_ORD], ObjName(dv));
        HashFields(r.group, dv, g_divFields, sizeof(g_divFields) / sizeof(g_divFields[0]));
        void* ships = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(ships); j++) {
            void* s = dl::ListItem(ships, j);
            if (!s) continue;
            r.ships++;
            if (sunkOff >= 0 && dl::At<uint8_t>(s, sunkOff)) r.sunk++;
            int side = dl::At<int>(s, E.fShipSide);
            if (side == 0 || side == 1) r.sides[side]++;
            FnvStr(r.group[G_POS], ObjName(s));
            HashFields(r.group, s, g_shipFields, sizeof(g_shipFields) / sizeof(g_shipFields[0]));
        }
    }
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* a = dl::ListItem(air, i)) {
            r.air++;
            HashFields(r.group, a, g_airFields, sizeof(g_airFields) / sizeof(g_airFields[0]));
        }
    void* torps = List(E.torpVar);
    for (int i = 0; i < dl::ListCount(torps); i++)
        if (void* t = dl::ListItem(torps, i)) {
            r.torps++;
            HashFields(r.group, t, g_torpFields, sizeof(g_torpFields) / sizeof(g_torpFields[0]));
        }
    r.all = 1469598103934665603ull;
    for (int g = 0; g < G_COUNT; g++)
        if (g != G_IDENT) Fnv(r.all, &r.group[g], 8);
    if (E.clockVar) Fnv(r.all, E.clockVar, 8);
    return r;
}

uint64_t StateHashValue() { return StateHash().all; }

// ---------------------------------------------------------------------------------------------
// The RNG. Delphi's own generator (RandSeed * 0x08088405 + 1) on a battle-local state. Draws are classified by context:
// inside the simulation (sim), inside display routines (ui, nested anywhere), or neither (other). Display draws come
// from a separate stream in mode 2; "other" draws are shared only while a snapshot loads (both PCs load the same way).

static int g_rngMode = 0;  // 0 the game's own, 1 one seeded stream, 2 seeded stream + display draws on their own stream
static uint32_t g_simState = 0, g_uiState = 0, g_origRand = 0;
static bool g_sharedOther = true;  // "other" draws use the shared stream (POC: always; lockstep: only while loading)
static uint64_t g_draws[3], g_drawsAt[3];
static std::map<uint32_t, uint32_t> g_callers[3];
static int g_sim = 0, g_ui = 0;  // context depths
static bool g_trace = false;     // tests: record the call sites of the shared draws of each minute
static std::vector<uint32_t> g_traceSeq;

extern "C" uint32_t __cdecl LS_Random32() {
    uint32_t* frame = (uint32_t*)_AddressOfReturnAddress();  // [0] into Random, [1] its pushed range, [2] its caller
    int kind = g_ui > 0 ? 1 : g_sim > 0 ? 0 : 2;
    g_draws[kind]++;
    if (kind != 0 && g_callers[kind].size() < 4096) g_callers[kind][frame[2]]++;
    bool shared = kind == 0 || (kind == 2 && g_sharedOther) || g_rngMode == 1;
    if (!shared) return g_uiState = g_uiState * 0x08088405u + 1;
    if (g_trace && g_traceSeq.size() < 200000) g_traceSeq.push_back(frame[2] - (uint32_t)dl::g_base);
    return g_simState = g_simState * 0x08088405u + 1;
}

void SetRng(int mode, uint32_t seed) {
    if (!Resolve()) return;
    if (mode == 0) {
        if (g_origRand && *E.randCell == U(&LS_Random32)) InterlockedExchange((volatile LONG*)E.randCell, (LONG)g_origRand);
        g_rngMode = 0;
        return;
    }
    g_simState = seed;
    g_uiState = GetTickCount() ^ (GetCurrentProcessId() << 8);  // deliberately not reproducible
    memset(g_draws, 0, sizeof(g_draws));
    memset(g_drawsAt, 0, sizeof(g_drawsAt));
    for (auto& m : g_callers) m.clear();
    g_rngMode = mode;
    uint32_t cur = *E.randCell;
    if (cur != U(&LS_Random32)) {
        g_origRand = cur;
        InterlockedExchange((volatile LONG*)E.randCell, (LONG)U(&LS_Random32));
    }
}

std::string RngStats() {
    std::string out = "ok mode=" + std::to_string(g_rngMode) + " draws=" + std::to_string(g_draws[0]) + "/" +
                      std::to_string(g_draws[1]) + "/" + std::to_string(g_draws[2]);
    for (int k = 1; k < 3; k++) {
        std::vector<std::pair<uint32_t, uint32_t>> v(g_callers[k].begin(), g_callers[k].end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        out += k == 1 ? " ui:" : " other:";
        for (size_t i = 0; i < v.size() && i < 12; i++) {
            char b[40];
            snprintf(b, sizeof(b), " %x*%u", (unsigned)(v[i].first - dl::g_base), v[i].second);
            out += b;
        }
    }
    return out;
}

std::string HashLine(const char* tag) {
    HashResult h = StateHash();
    char b[760];
    int n = snprintf(b, sizeof(b), "%s t=%s min=%d all=%016llx", tag, ClockText().c_str(), Minute(),
                     (unsigned long long)h.all);
    for (int g = 0; g < G_COUNT; g++)
        n += snprintf(b + n, sizeof(b) - n, " %s=%08x", kGroupNames[g], (unsigned)(h.group[g] ^ (h.group[g] >> 32)));
    void* m = Frm();
    snprintf(b + n, sizeof(b) - n,
             " ships=%d(%d/%d) sunk=%d divs=%d af=%d torps=%d draws sim=%llu ui=%llu other=%llu rng=%08x speed=%d",
             h.ships, h.sides[0], h.sides[1], h.sunk, h.divisions, h.air, h.torps,
             (unsigned long long)(g_draws[0] - g_drawsAt[0]), (unsigned long long)(g_draws[1] - g_drawsAt[1]),
             (unsigned long long)(g_draws[2] - g_drawsAt[2]), g_simState, m ? dl::At<int>(m, E.fGameSpeed) : -99);
    for (int k = 0; k < 3; k++) g_drawsAt[k] = g_draws[k];
    return b;
}

// ---------------------------------------------------------------------------------------------
// Contexts. Hooked routines run through a thunk that calls LS_CtxEnter/LS_CtxExit around the original. The thunk copies
// the routine's stack arguments below its own return address and returns to the caller itself (`ret N`), so the
// caller's return address never leaves the stack. Each entered routine is a frame (the address of the caller's return
// slot, the kind); a frame whose slot lies below the current stack pointer was unwound by an exception and is dropped.
// The mod's own shared sections (SharedBegin/End) are frames too.

enum { K_SIM = 0, K_UI = 1, K_RES = 2 };
struct CtxHook {
    const char* cls;   // nullptr: an unnamed routine (target from the resolver)
    const char* name;
    int kind;
    int pop;           // bytes of stack arguments (Delphi register convention: the 4th and later parameters)
    void* target;
    void* orig;
    void* thunk;
    bool on;
};
// Simulation contexts the player can also reach from the battle screen (DetachShip, CheckSupportingDDToDetach) are
// "actions": run from the screen they are recorded and replayed on the other PC (see OnActionEnter).
enum { H_STARTMINUTE, H_MOVETORPS, H_MOVESHIPS, H_EOTP, H_DETACH, H_CSDD, H_RESULTS, H_FIRST_UI };
static CtxHook g_ctx[] = {
    {nullptr, "StartMinute", K_SIM, 0}, {nullptr, "MoveTorpedoes", K_SIM, 0}, {nullptr, "MoveAllShips", K_SIM, 0},
    {nullptr, "EndOfTurnProcedures", K_SIM, 0}, {"TfrmMain", "DetachShip", K_SIM, 0},
    {"TfrmMain", "CheckSupportingDDToDetach", K_SIM, 0}, {nullptr, "ShowResultScreen", K_RES, 0},
    {"TfrmMain", "GlobeRender", K_UI, 0},                  // (Sender)
    {"TfrmMain", "Globe2Render", K_UI, 0},                 // (Sender)
    {"TfrmMain", "GlobeMouseMove", K_UI, 8},               // (Sender, Shift; X, Y)
    {"TfrmMain", "FillShipTree", K_UI, 8},                 // (aTreeView, aSide; ShowShipData, aDivToSelect)
    {"TfrmMain", "FillReportList", K_UI, 0},               // ()
    {"TfrmMain", "UpdateTreeText", K_UI, 0},               // (aDivision)
    {"TfrmMain", "PlaySoundEffect", K_UI, 8},              // (aFilename, Versions; Background, OverEverything)
    {"TfrmMain", "PlayWave", K_UI, 0},                     // (FileName, Modus)
    {"TfrmMain", "tvShipsChange", K_UI, 0},                // (Sender, Node)
    {"TFightingShip", "GetTypeAndNameForEnemy", K_UI, 0},  // (var Result)
};
static const int kCtxHooks = sizeof(g_ctx) / sizeof(g_ctx[0]);
static const int kManual = -1;  // a frame of the mod's own shared section

struct Frame {
    uint32_t slot;  // the address of the caller's return address (shared sections: the owner's stack position)
    int idx;        // hook index, or kManual
    int kind;
};
static Frame g_frames[512];
static int g_nFrames = 0;
static bool g_leakCheck = false;  // tests (`ls leakcheck 1`): the simulation state must not change between its steps
static std::vector<int> g_uiSince;  // display routines entered since the last simulation step (leak check)
static int g_outerIdx = -2;          // the frame that entered the simulation (leak check)
static bool g_testThrow = false;  // tests: an exception inside this PC's next MoveAllShips (`ls throw`)
static bool g_testStall = false;  // tests: this PC's next released minute doesn't start (`ls stall`)
static int g_viewSide = -1;  // the side this PC shows outside the simulation (-1: never touch the display state)
static int g_results = 0;    // inside the game's result screen (its dialogs are the player's own)
static bool g_forceCanonical = false;  // the simulation's set stays in the ships (a snapshot is being saved)
static int g_manual = 0;               // open shared sections

// Session-level reactions (defined below).
static void ApplyViewSide();
static void OnSimOuterEnter();
static void OnSimOuterExit();
static void OnActionEnter(int idx, void* a, void* b);
static void OnActionExit(int idx, void* result);
static void OnSimFrameUnwound(int idx);

static void LeakCheckEnter(int idx);
static int g_outerIdxFwd();
static void OnResultsExit();

static void FrameEnterCount(int kind, int dir) {
    if (kind == K_RES) {
        if (dir > 0)
            g_results++;
        else if (g_results > 0)
            g_results--;
        ApplyViewSide();
        return;
    }
    if (kind == K_SIM) {
        if (dir > 0) {
            if (g_sim++ == 0) {
                OnSimOuterEnter();
                ApplyViewSide();  // the simulation's set is in place for the leak check
                LeakCheckEnter(g_outerIdxFwd());
            }
        } else if (g_sim > 0 && --g_sim == 0) {
            OnSimOuterExit();
        }
    } else {
        if (dir > 0)
            g_ui++;
        else if (g_ui > 0)
            g_ui--;
    }
    ApplyViewSide();
}

static const char* FrameName(const Frame& f) { return f.idx == kManual ? "shared section" : g_ctx[f.idx].name; }

// Drops the top frame because an exception unwound it.
static void DropUnwound(const char* where) {
    Frame f = g_frames[--g_nFrames];
    Log("lockstep: context frame %s dropped (unwound, %s)", FrameName(f), where);
    if (f.idx == kManual && g_manual > 0) g_manual--;
    if (f.kind == K_SIM) OnSimFrameUnwound(f.idx);
    FrameEnterCount(f.kind, -1);
}

// Frames at or below `sp` (deeper in the stack) can't be active any more.
static void PruneBelow(uint32_t sp, bool inclusive, const char* where) {
    while (g_nFrames > 0 && (g_frames[g_nFrames - 1].slot < sp || (inclusive && g_frames[g_nFrames - 1].slot == sp)))
        DropUnwound(where);
}

struct Regs {  // pushad layout
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
};

// Tests (`ls trace 1`): markers in the per-minute draw trace. The state hash (every group, simulation set) at the entry
// and exit of every outer simulation frame (0x8/0xC + frame index + 24 bits), the game speed and intervals (0xF), the
// display routines entered (0xE, not compared).
static void TraceStateMark(int idx, bool exit) {
    void* m = E.frmMainVar ? *E.frmMainVar : nullptr;
    if (!m || g_traceSeq.size() >= 200000) return;
    HashResult h = StateHash();
    uint64_t x = h.all;
    Fnv(x, &h.group[G_IDENT], 8);
    uint32_t f = (uint32_t)(x ^ (x >> 24) ^ (x >> 48)) & 0xffffff;
    g_traceSeq.push_back(0x80000000u | (exit ? 0x40000000u : 0) | ((uint32_t)(idx & 0x3f) << 24) | f);
    uint32_t sp = (uint32_t)(dl::At<int>(m, E.fGameSpeed) + 8) & 0xff;
    uint32_t iv = E.fIntervals >= 0 ? (uint32_t)dl::At<int>(m, E.fIntervals) & 0xffff : 0xffff;
    g_traceSeq.push_back(0xF0000000u | (sp << 16) | iv);
}

extern "C" void __stdcall LS_CtxEnter(int idx, Regs* r, uint32_t slot) {
    __try {
        PruneBelow(slot, true, g_ctx[idx].name);
        if (g_nFrames < 512) g_frames[g_nFrames++] = {slot, idx, g_ctx[idx].kind};
        if ((idx == H_DETACH || idx == H_CSDD) && g_sim == 0)
            OnActionEnter(idx, (void*)(uintptr_t)r->edx, (void*)(uintptr_t)r->ecx);
        if (g_sim == 0 && g_ctx[idx].kind == K_SIM) g_outerIdx = idx;
        FrameEnterCount(g_ctx[idx].kind, +1);
        if (g_trace) {
            if (g_ctx[idx].kind == K_SIM && g_sim == 1)
                TraceStateMark(idx, false);
            else if (g_ctx[idx].kind != K_SIM && g_traceSeq.size() < 200000)
                g_traceSeq.push_back(0xE0000000u | ((uint32_t)idx << 16) | (uint32_t)(g_sim & 0xffff));
        }
        if (g_leakCheck && g_sim == 0 && g_ctx[idx].kind != K_SIM && g_uiSince.size() < 64) g_uiSince.push_back(idx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (g_testThrow && idx == H_MOVESHIPS) {  // unwinds MoveAllShips to the game's own handler (its frame stays open)
        g_testThrow = false;
        Log("lockstep: TEST exception in MoveAllShips");
        RaiseException(0xE0545354, 0, 0, nullptr);
    }
}

extern "C" void __stdcall LS_CtxExit(int idx, Regs* r, uint32_t slot) {
    __try {
        // The returning frame has exactly this slot (the thunk returns from the same stack position it entered at);
        // frames above it were unwound by an exception.
        bool found = false;
        for (int i = g_nFrames - 1; i >= 0; i--)
            if (g_frames[i].idx == idx && g_frames[i].slot == slot) {
                found = true;
                break;
            }
        if (found) {
            while (g_nFrames > 0 && !(g_frames[g_nFrames - 1].idx == idx && g_frames[g_nFrames - 1].slot == slot))
                DropUnwound(g_ctx[idx].name);
            if (g_trace && g_ctx[idx].kind == K_SIM && g_sim == 1) TraceStateMark(idx, true);
            g_nFrames--;
            FrameEnterCount(g_ctx[idx].kind, -1);
        } else {
            Log("lockstep: context frame of %s not found at its exit", g_ctx[idx].name);
        }
        if ((idx == H_DETACH || idx == H_CSDD) && g_sim == 0) OnActionExit(idx, (void*)(uintptr_t)r->eax);
        if (idx == H_RESULTS) OnResultsExit();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void* EmitCtxThunk(int idx) {
    uint8_t* m = (uint8_t*)VirtualAlloc(nullptr, 160, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!m) return nullptr;
    uint8_t* p = m;
    auto b = [&](std::initializer_list<uint8_t> l) {
        for (uint8_t x : l) *p++ = x;
    };
    auto d = [&](uint32_t v) {
        *(uint32_t*)p = v;
        p += 4;
    };
    int pop = g_ctx[idx].pop;
    // enter
    b({0x60});                    // pushad
    b({0x89, 0xE0});              // mov eax, esp            (regs)
    b({0x8D, 0x4C, 0x24, 0x20});  // lea ecx, [esp+32]       (the caller's return slot)
    b({0x51, 0x50, 0x68});        // push ecx; push eax; push idx
    d((uint32_t)idx);
    b({0xB8});                    // mov eax, LS_CtxEnter
    d(U((void*)&LS_CtxEnter));
    b({0xFF, 0xD0});              // call eax
    b({0x61});                    // popad
    for (int i = 0; i < pop / 4; i++)
        b({0xFF, 0x74, 0x24, (uint8_t)pop});  // push dword ptr [esp+pop]   (the stack arguments, last one first)
    b({0xFF, 0x15});              // call [orig]             (it pops its copy of the arguments)
    d(U(&g_ctx[idx].orig));
    // exit: the stack is where it was at the entry
    b({0x60});                    // pushad
    b({0x89, 0xE0});              // mov eax, esp
    b({0x8D, 0x4C, 0x24, 0x20});  // lea ecx, [esp+32]
    b({0x51, 0x50, 0x68});        // push ecx; push eax; push idx
    d((uint32_t)idx);
    b({0xB8});                    // mov eax, LS_CtxExit
    d(U((void*)&LS_CtxExit));
    b({0xFF, 0xD0});              // call eax
    b({0x61});                    // popad
    if (pop)
        b({0xC2, (uint8_t)pop, 0x00});  // ret pop
    else
        b({0xC3});                      // ret
    FlushInstructionCache(GetCurrentProcess(), m, p - m);
    return m;
}

// Manual simulation sections: the mod's own steps that must run like simulation on both PCs (shared draws, canonical
// side): applying a release, replaying a detach, the agreed end, setting up a load. A frame at the owner's stack
// position, so an exception that unwinds the owner also ends the section.
static __declspec(noinline) void SharedBegin() {
    uint32_t slot = (uint32_t)(uintptr_t)_AddressOfReturnAddress() + 4;
    PruneBelow(slot, true, "shared section");
    if (g_nFrames < 512) g_frames[g_nFrames++] = {slot, kManual, K_SIM};
    g_manual++;
    if (g_sim == 0) g_outerIdx = kManual;
    FrameEnterCount(K_SIM, +1);
    if (g_trace && g_sim == 1) TraceStateMark(0x3f, false);
}
static void SharedEnd() {
    while (g_nFrames > 0 && g_frames[g_nFrames - 1].idx != kManual) DropUnwound("shared section end");
    if (g_trace && g_sim == 1) TraceStateMark(0x3f, true);
    if (g_nFrames > 0) {
        g_nFrames--;
        FrameEnterCount(K_SIM, -1);
    }
    if (g_manual > 0) g_manual--;
}

// ---------------------------------------------------------------------------------------------
// Special detours

extern "C" {
void* g_lsOrigStartRun = nullptr;
void* g_lsOrigMsgDlg = nullptr;
void* g_lsOrigShowPic = nullptr;
void* g_lsOrigExecGeneral = nullptr;
void* g_lsThunkStartMinute = nullptr;
void* g_lsThunkEOTP = nullptr;
int __stdcall LS_StartMinuteAllowed(void* self);
void __stdcall LS_AfterEOTP(void* self);
int __stdcall LS_StartRun(void* self, int n);
int __stdcall LS_MsgDlg(const wchar_t* msg, int type, int buttons);
int __stdcall LS_ShowPic(const wchar_t* text, const wchar_t* pic);
int __stdcall LS_ExecGeneral(const wchar_t* prompt, int preselect);
int __stdcall LS_Block(int idx, void* self);
}

extern "C" __declspec(naked) void LS_dStartMinute() {  // StartMinute(Self)
    __asm {
        push eax
        push eax
        call LS_StartMinuteAllowed
        test eax, eax
        pop eax
        jz skip
        jmp dword ptr [g_lsThunkStartMinute]
    skip:
        ret
    }
}

extern "C" __declspec(naked) void LS_dEOTP() {  // EndOfTurnProcedures(Self)
    __asm {
        push eax
        call dword ptr [g_lsThunkEOTP]
        call LS_AfterEOTP
        ret
    }
}

extern "C" __declspec(naked) void LS_dStartRun() {  // StartRun(Self, Minutes)
    __asm {
        push eax
        push ecx
        push edx
        push edx
        push eax
        call LS_StartRun
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz skip
        jmp dword ptr [g_lsOrigStartRun]
    skip:
        ret
    }
}

// MessageDlgPosHelp(Msg=EAX, DlgType=DL, Buttons=CX; HelpCtx, X, Y, HelpFileName): Integer
extern "C" __declspec(naked) void LS_dMsgDlg() {
    __asm {
        push eax
        push ecx
        push edx
        movzx ecx, cx
        movzx edx, dl
        push ecx
        push edx
        push eax
        call LS_MsgDlg
        cmp eax, -1
        je show
        add esp, 12
        ret 16
    show:
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [g_lsOrigMsgDlg]
    }
}

// TdlgPictureMessage.ShowPicMessage(Self, MessageText=EDX, PictureFileName=ECX; CopyMessageToLog)
extern "C" __declspec(naked) void LS_dShowPic() {
    __asm {
        push eax
        push ecx
        push edx
        push ecx
        push edx
        call LS_ShowPic
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz skip
        jmp dword ptr [g_lsOrigShowPic]
    skip:
        ret 4
    }
}

// TdlgEventAnswer.ExecuteGeneral(Self, aPrompt=EDX, answer1=ECX; answer2, answer3, aPreselect): Integer
extern "C" __declspec(naked) void LS_dExecGeneral() {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 16]  // aPreselect
        push edx
        call LS_ExecGeneral
        cmp eax, -1
        je show
        add esp, 12
        ret 12
    show:
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [g_lsOrigExecGeneral]
    }
}

// Click handlers (Self, Sender) the lockstep blocks or turns into requests.
struct Blocker {
    const char* cls;
    const char* name;
    int policy;  // see LS_Block
    void* orig;
};
enum { B_PAUSE, B_END, B_AUTORUN, B_DETACH, B_UNAVAILABLE, B_SAVE, B_SPEED };
static Blocker g_blockers[] = {
    {"TfrmMain", "sbPauseClick", B_PAUSE},
    {"TfrmMain", "sbEndGameClick", B_END},
    {"TfrmMain", "sbAutoRunBattleClick", B_AUTORUN},
    {"TfrmMain", "Detach1Click", B_DETACH},
    {"TdlgDivisionReport", "Detach1Click", B_DETACH},
    {"TfrmMain", "sbSaveClick", B_SAVE},
    {"TfrmMain", "cbGameSpeedChange", B_SPEED},
    {"TfrmMain", "cbFastestAvailableClick", B_UNAVAILABLE},
    {"TfrmMain", "sbFastestAvailableClick", B_UNAVAILABLE},
    {"TfrmMain", "sbStrikeClick", B_UNAVAILABLE},
    {"TfrmMain", "LaunchStrike1Click", B_UNAVAILABLE},
    {"TfrmMain", "Launchstrike2Click", B_UNAVAILABLE},
    {"TfrmMain", "Launchaircraft1Click", B_UNAVAILABLE},
    {"TfrmMain", "Bombship1Click", B_UNAVAILABLE},
    {"TfrmMain", "TryforHome1Click", B_UNAVAILABLE},
    {"TfrmMain", "bnFireClick", B_UNAVAILABLE},
    {"TfrmMain", "sbFireSalvoClick", B_UNAVAILABLE},
    {"TfrmMain", "sbRoutineAirOpsClick", B_UNAVAILABLE},
    {"TfrmMain", "Searchpattern1Click", B_UNAVAILABLE},
    {"TfrmMain", "ShowAircraft1Click", B_UNAVAILABLE},
    {"TfrmMain", "Changeforce2Click", B_UNAVAILABLE},
    {"TfrmMain", "Sink1Click", B_UNAVAILABLE},
    {"TfrmMain", "Damage1Click", B_UNAVAILABLE},
    {"TfrmMain", "Teleport1Click", B_UNAVAILABLE},
    {"TfrmMain", "CreateMTB1Click", B_UNAVAILABLE},
    {"TfrmMain", "Misunderstand1Click", B_UNAVAILABLE},
    {"TfrmMain", "Blowup1Click", B_UNAVAILABLE},
    {"TfrmMain", "IncWeather1Click", B_UNAVAILABLE},
    {"TfrmMain", "Setradarclass1Click", B_UNAVAILABLE},
    {"TfrmMain", "SetRadarSightingLevel1Click", B_UNAVAILABLE},
    {"TfrmMain", "bnTestClick", B_UNAVAILABLE},
    {"TfrmMain", "Test1Click", B_UNAVAILABLE},
    {"TdlgDivisionReport", "bnFireMissilesClick", B_UNAVAILABLE},
    {"TdlgDivisionReport", "bnTorpsClick", B_UNAVAILABLE},
    {"TdlgDivisionReport", "sbLandCAPClick", B_UNAVAILABLE},
    {"TdlgForceOrders", "sbChangeLeadClick", B_UNAVAILABLE},
    {"TdlgForceOrders", "sbTurnAwayClick", B_UNAVAILABLE},
    {"TdlgTorpTargets", "bnFireClick", B_UNAVAILABLE},
};
static const int kBlockers = sizeof(g_blockers) / sizeof(g_blockers[0]);

static void* EmitBlockThunk(int idx) {
    uint8_t* m = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!m) return nullptr;
    uint8_t* p = m;
    auto b = [&](std::initializer_list<uint8_t> l) {
        for (uint8_t x : l) *p++ = x;
    };
    auto d = [&](uint32_t v) {
        *(uint32_t*)p = v;
        p += 4;
    };
    b({0x50, 0x51, 0x52});  // push eax; push ecx; push edx
    b({0x50, 0x68});        // push eax (Self); push idx
    d((uint32_t)idx);
    b({0xB8});              // mov eax, LS_Block
    d(U((void*)&LS_Block));
    b({0xFF, 0xD0});        // call eax
    b({0x85, 0xC0});        // test eax, eax
    b({0x5A, 0x59, 0x58});  // pop edx; pop ecx; pop eax
    b({0x75, 0x06});        // jnz skip
    b({0xFF, 0x25});        // jmp [orig]
    d(U(&g_blockers[idx].orig));
    b({0xC3});              // skip: ret
    FlushInstructionCache(GetCurrentProcess(), m, p - m);
    return m;
}

static bool g_hooksTried = false, g_hooksOk = false;

static bool HookOne(void* target, void* detour, void** orig, const char* name) {
    if (!target || !detour) {
        Log("lockstep hook %s: no target", name);
        return false;
    }
    MH_STATUS st = MH_CreateHook(target, detour, orig);
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        Log("lockstep hook %s failed (%d)", name, (int)st);
        return false;
    }
    return true;
}

bool EnsureHooks() {
    if (g_hooksTried) return g_hooksOk;
    if (!Resolve()) return false;
    g_hooksTried = true;
    bool ok = true;
    g_ctx[H_STARTMINUTE].target = E.startMinute;
    g_ctx[H_MOVETORPS].target = E.moveTorps;
    g_ctx[H_MOVESHIPS].target = E.moveShips;
    g_ctx[H_EOTP].target = E.eotp;
    g_ctx[H_RESULTS].target = E.showResults;
    int n = 0;
    for (int i = 0; i < kCtxHooks; i++) {
        CtxHook& h = g_ctx[i];
        if (h.cls) h.target = dl::Method(h.cls, h.name);
        h.thunk = h.target ? EmitCtxThunk(i) : nullptr;
        void* detour = i == H_STARTMINUTE ? (void*)&LS_dStartMinute : i == H_EOTP ? (void*)&LS_dEOTP : h.thunk;
        if (i == H_STARTMINUTE) g_lsThunkStartMinute = h.thunk;
        if (i == H_EOTP) g_lsThunkEOTP = h.thunk;
        h.on = h.thunk && HookOne(h.target, detour, &h.orig, h.name);
        if (h.on) n++;
        else if (i < H_FIRST_UI) ok = false;  // the simulation contexts are required
    }
    int nb = 0;
    for (int i = 0; i < kBlockers; i++) {
        Blocker& b = g_blockers[i];
        void* target = dl::Method(b.cls, b.name);
        void* thunk = target ? EmitBlockThunk(i) : nullptr;
        if (thunk && HookOne(target, thunk, &b.orig, b.name)) nb++;
        else if (b.policy != B_UNAVAILABLE) ok = false;
    }
    ok &= HookOne(E.startRun, (void*)&LS_dStartRun, &g_lsOrigStartRun, "StartRun");
    ok &= HookOne(E.msgDlg, (void*)&LS_dMsgDlg, &g_lsOrigMsgDlg, "MessageDlgPosHelp");
    ok &= HookOne(E.mShowPic, (void*)&LS_dShowPic, &g_lsOrigShowPic, "ShowPicMessage");
    ok &= HookOne(E.mExecGeneral, (void*)&LS_dExecGeneral, &g_lsOrigExecGeneral, "ExecuteGeneral");
    g_hooksOk = ok;
    Log("lockstep hooks: %s (%d of %d context hooks, %d of %d click handlers)", ok ? "ok" : "INCOMPLETE", n, kCtxHooks, nb,
        kBlockers);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Session state

enum class St { Idle, Holding, Loading, WaitReady, Running, Ended };
static const char* StName(St s) {
    switch (s) {
        case St::Idle: return "idle";
        case St::Holding: return "holding";
        case St::Loading: return "loading";
        case St::WaitReady: return "waitready";
        case St::Running: return "running";
        case St::Ended: return "ended";
    }
    return "?";
}

struct Batch {
    int seq = 0;
    std::vector<std::string> items;  // "V<obj key>\x1f<field>\x1f<value>" or "A<action>\x1f<arg>..."
};
struct Overlay {
    std::string value;
    int seq = 0;
};

static Hooks g_hooks;

static struct Session {
    bool on = false;  // a lockstep battle is set up on this PC (from Hold/start to the end)
    bool isA = false;
    St st = St::Idle;
    int bid = 0, epoch = 0, slot = -1, opp = -1, mySide = 0;
    std::string peerName, sideNames[2];
    SendFn send;
    uint32_t seed = 0;
    std::string why;  // the hold / end reason
    // clock
    int boundary = 0;            // the game's minute counter at this PC's last boundary
    bool minuteRunning = false;  // between our StartRun and the end of the minute
    int tokens = 0;              // minute starts allowed (one per release)
    bool ownRun = false;         // our own StartRun call
    bool inAction = false;       // inside a release / the agreed end (no re-entry from Tick)
    int runLeft = 0;             // A: minutes still to release
    std::string runWhat = "paused";
    int wantSpeed = INT_MIN;  // A: game speed to apply with the next release
    // the boundary hashes (A compares)
    std::string myHash;
    uint32_t myRng = 0;
    int myHashN = -1;
    std::string peerHash;
    uint32_t peerRng = 0;
    int peerDoneN = -1;
    int verifiedN = -1;
    int okMinutes = 0;  // minutes in sync since the last resync
    // orders
    int mySeq = 0, peerSeq = 0;
    std::vector<Batch> myBatches;      // own batches not released yet
    std::map<int, Batch> peerBatches;  // the other side's batches not released yet
    bool forceResync = false;
    // B: a release that waits for a modal dialog to close
    KV pendingRel;
    bool hasPendingRel = false;
    // end
    bool endWant[2] = {false, false};
    bool proposal = false;  // the game asked at this boundary whether to end the battle
    bool endedNormal = false;
    // robustness
    bool peerAI = false;     // A: side 1 was handed to the AI
    bool dropPending = false;
    std::string dropWhy;
    bool peerBusy = false;
    int resyncs = 0;
    int forceDesync = 0;  // tests: corrupt this PC's state before the next minute
    DWORD lastPeerMsg = 0, lastHbSent = 0, waitSince = 0, loadStart = 0, busySince = 0;
    // B: the start that waits for a clean moment to load
    std::string pendingStart;
    bool loadQueued = false;
    std::string savedPrefs;  // B: this PC's own battle preferences (restored at the end)
    std::string prefsA;      // B: A's battle preferences
    int startSpeed = 1;      // B: the game speed of the snapshot
    bool firstLoad = false;  // B: the first load of this battle (side 1 becomes human)
    bool everRunning = false;  // A: the battle ran in lockstep at least once
    bool peerReady = false;    // A: B's load finished
    std::string peerReadyHash, myReadyHash;
    std::string myName;  // this PC's player name (in A's start message)
    DWORD finSince = 0;  // B: A ended the battle (B ends at the same minute or leaves)
    unsigned cw0 = 0;
    std::string gameDirA;
    bool slotModeSet = false;
    bool leavePending = false;  // B: discard the battle once possible
    std::string leaveWhy;
    // robustness (review fixes)
    bool atBoundary = false;    // OnBoundary processed this boundary; a release may start the next minute
    bool minuteBroken = false;  // a simulation frame of the running minute was unwound by an exception
    DWORD stalledSince = 0;     // the minute runs but both battle timers are off (watchdog)
    int brokenMinutes = 0;      // minutes recovered by the watchdog / unwinding since the last good minute
    bool peerEnded = false;     // A: B reported the end of the battle (its result screen; it stops talking)
    int peerEndN = -1;          // A: the boundary B's battle ended at
    bool battleLost = false;    // A: the battle screen could not be restored after a failed (re)load
    bool endSoon = false;       // A (tests): the next release makes the game end the battle at the end of that minute
} S;

static unsigned g_cwSaved = 0;  // B: this PC's own x87 control word while A's is in use

static bool g_loading = false;  // a battle snapshot is loading (no guard, no display swaps)
static bool g_noView = false;   // tests: no display swap (B shows side 0's view)
static bool g_muted = false;    // tests: this PC drops all lockstep traffic both ways
// Timeouts (ms): a boundary the other PC doesn't reach, no message at all, a load, the heartbeat, a dialog open there.
static DWORD g_tBoundary = 30000, g_tLost = 30000, g_tLoad = 120000, g_tHb = 5000, g_tBusyCap = 300000;
static int g_quiet = 0;         // our own save/load: every message box gets its default answer
static int g_answerYes = 0;     // the agreed end: "Exit battle?" -> Yes
static bool g_human[2] = {true, true};  // the simulation's human control per side
static bool g_viewOn = false;           // the display state (this PC's side) is applied

static void Chat(const std::string& s) {
    Log("lockstep: %s", s.c_str());
    if (g_hooks.chat) g_hooks.chat("* " + s);
}

// ---------------------------------------------------------------------------------------------
// Display state. Inside the simulation the battle's human side (PlayerSide) is 0 and both sides' ships count as human
// controlled (ForceIsHumanControlled), the same on both PCs. Outside it (and in display routines inside it) this PC
// sees its own side: PlayerSide = its side, and only its own ships are "human controlled" (names, reports, outlines).
// The display also writes ship fields the simulation reads: the identification (IdentifiedAsType/IdentifiedAsClass,
// set lazily when an enemy ship is named on screen; IdentifyClass draws a different number of random numbers once a
// class is set) and Hidden (the map's visibility; AddSmokePoints lays smoke only for ships not hidden). Each ship keeps
// two sets: the simulation's (its own identification, Hidden = 0) and this PC's display set, swapped at the same
// moments as PlayerSide. A change the simulation makes is adopted by the display set.


struct Dual {
    bool known = false;
    uint8_t simType = 0, dispType = 0, dispHidden = 0, lastSimType = 0;
    uint32_t simClass = 0, dispClass = 0;  // Delphi strings we hold a reference to
    uint32_t lastSimClass = 0;             // identity only (simClass references it)
    uint32_t simG[4] = {}, dispG[4] = {}, lastSimG[4] = {};  // g_guard
};

static uint32_t GuardRead(void* s, const GuardField& f) {
    return f.size == 1 ? dl::At<uint8_t>(s, f.off) : dl::At<uint32_t>(s, f.off);
}
static void GuardWrite(void* s, const GuardField& f, uint32_t v) {
    if (f.size == 1)
        dl::At<uint8_t>(s, f.off) = (uint8_t)v;
    else
        dl::At<uint32_t>(s, f.off) = v;
}
static std::unordered_map<void*, Dual> g_dual;

static void StrAsg(uint32_t* slot, uint32_t value) {
    if (*slot != value || !value) dl::Call(g_ustrAsg, U(slot), value);
}


static void SwapShip(void* s, int side, bool view) {
    uint8_t& fihc = dl::At<uint8_t>(s, E.fHuman);
    if (!DualOk()) {
        fihc = (uint8_t)(view ? side == g_viewSide : g_human[side]);
        return;
    }
    uint8_t& type = dl::At<uint8_t>(s, g_fIdType);
    uint8_t& hid = dl::At<uint8_t>(s, g_fHidden);
    uint32_t* cls = &dl::At<uint32_t>(s, g_fIdClass);
    Dual& d = g_dual[s];
    if (!d.known) {  // first seen: the values in the fields are the simulation's (and the display's)
        d.known = true;
        for (int g = 0; g < kGuard; g++)
            if (g_guard[g].off >= 0) d.simG[g] = d.dispG[g] = d.lastSimG[g] = GuardRead(s, g_guard[g]);
        d.simType = d.dispType = d.lastSimType = type;
        d.dispHidden = hid;
        StrAsg(&d.simClass, *cls);
        StrAsg(&d.dispClass, *cls);
        d.lastSimClass = *cls;
        if (view) {  // the fields hold the simulation's values: switch to the display set below
        } else {
            fihc = (uint8_t)g_human[side];
            hid = 0;
            return;
        }
    }
    if (view) {  // leaving the simulation: keep its values, show this PC's
        for (int g = 0; g < kGuard; g++) {
            if (g_guard[g].off < 0 || !g_guardOn) continue;
            uint32_t v = GuardRead(s, g_guard[g]);
            d.simG[g] = v;
            if (v != d.lastSimG[g]) d.dispG[g] = v;  // the simulation changed it: the display follows
            d.lastSimG[g] = v;
            GuardWrite(s, g_guard[g], d.dispG[g]);
        }
        d.simType = type;
        StrAsg(&d.simClass, *cls);
        if (type != d.lastSimType) d.dispType = type;
        if (*cls != d.lastSimClass) StrAsg(&d.dispClass, *cls);
        d.lastSimType = type;
        d.lastSimClass = *cls;
        type = d.dispType;
        hid = d.dispHidden;
        StrAsg(cls, d.dispClass);
        fihc = (uint8_t)(side == g_viewSide);
    } else {  // entering the simulation: keep the display's values, give it its own
        for (int g = 0; g < kGuard; g++) {
            if (g_guard[g].off < 0 || !g_guardOn) continue;
            uint32_t v = GuardRead(s, g_guard[g]);
            if (v != d.dispG[g]) g_guardHits++;
            d.dispG[g] = v;
            GuardWrite(s, g_guard[g], d.simG[g]);
        }
        d.dispType = type;
        d.dispHidden = hid;
        StrAsg(&d.dispClass, *cls);
        type = d.simType;
        hid = 0;
        StrAsg(cls, d.simClass);
        fihc = (uint8_t)g_human[side];
    }
}

static void SwapAll(bool view) {
    void* ships = List(E.allShipsVar);
    int n = dl::ListCount(ships);
    for (int i = 0; i < n; i++) {
        void* s = dl::ListItem(ships, i);
        if (!s) continue;
        int side = dl::At<int>(s, E.fShipSide);
        if (side == 0 || side == 1) SwapShip(s, side, view);
    }
}

static void WriteHumanFlags(bool view) {
    __try {
        SwapAll(view);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception in the display swap");
    }
}

// Drops the second sets (the ships are freed or the battle goes on without lockstep).
static void ReleaseDual() {
    for (auto& kv : g_dual) {
        if (kv.second.simClass) dl::Call(g_ustrAsg, U(&kv.second.simClass), 0);
        if (kv.second.dispClass) dl::Call(g_ustrAsg, U(&kv.second.dispClass), 0);
    }
    g_dual.clear();
}

static void ApplyViewSide() {
    if (g_viewSide < 0 || g_loading || !E.playerSideVar) return;
    bool view = !g_forceCanonical && (g_results > 0 || !(g_sim > 0 && g_ui == 0));
    *E.playerSideVar = view ? g_viewSide : 0;
    if (view != g_viewOn) {
        g_viewOn = view;
        WriteHumanFlags(view);
    }
}

// The simulation's set of the display-swapped fields (identification, sighting, human flags, PlayerSide 0) in the ships
// while the battle is hashed or saved; then this PC's display set again.
static void CanonBegin() {
    g_forceCanonical = true;
    ApplyViewSide();
}
static void CanonEnd() {
    g_forceCanonical = false;
    ApplyViewSide();
}

// Display swaps on for this PC's side; or off (side -1): the ships are about to be freed (a load), nothing is written.
static void SetView(int side) {
    if (side < 0) {
        g_viewSide = -1;
        g_viewOn = false;
        ReleaseDual();
        return;
    }
    ReleaseDual();
    g_viewSide = side;
    g_viewOn = false;  // the fields hold the simulation's values (just loaded)
    ApplyViewSide();
}

// The live battle is over on this PC: the display set stays in the ships (a single-player battle from here).
static void FreezeView() {
    if (g_viewSide >= 0 && !g_loading) {
        if (!g_viewOn) WriteHumanFlags(true);
        if (E.playerSideVar) *E.playerSideVar = g_viewSide;
    }
    g_viewSide = -1;
    g_viewOn = false;
    __try {
        ReleaseDual();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ---------------------------------------------------------------------------------------------
// Battle preferences the simulation reads (offset, length in the game's preferences record).
static const int kPrefs[][2] = {{0x10, 4},  {0x14, 4},  {0x18, 14}, {0x26, 4},  {0x2a, 4},  {0x2e, 2},  {0x34, 5},
                                {0x39, 4},  {0x3e, 2},  {0x42, 4},  {0x46, 4},  {0x4a, 4},  {0x4e, 4},  {0x52, 4},
                                {0x56, 4},  {0x65, 4},  {0x34e, 1}, {0x350, 2}, {0x356, 2}, {0x36e, 9}, {0x378, 3}};

static std::string Hex(const uint8_t* p, int n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (int i = 0; i < n; i++) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}
static bool Unhex(const std::string& s, uint8_t* out, int n) {
    if ((int)s.size() != n * 2) return false;
    for (int i = 0; i < n; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[i * 2 + k];
            int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (x < 0) return false;
            v = v * 16 + x;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

static std::string PrefsHex() {
    std::string s;
    if (!E.prefs) return s;
    for (auto& r : kPrefs) s += Hex(E.prefs + r[0], r[1]);
    return s;
}
static bool SetPrefsHex(const std::string& h) {
    if (!E.prefs) return false;
    size_t pos = 0;
    for (auto& r : kPrefs) {
        if (pos + r[1] * 2 > h.size()) return false;
        if (!Unhex(h.substr(pos, r[1] * 2), E.prefs + r[0], r[1])) return false;
        pos += r[1] * 2;
    }
    return true;
}

static unsigned FpuCw() {
    unsigned short cw = 0;
    __asm fnstcw cw
    return cw;
}
static void SetFpuCw(unsigned v) {
    unsigned short cw = (unsigned short)v;
    __asm fldcw cw
}
// The x87 control word must not change during a battle (rounding/precision decide hits and positions).
static void CheckFpu(const char* where) {
    unsigned cw = FpuCw();
    if (S.cw0 && cw != S.cw0) {
        Log("lockstep: FPU control word %04x at %s (expected %04x); restored", cw, where, S.cw0);
        SetFpuCw(S.cw0);
    }
}

// ---------------------------------------------------------------------------------------------
// The order guard. The order fields of every division, ship and force are snapshot when a simulation step ends
// ("agreed"). Before the next step, any difference on this PC's own side is a player's order: it becomes a numbered
// batch for the other PC, and the agreed value is put back so the simulation never runs with an order the other PC
// doesn't have. Between steps the player keeps seeing his pending orders (overlay). A release applies the batches it
// names on both PCs in a fixed order: actions of side 0, actions of side 1, values of side 0, values of side 1.

struct OField {
    const char* cls;
    const char* name;
    int size;  // bytes, or -2 for an object reference (sent as the object's key)
    int off;
};
static OField g_oDiv[] = {
    {"TDivision", "DivisionCourse", 4},   {"TDivision", "DivisionSpeed", 4},        {"TDivision", "TurnTogether", 1},
    {"TDivision", "TargetDiv", -2},       {"TDivision", "TacticalStance", 1},       {"TDivision", "AIControlled", 1},
    {"TDivision", "Role", 1},             {"TDivision", "Formation", 1},            {"TDivision", "LeadDivision", -2},
    {"TDivision", "CappedDivision", -2},  {"TDivision", "WithdrawMe", 1},           {"TDivision", "HoldFire", 4},
    {"TDivision", "HoldTorpedoFire", 1},  {"TDivision", "HoldMissileFire", 1},      {"TDivision", "DivisionObjective", 12},
    {"TDivision", "TorpedoTarget", -2},   {"TDivision", "ManuallySelectedTarget", 1}, {"TDivision", "ManualTargeting", 1},
    {"TDivision", "DropSmoke", 1},        {"TDivision", "DesiredCruiseSpeed", 4},   {"TDivision", "ForceLeadDivision", 1},
    {"TDivision", "WaypointIndex", 4},    {"TDivision", "KeepHighSpeed", 4},
};
static OField g_oShip[] = {
    {"TFightingShip", "OrderedCourse", 4},        {"TFightingShip", "OrderedSpeed", 4},
    {"TFightingShip", "TargetMain", -2},          {"TFightingShip", "SetNewTargetLastTurn", 1},
    {"TFightingShip", "ManuallySelectedTarget", 1}, {"TFightingShip", "DetachDelay", 4},
    {"TFightingShip", "MakingSmoke", 1},
};
static OField g_oForce[] = {
    {"TForce", "Course", 4},         {"TForce", "Speed", 4},          {"TForce", "ForceWithdraw", 4},
    {"TForce", "AIControlled", 1},   {"TForce", "Rallied", 1},        {"TForce", "FlotillaAttack", 1},
    {"TForce", "DoNotEnterPort", 1}, {"TForce", "ForceObjective", 12}, {"TForce", "TimeToTurnAway", 4},
    {"TForce", "HasChangedLead", 1},
};
static bool g_oResolved = false;

static void ResolveOrderFields() {
    if (g_oResolved) return;
    g_oResolved = true;
    for (auto& f : g_oDiv) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_oShip) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_oForce) f.off = dl::Field(f.cls, f.name);
}

static void FieldsOf(char kind, OField*& fs, int& n) {
    if (kind == 'D') fs = g_oDiv, n = sizeof(g_oDiv) / sizeof(g_oDiv[0]);
    else if (kind == 'S') fs = g_oShip, n = sizeof(g_oShip) / sizeof(g_oShip[0]);
    else fs = g_oForce, n = sizeof(g_oForce) / sizeof(g_oForce[0]);
}

struct OObj {
    char kind;
    void* p;
    int side;
    std::string key;
};
struct World {
    std::vector<OObj> objs;
    std::map<void*, std::string> keyOf;
    std::map<std::string, void*> objOf;
};

// Every order object with a key that is the same on both PCs: kind + name + "#" + the number of earlier objects of the
// same kind and name (lists are in creation order, the same on both PCs).
static void Walk(World& w) {
    ResolveOrderFields();
    w.objs.clear();
    w.keyOf.clear();
    w.objOf.clear();
    std::map<std::string, int> seen;
    auto add = [&](char kind, void* p, int side, const std::wstring& name) {
        std::string base = std::string(1, kind) + W2U(name);
        std::string key = base + "#" + std::to_string(seen[base]++);
        w.objs.push_back({kind, p, side, key});
        w.keyOf[p] = key;
        w.objOf[key] = p;
    };
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++)
        if (void* d = dl::ListItem(divs, i))
            add('D', d, dl::At<int>(d, E.fDivSide), dl::ReadUStr(dl::At<void*>(d, E.fDivName)));
    void* ships = List(E.allShipsVar);
    for (int i = 0; i < dl::ListCount(ships); i++)
        if (void* s = dl::ListItem(ships, i))
            add('S', s, dl::At<int>(s, E.fShipSide), dl::ReadUStr(dl::At<void*>(s, E.fShipName)));
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* f = dl::ListItem(forces, i))
            add('F', f, dl::At<int>(f, E.fForceSide), dl::ReadUStr(dl::At<void*>(f, E.fForceName)));
}

static std::string ReadField(const OField& f, void* obj, const World& w) {
    uint8_t* p = (uint8_t*)obj + f.off;
    if (f.size == -2) {
        void* r = *(void**)p;
        if (!r) return "-";
        auto it = w.keyOf.find(r);
        return it == w.keyOf.end() ? "?" : it->second;
    }
    return Hex(p, f.size);
}

static bool WriteField(const OField& f, void* obj, const std::string& v, const World& w) {
    uint8_t* p = (uint8_t*)obj + f.off;
    if (f.size == -2) {
        if (v == "?") return false;
        void* r = nullptr;
        if (v != "-") {
            auto it = w.objOf.find(v);
            if (it == w.objOf.end()) return false;
            r = it->second;
        }
        *(void**)p = r;
        return true;
    }
    return Unhex(v, p, f.size);
}

static std::unordered_map<std::string, std::string> g_agreed;  // "<obj key>\x1f<field>" -> value
static std::map<std::string, Overlay> g_overlay;               // this PC's orders not released yet

static std::string FK(const std::string& key, int i) { return key + "\x1f" + std::to_string(i); }

static bool SplitFK(const std::string& fk, std::string& key, int& idx) {
    size_t p = fk.rfind('\x1f');
    if (p == std::string::npos) return false;
    key = fk.substr(0, p);
    idx = atoi(fk.c_str() + p + 1);
    return !key.empty();
}

static void SendMsg(KV kv, const std::string& body = "");

static void TakeAgreed() {
    World w;
    Walk(w);
    g_agreed.clear();
    for (auto& o : w.objs) {
        OField* fs;
        int n;
        FieldsOf(o.kind, fs, n);
        for (int i = 0; i < n; i++)
            if (fs[i].off >= 0) g_agreed[FK(o.key, i)] = ReadField(fs[i], o.p, w);
    }
}

static int RestoreAgreed() {
    World w;
    Walk(w);
    int n = 0;
    for (auto& o : w.objs) {
        OField* fs;
        int nf;
        FieldsOf(o.kind, fs, nf);
        for (int i = 0; i < nf; i++) {
            if (fs[i].off < 0) continue;
            auto it = g_agreed.find(FK(o.key, i));
            if (it == g_agreed.end()) continue;
            if (ReadField(fs[i], o.p, w) != it->second && WriteField(fs[i], o.p, it->second, w)) n++;
        }
    }
    return n;
}

static void ReapplyOverlay() {
    if (g_overlay.empty()) return;
    World w;
    Walk(w);
    for (auto& ov : g_overlay) {
        std::string key;
        int idx;
        if (!SplitFK(ov.first, key, idx)) continue;
        auto it = w.objOf.find(key);
        if (it == w.objOf.end()) continue;
        OField* fs;
        int nf;
        FieldsOf(key[0], fs, nf);
        if (idx >= 0 && idx < nf && fs[idx].off >= 0) WriteField(fs[idx], it->second, ov.second.value, w);
    }
}

static bool MySideHuman() { return S.mySide == 0 || !S.peerAI; }

static void SendBatch(const Batch& b) {
    std::string items;
    for (size_t i = 0; i < b.items.size(); i++) items += (i ? "\x1e" : "") + b.items[i];
    KV kv;
    kv["t"] = "batch";
    kv["side"] = std::to_string(S.mySide);
    kv["seq"] = std::to_string(b.seq);
    SendMsg(kv, items);
}

static void QueueBatch(std::vector<std::string>& items, const char* why) {
    if (items.empty()) return;
    Batch b;
    b.seq = ++S.mySeq;
    b.items = items;
    S.myBatches.push_back(b);
    Log("lockstep: orders #%d (%s, %d items) at t=%s: %s", b.seq, why, (int)items.size(), ClockText().c_str(),
        items[0].substr(0, 120).c_str());
    SendBatch(b);
}

// New orders of this PC's player (fields that differ from the agreed value and from the pending orders).
static void DetectEdits(const char* why) {
    if (!S.on || S.st != St::Running || g_loading || !MySideHuman()) return;
    World w;
    Walk(w);
    std::vector<std::string> items;
    for (auto& o : w.objs) {
        if (o.side != S.mySide) continue;
        OField* fs;
        int nf;
        FieldsOf(o.kind, fs, nf);
        for (int i = 0; i < nf; i++) {
            if (fs[i].off < 0) continue;
            std::string fk = FK(o.key, i);
            const std::string* expect = nullptr;
            auto ov = g_overlay.find(fk);
            if (ov != g_overlay.end())
                expect = &ov->second.value;
            else {
                auto ag = g_agreed.find(fk);
                if (ag != g_agreed.end()) expect = &ag->second;
            }
            if (!expect) continue;
            std::string cur = ReadField(fs[i], o.p, w);
            if (cur == *expect || cur == "?") continue;
            items.push_back("V" + fk + "\x1f" + cur);
            g_overlay[fk] = {cur, S.mySeq + 1};
        }
    }
    QueueBatch(items, why);
}

// Leak check (tests): the simulation's state (everything hashed except the orders and the display's identification,
// plus the order of the division and ship lists) at the end of a simulation step must be the same at the start of the
// next one; a difference was made by this PC's display code or the mod (it would desync the two PCs).
struct LocalHash {
    uint64_t g[G_COUNT + 1];
};
static void LocalSimHash(LocalHash& h) {
    ResolveSpecs();
    for (auto& x : h.g) x = 1469598103934665603ull;
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* dv = dl::ListItem(divs, i);
        if (!dv) continue;
        Fnv(h.g[G_COUNT], &dv, 4);
        void* ships = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(ships); j++) {
            void* sp = dl::ListItem(ships, j);
            if (!sp) continue;
            Fnv(h.g[G_COUNT], &sp, 4);
            for (auto& f : g_shipFields)
                if (f.off >= 0 && f.size > 0 && f.group != G_IDENT && f.group != G_ORD)
                    Fnv(h.g[f.group], (uint8_t*)sp + f.off, f.size);
        }
    }
    void* torps = List(E.torpVar);
    for (int i = 0; i < dl::ListCount(torps); i++)
        if (void* t = dl::ListItem(torps, i))
            for (auto& f : g_torpFields)
                if (f.off >= 0 && f.size > 0) Fnv(h.g[G_TORP], (uint8_t*)t + f.off, f.size);
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* a = dl::ListItem(air, i))
            for (auto& f : g_airFields)
                if (f.off >= 0 && f.size > 0) Fnv(h.g[G_AIR], (uint8_t*)a + f.off, f.size);
}
static LocalHash g_exitHash;
static std::vector<std::pair<void*, std::string>> g_exitShips;  // the hashed ship fields at the last exit (raw bytes)
static bool g_exitHashOk = false;
static int g_exitFrame = -2;
static int g_leaks = 0;

static int g_outerIdxFwd() { return g_outerIdx; }

static std::string ShipFieldBytes(void* sp) {
    std::string b;
    for (auto& f : g_shipFields)
        if (f.off >= 0 && f.size > 0) b.append((const char*)sp + f.off, f.size);
    return b;
}

static void LeakCheckEnter(int idx) {
    if (!g_leakCheck || !g_exitHashOk || !S.on || S.st != St::Running || g_loading) return;
    g_exitHashOk = false;
    LocalHash h;
    LocalSimHash(h);
    std::string groups;
    for (int g = 0; g <= G_COUNT; g++)
        if (h.g[g] != g_exitHash.g[g]) groups += std::string(groups.empty() ? "" : ",") + (g == G_COUNT ? "list order" : kGroupNames[g]);
    if (groups.empty()) return;
    std::string ui;
    for (int u : g_uiSince) ui += std::string(ui.empty() ? "" : ",") + g_ctx[u].name;
    std::string fields;  // which fields of which ships
    int nf = 0;
    for (auto& e : g_exitShips) {
        std::string now = ShipFieldBytes(e.first);
        if (now == e.second) continue;
        size_t pos = 0;
        for (auto& f : g_shipFields) {
            if (f.off < 0 || f.size <= 0) continue;
            if (f.group != G_IDENT && f.group != G_ORD && now.compare(pos, f.size, e.second, pos, f.size) != 0 && nf++ < 4)
                fields += " " + W2U(ObjName(e.first)) + "." + f.name + " " + Hex((uint8_t*)e.second.data() + pos, f.size) +
                          "->" + Hex((uint8_t*)now.data() + pos, f.size);
            pos += f.size;
        }
    }
    if (++g_leaks <= 50)
        Log("lockstep: LEAK the simulation state changed outside the simulation (%s) between %s and %s at t=%s; display "
            "routines in between: %s;%s",
            groups.c_str(), g_exitFrame == kManual ? "a shared section" : g_exitFrame >= 0 ? g_ctx[g_exitFrame].name : "?",
            idx == kManual ? "a shared section" : idx >= 0 ? g_ctx[idx].name : "?", ClockText().c_str(),
            ui.empty() ? "none" : ui.c_str(),
            fields.c_str());
}
static void LeakCheckExit(int idx) {
    if (!g_leakCheck) return;
    LocalSimHash(g_exitHash);
    g_exitShips.clear();
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++)
        if (void* dv = dl::ListItem(divs, i)) {
            void* ships = dl::At<void*>(dv, E.fShips);
            for (int j = 0; j < dl::ListCount(ships); j++)
                if (void* sp = dl::ListItem(ships, j)) g_exitShips.push_back({sp, ShipFieldBytes(sp)});
        }
    g_exitHashOk = true;
    g_exitFrame = idx;
    g_uiSince.clear();
}

// The guard around every simulation step.
static void OnSimOuterEnter() {
    if (!S.on || S.st != St::Running || g_loading) return;
    CheckFpu("step");
    __try {
        DetectEdits("step");
        int n = RestoreAgreed();
        if (n) Log("lockstep: %d pending order fields held back for this step", n);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception in the order guard (enter)");
    }
}

static void OnSimOuterExit() {
    if (!S.on || S.st != St::Running || g_loading) return;
    __try {
        TakeAgreed();
        LeakCheckExit(g_outerIdx);
        ReapplyOverlay();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception in the order guard (exit)");
    }
}

// Actions: DetachShip / CheckSupportingDDToDetach run from the battle screen (between minutes) are recorded with their
// arguments and replayed on the other PC at the release.
static std::string g_actA, g_actB;
static bool g_actLocal = false;

static void OnActionEnter(int idx, void* a, void* b) {
    g_actLocal = S.on && S.st == St::Running && !g_loading && g_manual == 0;
    if (!g_actLocal) return;
    World w;
    Walk(w);
    g_actA = w.keyOf.count(a) ? w.keyOf[a] : "?";
    g_actB = w.keyOf.count(b) ? w.keyOf[b] : "?";
    if (idx == H_DETACH && S.minuteRunning) Log("lockstep: WARNING detach during a minute (desync likely)");
}

static void OnActionExit(int idx, void* result) {
    if (!g_actLocal) return;
    g_actLocal = false;
    std::vector<std::string> items;
    if (idx == H_DETACH) {
        World w;
        Walk(w);
        std::string nk = w.keyOf.count(result) ? w.keyOf[result] : "-";
        items.push_back("Adetach\x1f" + g_actA + "\x1f" + g_actB + "\x1f" + nk);
    } else {
        items.push_back("Acsdd\x1f" + g_actA + "\x1f" + g_actB);
    }
    QueueBatch(items, "action");
}

static bool ReplayAction(const std::vector<std::string>& a) {
    void* m = Frm();
    World w;
    Walk(w);
    auto obj = [&](const std::string& k) -> void* {
        auto it = w.objOf.find(k);
        return it == w.objOf.end() ? nullptr : it->second;
    };
    bool ok = false;
    if (a[0] == "detach" && a.size() >= 4) {
        void *div = obj(a[1]), *ship = obj(a[2]);
        if (m && div && ship) {
            void* nd = (void*)(uintptr_t)SafeCall(E.mDetachShip, U(m), U(div), U(ship), 0, nullptr, &ok);
            World w2;
            Walk(w2);
            std::string nk = w2.keyOf.count(nd) ? w2.keyOf[nd] : "-";
            if (nk != a[3]) Log("lockstep: replayed detach made %s, the other PC %s", nk.c_str(), a[3].c_str());
        }
    } else if (a[0] == "csdd" && a.size() >= 3) {
        void *orig = obj(a[1]), *nd = obj(a[2]);
        if (m && orig && nd) SafeCall(E.mCSDD, U(m), U(orig), U(nd), 0, nullptr, &ok);
    }
    Log("lockstep: replayed %s %s -> %s", a[0].c_str(), a.size() > 1 ? a[1].c_str() : "", ok ? "ok" : "FAILED");
    return ok;
}

// Applies the released batches (inside a shared section). Returns the number of sides that had actions.
static int ApplyBatches(int seq0, int seq1) {
    std::vector<Batch> bySide[2];
    int mySide = S.mySide, peerSide = 1 - S.mySide;
    int mySeqTo = mySide == 0 ? seq0 : seq1, peerSeqTo = peerSide == 0 ? seq0 : seq1;
    for (auto& b : S.myBatches)
        if (b.seq <= mySeqTo) bySide[mySide].push_back(b);
    for (auto& kv : S.peerBatches)
        if (kv.first <= peerSeqTo) bySide[peerSide].push_back(kv.second);
    int withActions = 0;
    for (int side = 0; side < 2; side++) {
        bool any = false;
        for (auto& b : bySide[side])
            for (auto& it : b.items)
                if (!it.empty() && it[0] == 'A') {
                    any = true;
                    if (side != mySide) ReplayAction(Split(it.substr(1), '\x1f'));
                }
        if (any) withActions++;
    }
    World w;
    Walk(w);
    int nv = 0, bad = 0;
    for (int side = 0; side < 2; side++)
        for (auto& b : bySide[side])
            for (auto& it : b.items) {
                if (it.empty() || it[0] != 'V') continue;
                std::vector<std::string> p = Split(it.substr(1), '\x1f');
                if (p.size() < 3) continue;
                auto o = w.objOf.find(p[0]);
                OField* fs;
                int nf;
                FieldsOf(p[0][0], fs, nf);
                int idx = atoi(p[1].c_str());
                if (o == w.objOf.end() || idx < 0 || idx >= nf || fs[idx].off < 0 ||
                    !WriteField(fs[idx], o->second, p[2], w))
                    bad++;
                else
                    nv++;
            }
    // released own orders leave the overlay (unless a newer order of the same field is still pending)
    for (auto it = g_overlay.begin(); it != g_overlay.end();)
        it = it->second.seq <= mySeqTo ? g_overlay.erase(it) : std::next(it);
    S.myBatches.erase(std::remove_if(S.myBatches.begin(), S.myBatches.end(),
                                     [&](const Batch& b) { return b.seq <= mySeqTo; }),
                      S.myBatches.end());
    for (auto it = S.peerBatches.begin(); it != S.peerBatches.end();)
        it = it->first <= peerSeqTo ? S.peerBatches.erase(it) : std::next(it);
    if (nv || bad || withActions)
        Log("lockstep: released orders side0<=#%d side1<=#%d: %d fields%s, %d with actions", seq0, seq1, nv,
            bad ? (" (" + std::to_string(bad) + " not found)").c_str() : "", withActions);
    return withActions;
}

static void ClearOrders() {
    S.myBatches.clear();
    S.peerBatches.clear();
    g_overlay.clear();
    g_agreed.clear();
}

// ---------------------------------------------------------------------------------------------
// Snapshots: the battle screen's own Save (the scenario .sac + the campaign with the battle in progress) into this PC's
// slot; the slot's files travel; both PCs load them through the game's campaign load (which resumes the battle).

static bool IsAutosave(const std::wstring& n) {
    return n.size() >= 8 && _wcsnicmp(n.c_str(), L"Autosave", 8) == 0;
}

// Names saveio writes itself (its own rule); the battle's log files ("AA Log.skv", ...) are written here.
static bool PlainName(const std::string& n) {
    if (n.empty() || n.size() > 100 || n.find("..") != std::string::npos) return false;
    for (char c : n)
        if (!(isalnum((unsigned char)c) || c == '_' || c == '.' || c == '{' || c == '}' || c == '-')) return false;
    return true;
}
static bool ExtraName(const std::string& n) {
    if (n.empty() || n.size() > 100 || n.find("..") != std::string::npos) return false;
    for (char c : n)
        if ((unsigned char)c < 32 || strchr("\\/:*?\"<>|", c)) return false;
    return true;
}

static void DeleteMatching(const std::wstring& dir, const wchar_t* pattern) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// B: the battle's files into the battle slot (the previous battle's logs go first).
static bool WriteBattleFiles(const Bundle& b, int slot, std::string& err) {
    std::wstring dir = game::SaveDir(slot);
    Bundle plain;
    std::vector<const std::pair<std::string, std::string>*> extra;
    for (auto& f : b.files) {
        if (PlainName(f.first))
            plain.files.push_back(f);
        else if (ExtraName(f.first))
            extra.push_back(&f);
        else
            Log("lockstep: file '%s' of the battle not written (name)", f.first.c_str());
    }
    EnsureDir(dir);
    DeleteMatching(dir, L"*.skv");  // the previous battle's logs (before any file of this battle is written)
    if (!saveio::ApplyStateBundle(plain, dir, slot, err)) return false;
    for (auto* f : extra)
        if (!WriteFileBytes(dir + U2W(f->first), f->second)) {
            err = "cannot write " + f->first;
            return false;
        }
    return true;
}

static std::wstring NormName(const std::wstring& name, int slot) {
    std::wstring s = std::to_wstring(slot);
    for (const wchar_t* prefix : {L"RTWGame", L"MapData"}) {
        std::wstring pre = std::wstring(prefix) + s + L".";
        if (name.rfind(pre, 0) == 0) return std::wstring(prefix) + L"{S}." + name.substr(pre.size());
    }
    return name;
}

static bool BuildBundle(int slot, Bundle& b, std::string& err) {
    std::wstring dir = game::SaveDir(slot);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        err = "save folder not found";
        return false;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        if (IsAutosave(name) || _wcsnicmp(name.c_str(), L"rtw3mp", 6) == 0) continue;  // not the session's files
        std::string data;
        if (!ReadFileBytes(dir + name, data)) {
            err = "cannot read " + W2U(name);
            FindClose(h);
            return false;
        }
        b.Add(W2U(NormName(name, slot)), data);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!b.Get("RTWGame{S}.sac") || !b.Get("RTWGame{S}.bcs")) {
        err = "the battle save is missing";
        return false;
    }
    return true;
}

// The scenario names flag pictures with absolute paths of the PC that saved it.
static void FixFlagFiles(int slot, const std::string& gameDirA) {
    std::wstring path = game::SaveDir(slot) + L"RTWGame" + std::to_wstring(slot) + L".sac";
    std::string data, mine = W2U(GameDir());
    if (gameDirA.empty() || !ReadFileBytes(path, data)) return;
    std::string out;
    out.reserve(data.size() + 1024);
    int n = 0;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t e = data.find('\n', pos);
        if (e == std::string::npos) e = data.size() - 1;
        std::string line = data.substr(pos, e - pos + 1);
        size_t k = line.find("FlagFile=");
        if (k != std::string::npos && line.size() >= k + 9 + gameDirA.size() &&
            _strnicmp(line.c_str() + k + 9, gameDirA.c_str(), gameDirA.size()) == 0) {
            line = line.substr(0, k + 9) + mine + line.substr(k + 9 + gameDirA.size());
            n++;
        }
        out += line;
        pos = e + 1;
    }
    if (n && WriteFileBytes(path, out)) Log("lockstep: %d flag paths of the battle save point to this PC's game", n);
}

// Runs f; a structured exception (a Delphi exception, an access violation) is caught and reported as false. f's C++
// objects are not destroyed then (/EHsc): only use it for code whose leak is harmless.
static bool TryRun(const std::function<void()>& f) {
    __try {
        f();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception %08lx caught", GetExceptionCode());
        return false;
    }
}

static bool LoadCampaignGuarded(int slot) {
    __try {
        return game::LoadCampaign(slot, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception %08lx while loading slot %d", GetExceptionCode(), slot);
        return false;
    }
}

// The battle form may be freed and a campaign loaded only from the outermost message loop: no hooked battle routine on
// the stack, no modal dialog, no menu or window move/size loop, no mouse capture, no nested message loop (a native
// MessageBox, ProcessMessages).
static bool CleanStack(std::string* why = nullptr) {
    PruneBelow((uint32_t)(uintptr_t)_AddressOfReturnAddress(), false, "clean-stack check");
    const char* r = nullptr;
    GUITHREADINFO gti = {sizeof(gti)};
    if (g_nFrames > 0)
        r = "a battle routine is running";
    else if (game::ModalOpen())
        r = "a dialog is open";
    else if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) &&
             (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE | GUI_INMOVESIZE)))
        r = "a menu or a window move is active";
    else if (GetCapture())
        r = "the mouse is captured";
    else if (!AtOutermostDispatch())
        r = "inside a nested message loop";
    if (r && why) *why = r;
    static DWORD lastLog = 0;
    static const char* lastWhy = nullptr;
    if (r && (r != lastWhy || GetTickCount() - lastLog > 5000)) {
        lastLog = GetTickCount();
        Log("lockstep: waiting for the outermost message loop: %s (dispatcher %s, gui flags %lx)", r,
            DispatchDepthText().c_str(), gti.flags);
    }
    lastWhy = r;
    return r == nullptr;
}

static bool SaveSnapshot(std::string& err) {
    void* m = BattleForm();
    void* bc = E.buildCampVar ? *E.buildCampVar : nullptr;
    if (!m || !bc || !E.inProgVar) {
        err = "no battle";
        return false;
    }
    int slot = game::CurrentSlot();
    std::wstring path = game::SaveDir(slot) + L"RTWGame" + std::to_wstring(slot) + L".sac";
    dl::ConstUStr p(path);
    bool ok1 = false, ok2 = false;
    uint32_t st[1] = {0};
    DWORD t0 = GetTickCount();
    CanonBegin();  // the battle is saved with the simulation's state, not this PC's display set
    g_quiet++;
    SafeCall(E.saveScenario, U(m), p.ptr(), 0, 0, nullptr, &ok1);
    if (ok1) SafeCall(E.mSaveCampaign, U(bc), (uint32_t)*E.inProgVar, 0, 1, st, &ok2);
    g_quiet--;
    CanonEnd();
    Log("lockstep: battle saved into slot %d at t=%s (%s) in %lu ms", slot, ClockText().c_str(),
        ok1 && ok2 ? "ok" : "FAILED", GetTickCount() - t0);
    if (!ok1 || !ok2) err = "the game could not save the battle";
    return ok1 && ok2;
}

// The battle form as DoBattle makes it: free the old one, create a fresh (hidden) one into the unit variable.
static bool ResetBattleForm() {
    void* app = E.appVar ? *E.appVar : nullptr;
    if (!app || !E.vmtMain || !E.frmMainVar) return false;
    bool ok = true;
    void* f = *E.frmMainVar;
    if (f) {
        if (game::FormVisible(f)) SafeCall(E.mHide, U(f), 0, 0, 0, nullptr, &ok);
        SafeCall(E.mFree, U(f), 0, 0, 0, nullptr, &ok);
        *E.frmMainVar = nullptr;
    }
    if (ok) SafeCall(E.mCreateForm, U(app), U(E.vmtMain), U(E.frmMainVar), 0, nullptr, &ok);
    Log("lockstep: battle form reset (%s)", ok && *E.frmMainVar ? "ok" : "FAILED");
    return ok && *E.frmMainVar;
}

bool SetSideControl(int side, bool ai, bool human) {
    if (!Resolve()) return false;
    int nf = 0, nd = 0, ns = 0;
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* f = dl::ListItem(forces, i))
            if (dl::At<int>(f, E.fForceSide) == side) dl::At<uint8_t>(f, E.fFAI) = (uint8_t)ai, nf++;
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* dv = dl::ListItem(divs, i);
        if (!dv || dl::At<int>(dv, E.fDivSide) != side) continue;
        dl::At<uint8_t>(dv, E.fDAI) = (uint8_t)ai;
        nd++;
        void* ships = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(ships); j++)
            if (void* s = dl::ListItem(ships, j)) {
                dl::At<uint8_t>(s, E.fHuman) = (uint8_t)human;
                dl::At<uint8_t>(s, E.fSDAI) = (uint8_t)ai;
                ns++;
            }
    }
    if (side == 0 || side == 1) g_human[side] = human;
    Log("lockstep: side %d ai=%d human=%d (%d forces, %d divisions, %d ships) at t=%s", side, ai, human, nf, nd, ns,
        ClockText().c_str());
    return true;
}

// The ship tree and the map of this PC's side.
static void RefreshView() {
    void* m = BattleForm();
    if (!m || !E.mFillShipTree || E.fTvShips < 0 || !E.playerSideVar) return;
    uint32_t st[2] = {1, 0};  // FillShipTree(tvShips, side, ShowShipData, nil)
    bool ok;
    SafeCall(E.mFillShipTree, U(m), U(dl::At<void*>(m, E.fTvShips)), (uint32_t)*E.playerSideVar, 2, st, &ok);
    InvalidateRect(game::FormHwnd(m), nullptr, TRUE);
}

static int CurSpeed() {
    void* m = BattleForm();
    return m ? dl::At<int>(m, E.fGameSpeed) : 0;
}

static void ApplySpeed(int s) {  // inside a shared section
    void* m = BattleForm();
    if (!m || s < -3 || s > 5 || dl::At<int>(m, E.fGameSpeed) == s) return;
    dl::At<int>(m, E.fGameSpeed) = s;
    bool ok;
    SafeCall(E.setSpeed, U(m), 0, 0, 0, nullptr, &ok);
    Log("lockstep: game speed %d", s);
}

// Loads the slot's battle with the shared seed (both PCs do exactly this). first = the first start of this battle.
static bool LoadSnapshot(int slot, uint32_t seed, int speed, bool first, std::string& err) {
    DWORD t0 = GetTickCount();
    SetView(-1);
    ClearOrders();
    g_exitHashOk = false;  // (leak check) a new state
    g_loading = true;
    S.minuteRunning = false;
    S.tokens = 0;
    bool ok = ResetBattleForm();
    if (ok) {
        SetRng(2, seed);
        g_sharedOther = true;
        g_quiet++;
        ok = LoadCampaignGuarded(slot);  // an exception counts as a failed load; the flags below are reset anyway
        g_quiet--;
        g_sharedOther = false;
        // Both PCs continue from the same stream position even if a stray timer drew numbers during the load (the
        // READY hash compares the loaded state).
        g_simState = seed * 0x9E3779B1u + 0x7F4A7C15u;
    }
    g_loading = false;
    if (!ok || !BattleVisible()) {
        err = ok ? "the battle did not open" : "the game could not load the battle";
        Log("lockstep: load of slot %d FAILED (%s)", slot, err.c_str());
        return false;
    }
    if (S.cw0) SetFpuCw(S.cw0);
    SharedBegin();
    bool setup = TryRun([&] {
        if (first) {
            SetSideControl(1, false, true);  // the other player commands side 1
        } else {
            g_human[1] = !S.peerAI;  // the battle save keeps the forces' and divisions' AI flags
            void* ships = List(E.allShipsVar);
            for (int i = 0; i < dl::ListCount(ships); i++)
                if (void* s = dl::ListItem(ships, i)) {
                    int side = dl::At<int>(s, E.fShipSide);
                    if (side == 0 || side == 1) dl::At<uint8_t>(s, E.fHuman) = (uint8_t)g_human[side];
                }
        }
        if (S.peerAI) SetSideControl(1, true, false);
        ApplySpeed(speed);
    });
    SharedEnd();
    if (!setup) {
        err = "the loaded battle could not be set up";
        return false;
    }
    S.boundary = Minute();
    S.atBoundary = true;
    S.minuteBroken = false;
    S.stalledSince = 0;
    TakeAgreed();
    SetView(g_noView ? -1 : S.mySide);
    g_traceSeq.clear();
    RefreshView();
    Log("lockstep: battle loaded from slot %d (seed %u, minute %d, t=%s) in %lu ms", slot, seed, S.boundary,
        ClockText().c_str(), GetTickCount() - t0);
    return true;
}

static std::string HashNow() {
    char b[48];
    snprintf(b, sizeof(b), "%016llx-%08x", (unsigned long long)StateHash().all, g_simState);
    return b;
}

// ---------------------------------------------------------------------------------------------
// Dialog policy. Inside a minute (the simulation) the game's questions get a fixed answer on both PCs: keep the battle
// going. The texts go to this PC's chat on side 0 (they are written from side 0's point of view).

static std::string g_lastBox;
static bool g_pocGate = false, g_pocGateHeld = false, g_pocHash = false;
static int g_pocTokens = 0, g_pocHashCount = 0;
static double g_pocLastHashClock = -1;

static bool Contains(const std::wstring& s, const wchar_t* sub) {
    std::wstring a = s, b = sub;
    for (auto& c : a) c = towlower(c);
    for (auto& c : b) c = towlower(c);
    return a.find(b) != std::wstring::npos;
}

static void OnGameText(const std::wstring& text, bool proposal);

extern "C" int __stdcall LS_MsgDlg(const wchar_t* msg, int type, int buttons) {
    if (g_results > 0 && !g_quiet) return -1;  // the result screen's questions are each player's own
    bool sim = g_sim > 0 && S.on;
    if (!sim && !g_quiet && !g_answerYes) return -1;  // the player's own dialogs
    std::wstring text = msg ? dl::ReadUStr(msg) : L"";
    bool yes = buttons & 1, no = (buttons & 2) != 0, ok = (buttons & 4) != 0, cancel = (buttons & 8) != 0;
    int r = ok ? 1 : yes ? 6 : cancel ? 2 : 7;
    bool proposal = false;
    if (yes && no) {
        if (g_answerYes)
            r = 6;
        else if (Contains(text, L"continue")) {
            r = 6;  // "... Do you want to continue searching?"
            proposal = Contains(text, L"search");
        } else {
            r = 7;  // "... Do you want to end the scenario?", "Stand down strike?", detach questions
            proposal = Contains(text, L"end the scenario");
        }
    }
    g_lastBox = W2U(text);
    Log("lockstep: %s box '%s' answered %d", g_quiet ? "quiet" : g_answerYes ? "end" : "minute", g_lastBox.c_str(), r);
    if (sim) OnGameText(text, proposal);
    return r;
}

extern "C" int __stdcall LS_ShowPic(const wchar_t* text, const wchar_t* pic) {
    if (!(S.on && g_sim > 0 && g_results == 0) && !g_quiet) return 0;
    std::wstring t = text ? dl::ReadUStr(text) : L"";
    Log("lockstep: picture message '%s' (%s) skipped", W2U(t).c_str(), pic ? W2U(dl::ReadUStr(pic)).c_str() : "");
    if (!g_quiet) OnGameText(t, false);
    return 1;
}

extern "C" int __stdcall LS_ExecGeneral(const wchar_t* prompt, int preselect) {
    bool sim = S.on && g_sim > 0 && g_results == 0;
    if (!sim && !g_quiet) return -1;
    std::wstring t = prompt ? dl::ReadUStr(prompt) : L"";
    int r = Contains(t, L"autostart") ? 2 : preselect >= 1 && preselect <= 3 ? preselect : 1;
    Log("lockstep: question '%s' answered %d", W2U(t).c_str(), r);
    return r;
}

// ---------------------------------------------------------------------------------------------
// The clock hooks

static void ClockRequest(const std::string& what, int n);
static void OnBoundary();

static void PostRunRequest() {
    static DWORD last = 0;  // a key that runs one minute directly
    if (GetTickCount() - last > 1000) {
        last = GetTickCount();
        RunOnMainThread([] { ClockRequest("run", 1); });
    }
}

static int MinuteGate(void* self) {
    if (S.on) {
        if (S.tokens > 0 && g_testStall) {  // tests: the released minute doesn't start (the watchdog recovers)
            g_testStall = false;
            S.tokens = 0;
            dl::At<int>(self, E.fMinutesLeft) = 0;
            Log("lockstep: TEST minute start refused (t=%s)", ClockText().c_str());
            return 0;
        }
        if (S.tokens > 0) {
            S.tokens--;
            S.minuteRunning = true;
            return 1;
        }
        if (dl::At<int>(self, E.fMinutesLeft) > 0) {  // a run this PC's clock didn't release
            dl::At<int>(self, E.fMinutesLeft) = 0;
            return 0;
        }
        return 2;
    }
    if (g_pocGate && dl::At<uint8_t>(self, E.fFinished) && dl::At<int>(self, E.fMinutesLeft) > 0 &&
        !dl::At<uint8_t>(self, E.fEndGame)) {
        if (g_pocTokens <= 0) {
            if (!g_pocGateHeld) Log("poc gate: holding the next minute (t=%s) until `pocgate step`", ClockText().c_str());
            g_pocGateHeld = true;
            dl::At<int>(self, E.fMinutesLeft) = 0;
            return 0;
        }
        g_pocTokens--;
        g_pocGateHeld = false;
        Log("poc gate: minute released (t=%s, %d tokens left)", ClockText().c_str(), g_pocTokens);
    }
    return 1;
}

extern "C" int __stdcall LS_StartMinuteAllowed(void* self) {
    int r = 1;
    __try {
        r = MinuteGate(self);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = 1;
    }
    if (r == 2) {
        PostRunRequest();
        return 0;
    }
    return r;
}

// The game ended a live battle inside a minute and the player chose "Exit battle" in its results: EndOfTurnProcedures
// would close the battle screen right away, and FormClose -> DoStrategicTurn -> DoEndOfTurn (the host's month) would run
// inside the minute (the battle's dialog policy, its RNG and contexts). The close waits until the minute is over and
// the live battle has ended normally on this PC; then it runs from the outermost message loop (CloseAfterEnd).
static bool g_closeAfterEnd = false, g_closePosted = false;

static void OnResultsExit() {
    if (!S.on || g_sim == 0 || E.fCloseMain < 0 || !E.mClose) return;
    void* m = BattleForm();
    if (!m || !dl::At<uint8_t>(m, E.fCloseMain)) return;
    dl::At<uint8_t>(m, E.fCloseMain) = 0;
    g_closeAfterEnd = true;
    Log("lockstep: \"Exit battle\" in the results: the battle screen closes after this minute (t=%s)", ClockText().c_str());
}

static void CloseAfterEnd() {
    g_closePosted = false;
    if (!g_closeAfterEnd) return;
    if (!BattleVisible()) {  // the player closed it meanwhile
        g_closeAfterEnd = false;
        return;
    }
    std::string wait;
    if (S.on || !CleanStack(&wait)) return;  // Tick posts it again
    g_closeAfterEnd = false;
    Log("lockstep: closing the battle screen (\"Exit battle\" in the results)");
    bool ok;
    SafeCall(E.mClose, U(BattleForm()), 0, 0, 0, nullptr, &ok);
    if (!ok) Log("lockstep: closing the battle screen FAILED");
}

// A simulation routine of this PC's running minute was unwound by an exception (the game's own handler caught it): the
// minute may never reach its end (the watchdog in Tick decides once the battle's timers stopped).
static void OnSimFrameUnwound(int idx) {
    if (idx == kManual || !S.on || !S.minuteRunning) return;
    if (!S.minuteBroken) Log("lockstep: the running minute lost %s to an exception (t=%s)", g_ctx[idx].name, ClockText().c_str());
    S.minuteBroken = true;
}

static void AfterMinute() {
    if (g_pocHash && E.clockVar && *E.clockVar != g_pocLastHashClock) {
        g_pocLastHashClock = *E.clockVar;
        char tag[40];
        snprintf(tag, sizeof(tag), "poc hash #%d", g_pocHashCount++);
        Log("%s", HashLine(tag).c_str());
    }
    if (S.on && (S.st == St::Running || S.st == St::Holding)) {
        S.minuteRunning = false;
        S.minuteBroken = false;
        S.stalledSince = 0;
        RunOnMainThread(OnBoundary);  // after the interval timer's handler has finished
    }
}

extern "C" void __stdcall LS_AfterEOTP(void* self) {
    try {
        AfterMinute();
    } catch (...) {
    }
}

extern "C" int __stdcall LS_StartRun(void* self, int n) {
    if (!S.on || S.ownRun) return 0;
    if (dl::At<uint8_t>(self, E.fEndGame)) return 0;  // the game's "Scenario is finished!"
    RunOnMainThread([n] { ClockRequest("run", n); });
    return 1;
}

// The speed the player picked in the combo box (its ItemIndex - 3, as cbGameSpeedChange reads it), then the box shows
// the speed in use again.
static int ReadSpeedCombo(void* self, void* m) {
    int want = INT_MIN;
    __try {
        void* cb = dl::At<void*>(self, E.fCbSpeed);
        void** vt = cb ? *(void***)cb : nullptr;
        if (vt && g_speedGetIdx > 0) want = (int)dl::Call(vt[g_speedGetIdx / 4], U(cb)) - 3;
        if (m) dl::Call(E.setSpeed, U(m));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return want;
}

// Click handlers: 1 = handled here (the game's handler doesn't run).
extern "C" int __stdcall LS_Block(int idx, void* self) {
    if (!S.on || idx < 0 || idx >= kBlockers) return 0;
    const Blocker& b = g_blockers[idx];
    void* m = BattleForm();
    switch (b.policy) {
        case B_PAUSE:
            RunOnMainThread([] { ClockRequest("pause", 0); });
            return 1;
        case B_END:
            if (m && dl::At<uint8_t>(m, E.fEndGame)) return 0;
            RunOnMainThread([] { ClockRequest("end", 0); });
            return 1;
        case B_DETACH:
            if (S.st == St::Running && !S.minuteRunning && !S.inAction && S.runLeft <= 0) return 0;
            RunOnMainThread([] { Chat("Pause the battle first: ships can be detached between minutes."); });
            return 1;
        case B_SPEED: {
            int want = ReadSpeedCombo(self, m);
            if (want >= -3 && want <= 5) RunOnMainThread([want] { ClockRequest("speed", want); });
            return 1;
        }
        case B_AUTORUN:
            RunOnMainThread([] { Chat("Auto-run is not available in a live battle; use Run (until a break)."); });
            return 1;
        case B_SAVE:
            RunOnMainThread([] { Chat("A live battle can't be saved; it is saved for the other player when it starts."); });
            return 1;
        default: {
            std::string what = b.name;
            RunOnMainThread([what] { Chat(what + " is not available in a live battle (this version)."); });
            return 1;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Messages: [u32 header length][header: key=value lines][body]. Every message carries the battle id and the epoch
// (a new epoch starts with every (re)load; older messages are ignored).

static std::string PackHB(const std::string& header, const std::string& body) {
    std::string s;
    uint32_t n = (uint32_t)header.size();
    s.append((const char*)&n, 4);
    s += header;
    s += body;
    return s;
}

static bool UnpackHB(const std::string& p, KV& header, std::string& body) {
    if (p.size() < 4) return false;
    uint32_t n;
    memcpy(&n, p.data(), 4);
    if (4 + (size_t)n > p.size()) return false;
    header = KVDecode(p.substr(4, n));
    body = p.substr(4 + n);
    return true;
}

static void SendMsg(KV kv, const std::string& body) {
    kv["bid"] = std::to_string(S.bid);
    kv["epoch"] = std::to_string(S.epoch);
    if (g_muted || !S.send || (S.isA && S.peerAI)) return;
    S.send(PackHB(KVEncode(kv), body));
}

static std::string Who(int side) { return S.sideNames[side].empty() ? "side " + std::to_string(side) : S.sideNames[side]; }

static void AddClock(KV& kv) {
    kv["left"] = std::to_string(S.runLeft);
    kv["speed"] = std::to_string(S.wantSpeed != INT_MIN ? S.wantSpeed : CurSpeed());
    kv["e0"] = S.endWant[0] ? "1" : "0";
    kv["e1"] = S.endWant[1] ? "1" : "0";
}

static void SendInfo(const std::string& msg = "") {
    if (!S.isA) return;
    KV kv;
    kv["t"] = "info";
    AddClock(kv);
    if (!msg.empty()) kv["msg"] = msg;
    SendMsg(kv);
}

// A line for both players (A shows it and sends it along).
static void ChatBoth(const std::string& s) {
    Chat(s);
    SendInfo(s);
}

static void OnGameText(const std::wstring& text, bool proposal) {
    if (!S.isA || text.empty()) return;  // B: the texts are written for side 0
    std::string t = W2U(text);
    if (proposal) {
        S.proposal = true;
        RunOnMainThread([t] { ChatBoth("The game asks: \"" + t + "\" Press End on both sides to end the battle."); });
    } else {
        RunOnMainThread([t] { Chat("Battle: " + t); });
    }
}

static uint32_t NewSeed() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    uint32_t s = (uint32_t)q.LowPart ^ (GetTickCount() << 7) ^ (uint32_t)(S.bid * 2654435761u);
    return s ? s : 1;
}

static void Cleanup() {
    SetRng(0, 0);
    FreezeView();  // keep this PC's view (as in a single-player battle)
    if (!S.isA && !S.savedPrefs.empty()) SetPrefsHex(S.savedPrefs);
    S.savedPrefs.clear();
    if (g_cwSaved) {  // B: back to this PC's own x87 control word
        SetFpuCw(g_cwSaved);
        g_cwSaved = 0;
    }
    ClearOrders();
    S.tokens = 0;
    S.minuteRunning = false;
    S.minuteBroken = false;
    S.atBoundary = false;
    S.stalledSince = 0;
    S.hasPendingRel = false;
    S.loadQueued = false;
    S.pendingStart.clear();
    S.runLeft = 0;
}

static void Ended(bool normal, const std::string& why) {
    if (!S.on || S.st == St::Ended) return;
    if (S.isA) {
        KV kv;
        kv["t"] = normal ? "fin" : "abort";
        kv["why"] = why;
        SendMsg(kv);
    }
    S.st = St::Ended;
    S.endedNormal = normal;
    S.why = why;
    S.leavePending = false;  // the battle screen (with its results) stays; the session leaves once it is closed
    S.finSince = 0;
    Cleanup();
    S.on = false;
    Chat(normal ? "The battle is over (" + why + ")." : "The live battle ended: " + why);
    if (g_hooks.onEnded) g_hooks.onEnded(normal, why);
}

// B: the battle doesn't count here; drop it as soon as the stack is clean.
static void AbortB(const std::string& why) {
    if (S.isA || !S.on || S.st == St::Ended) return;
    if (!S.leavePending) Log("lockstep: leaving the battle: %s", why.c_str());
    S.leavePending = true;
    S.leaveWhy = why;
}

static void TryLeave() {
    if (!S.leavePending || S.inAction || g_loading) return;
    std::string wait;
    if (!CleanStack(&wait)) return;
    S.leavePending = false;
    Cleanup();
    if (BattleForm()) ResetBattleForm();  // the copy is discarded
    S.st = St::Ended;
    S.on = false;
    Chat("The live battle ended on this PC: " + S.leaveWhy);
    if (g_hooks.onEnded) g_hooks.onEnded(false, S.leaveWhy);
}

// A: the other player is gone; side 1 goes to the game's AI and this battle goes on as a single-player battle.
static void DoDrop() {
    S.dropPending = false;
    std::string why = S.dropWhy;
    if (!S.on) return;
    bool loaded = S.st == St::Running || S.st == St::WaitReady;
    KV kv;
    kv["t"] = "drop";
    kv["why"] = why;
    SendMsg(kv);
    S.peerAI = true;
    if (loaded && BattleForm()) {
        SharedBegin();
        TryRun([] { SetSideControl(1, true, false); });
        SharedEnd();
    }
    Cleanup();
    S.st = St::Ended;
    S.on = false;
    Chat(S.peerName + " left the live battle (" + why + "). The AI commands " + Who(1) + " now; the battle goes on here.");
    if (g_hooks.notice) g_hooks.notice(S.peerName + " left the battle: " + why);
    if (g_hooks.onEnded) g_hooks.onEnded(false, why);
}

static void DropPeer(const std::string& why) {
    if (!S.isA || !S.on || S.st == St::Ended) return;
    if (S.st == St::Holding) {
        Log("lockstep: start abandoned: %s", why.c_str());
        S.on = false;
        S.st = St::Idle;
        Chat("The live battle could not start (" + why + "); the battle goes on against the AI.");
        if (g_hooks.onEnded) g_hooks.onEnded(false, why);
        return;
    }
    S.dropWhy = why;
    S.dropPending = true;
    Log("lockstep: dropping %s: %s", S.peerName.c_str(), why.c_str());
    if ((!S.minuteRunning || S.minuteBroken) && !S.inAction && !g_loading) DoDrop();
}

// A: a (re)load failed and the battle screen is gone. The battle save of this slot (just written) is loaded again the
// game's own way, without lockstep: the AI commands side 1 and the host's month goes on when the battle is closed. If
// even that fails the battle is lost until the player loads the slot (the session doesn't send a half-done month).
static void RecoverAlone(const std::string& err) {
    KV kv;
    kv["t"] = "abort";
    kv["why"] = err;
    SendMsg(kv);
    int slot = game::CurrentSlot();
    Cleanup();
    S.peerAI = true;
    bool ok = false;
    if (ResetBattleForm()) {
        g_quiet++;
        ok = LoadCampaignGuarded(slot) && BattleVisible();
        g_quiet--;
    }
    if (ok) {
        TryRun([] { SetSideControl(1, true, false); });
        Chat("The live battle could not be loaded (" + err + "); it was loaded again without " + S.peerName +
             ": the AI commands " + Who(1) + ".");
    } else {
        S.battleLost = true;
        Chat("The battle could not be loaded (" + err + "). Load save slot " + std::to_string(slot) +
             " to finish it; the month goes on after the battle.");
        if (g_hooks.notice) g_hooks.notice("The live battle failed: load save slot " + std::to_string(slot));
    }
    S.st = St::Ended;
    S.on = false;
    if (g_hooks.onEnded) g_hooks.onEnded(false, err);
}

static bool g_resyncPending = false;
static std::string g_resyncWhy;
static bool StartTransfer(bool resync, std::string& err);
static void ACheckReady();

static void RequestResync(const std::string& why) {
    if (!S.isA || g_resyncPending) return;
    g_resyncPending = true;
    g_resyncWhy = why;
    Log("lockstep: resync requested: %s", why.c_str());
}

static void DoResync() {
    g_resyncPending = false;
    if (!S.on || S.st == St::Ended) return;
    if (++S.resyncs > 3) {
        DropPeer("the battles could not be kept in sync");
        return;
    }
    ChatBoth("Synchronising the battle again (" + g_resyncWhy + ")...");
    std::string err;
    if (!StartTransfer(true, err)) {
        if (BattleForm() && BattleVisible())
            DropPeer("the battle could not be sent again: " + err);
        else
            RecoverAlone(err);
    }
}

// A: compares B's report of a boundary with its own.
static void ACheck() {
    if (!S.isA || S.peerAI || S.myHashN < 0 || S.peerDoneN != S.myHashN || S.verifiedN == S.myHashN) return;
    S.verifiedN = S.myHashN;
    if (S.peerHash == S.myHash) {
        if (++S.okMinutes >= 10) S.resyncs = 0;
        return;
    }
    Log("lockstep: DESYNC at minute %d: here %s, %s %s", S.myHashN, S.myHash.c_str(), S.peerName.c_str(),
        S.peerHash.c_str());
    S.okMinutes = 0;
    RequestResync("the two battles differ at minute " + std::to_string(S.myHashN));
}

static void CorruptForTest() {
    S.forceDesync = 0;
    void* dv = dl::ListItem(List(E.divisionsVar), 0);
    int f = dl::Field("TDivision", "DivisionCourse");
    if (dv && f >= 0) {
        dl::At<int>(dv, f) = (dl::At<int>(dv, f) + 37) % 360;
        Log("lockstep: TEST desync: %s course changed on this PC only", W2U(ObjName(dv)).c_str());
    }
}

static void AgreedEnd(bool prop);

// The minute this PC started did not run to its end (an exception unwound it, the game refused to start it, its timers
// stopped). A loads its own state again and sends it (a resync); B asks A for that. Repeated failures end the live
// battle.
static void MinuteFailed(const std::string& why) {
    S.minuteRunning = false;
    S.minuteBroken = false;
    S.stalledSince = 0;
    S.tokens = 0;
    Log("lockstep: the minute from %d failed: %s (t=%s, failure %d)", S.boundary, why.c_str(), ClockText().c_str(),
        S.brokenMinutes + 1);
    if (++S.brokenMinutes > 3) {
        if (S.isA) {
            DropPeer("the battle's minutes keep failing");
        } else {
            KV r;
            r["t"] = "abort";
            r["why"] = "the battle's minutes keep failing there";
            SendMsg(r);
            AbortB("the battle's minutes keep failing");
        }
        return;
    }
    if (S.isA) {
        RequestResync("a minute failed: " + why);
    } else {
        KV r;
        r["t"] = "req";
        r["what"] = "resync";
        r["why"] = "a minute failed there: " + why;
        SendMsg(r);
    }
}

// Both PCs: the minute released by A (n = the boundary it starts from).
static void ApplyRelease(const KV& kv) {
    void* m = BattleForm();
    int n = KVInt(kv, "n", -1), s0 = KVInt(kv, "s0"), s1 = KVInt(kv, "s1"), speed = KVInt(kv, "speed", CurSpeed());
    bool end = KVInt(kv, "end") != 0;
    if (!m) return;
    if (n != Minute()) {
        Log("lockstep: release of minute %d at minute %d", n, Minute());
        if (!S.isA) {
            KV r;
            r["t"] = "req";
            r["what"] = "resync";
            r["why"] = "a clock difference";
            SendMsg(r);
        }
        return;
    }
    S.atBoundary = false;
    CheckFpu("release");
    int withActions = 0;
    S.inAction = true;
    SharedBegin();
    bool endSoon = KVInt(kv, "endsoon") != 0 && E.scenLenVar;
    bool ok = TryRun([&] {
        withActions = ApplyBatches(s0, s1);
        if (S.forceDesync) CorruptForTest();
        ApplySpeed(speed);
        if (endSoon) {  // tests: the game itself ends the battle inside this minute (its absolute time limit)
            int was = *E.scenLenVar;
            *E.scenLenVar = std::max(1, n / 2 - 1);  // length + min(length, 600) <= minute n
            Log("lockstep: TEST scenario length %d -> %d: the game ends the battle at the end of minute %d", was,
                *E.scenLenVar, n + 1);
        }
    });
    SharedEnd();
    S.inAction = false;
    if (!ok) {  // the orders are applied only partly here
        MinuteFailed("the orders could not be applied");
        return;
    }
    if (end) {
        AgreedEnd(KVInt(kv, "prop") != 0);
        return;
    }
    if (withActions >= 2) {  // both players changed the order of battle at the same boundary: A's order counts
        if (S.isA) RequestResync("both players detached ships at the same time");
        return;
    }
    if (!S.isA) {  // B detached a ship after A released this minute: this PC already differs
        int s1mine = S.mySide == 0 ? s0 : s1;
        for (auto& b : S.myBatches)
            for (auto& it : b.items)
                if (b.seq > s1mine && !it.empty() && it[0] == 'A') {
                    Log("lockstep: own action #%d missed the release of minute %d; asking for a resync", b.seq, n);
                    KV r;
                    r["t"] = "req";
                    r["what"] = "resync";
                    r["why"] = "a detach missed the release";
                    SendMsg(r);
                    return;
                }
    }
    if (dl::At<uint8_t>(m, E.fEndGame)) {  // the game would only say "Scenario is finished!"
        if (!S.isA) {
            KV d;
            d["t"] = "done";
            d["n"] = std::to_string(S.boundary);
            d["h"] = S.myHash;
            d["end"] = "1";
            SendMsg(d);
        }
        Ended(true, "the battle is over");
        return;
    }
    S.tokens = 1;
    S.ownRun = true;
    S.minuteRunning = true;
    S.minuteBroken = false;
    S.stalledSince = 0;
    SafeCall(E.startRun, U(m), 1, 0, 0, nullptr, &ok);
    S.ownRun = false;
    if (!ok || (S.minuteRunning && !TimerOn(m, E.fRunTimer) && !TimerOn(m, E.fMoveTimer)))
        MinuteFailed(ok ? "the game did not start the minute" : "an exception in StartRun");
}

static void AgreedEnd(bool prop) {
    void* m = BattleForm();
    if (!m) return;
    S.inAction = true;
    g_answerYes++;
    g_lastBox.clear();
    SharedBegin();
    TryRun([&] {
        bool ok;
        SafeCall(prop ? E.endNow : E.endBattle, U(m), 0, 0, 0, nullptr, &ok);
    });
    SharedEnd();
    g_answerYes--;
    S.inAction = false;
    S.endWant[0] = S.endWant[1] = false;
    S.proposal = false;
    m = BattleForm();
    if (m && dl::At<uint8_t>(m, E.fEndGame)) {
        Ended(true, "ended by agreement");
        return;
    }
    S.atBoundary = true;  // the battle goes on from this boundary
    Chat("The battle can't end yet" + (g_lastBox.empty() ? std::string(".") : ": " + g_lastBox));
    if (S.isA) SendInfo();
}

// A: release the next minute when both PCs are at this boundary and the clock wants to run (or both agreed to end).
static void DoRelease(bool end) {
    DetectEdits("release");
    KV kv;
    kv["t"] = "rel";
    kv["n"] = std::to_string(S.boundary);
    kv["s0"] = std::to_string(S.mySeq);
    kv["s1"] = std::to_string(S.peerSeq);
    kv["speed"] = std::to_string(S.wantSpeed != INT_MIN ? S.wantSpeed : CurSpeed());
    kv["end"] = end ? "1" : "0";
    kv["prop"] = S.proposal ? "1" : "0";  // the game proposed the end: both end with the game's own end routine
    if (S.endSoon) {
        kv["endsoon"] = "1";
        S.endSoon = false;
    }
    S.wantSpeed = INT_MIN;
    if (!end && S.runLeft > 0 && S.runLeft < INT_MAX / 4) S.runLeft--;
    kv["left"] = std::to_string(S.runLeft);
    SendMsg(kv);
    S.waitSince = 0;
    ApplyRelease(kv);
}

static void ATryRelease() {
    if (!S.isA || !S.on || S.st != St::Running || S.minuteRunning || S.inAction || g_loading || g_resyncPending ||
        S.dropPending || S.peerEnded)
        return;
    // Only from a boundary OnBoundary has processed (not between the end of a minute and its posted OnBoundary).
    if (!S.atBoundary || Minute() != S.boundary) return;
    void* m = BattleForm();
    if (!m || dl::At<uint8_t>(m, E.fEndGame) || game::ModalOpen() || g_nFrames > 0) return;
    bool wantEnd = S.endWant[0] && S.endWant[1];
    if (!wantEnd && S.runLeft <= 0) return;
    if (!S.peerAI && !(S.peerDoneN == S.boundary && S.verifiedN == S.boundary)) return;
    DoRelease(wantEnd);
}

static void OnBoundary() {
    if (!S.on || S.st != St::Running || S.minuteRunning) return;
    void* m = BattleForm();
    if (!m) return;
    S.boundary = Minute();
    S.brokenMinutes = 0;
    CheckFpu("minute");
    // The hash covers the agreed state: this PC's pending orders (the overlay) are taken out while it is computed.
    DetectEdits("boundary");
    if (!g_overlay.empty()) RestoreAgreed();
    CanonBegin();  // the simulation's values, not this PC's display set
    S.myHash = HashNow();
    S.myHashN = S.boundary;
    bool ended = dl::At<uint8_t>(m, E.fEndGame) != 0;
    bool brk = dl::At<uint8_t>(m, E.fPause) != 0;
    if (g_guardHits) {
        Log("lockstep: the display wrote %d sighting fields since the last boundary (kept out of the simulation)",
            g_guardHits);
        g_guardHits = 0;
    }
    Log("%s", HashLine(S.isA ? "lockstep hash A" : "lockstep hash B").c_str());
    if (g_trace) {
        char name[96];
        snprintf(name, sizeof(name), "ls_dump_%s_%d_%d_%d.txt", S.isA ? "A" : "B", S.bid, S.epoch, S.boundary);
        DumpState(ModDataDir() + U2W(name));
    }
    CanonEnd();
    ReapplyOverlay();
    if (g_trace) {  // tests: the call sites (rva) of this minute's shared draws, one per line
        char name[96];
        snprintf(name, sizeof(name), "ls_trace_%s_%d_%d_%d.txt", S.isA ? "A" : "B", S.bid, S.epoch, S.boundary);
        std::string out;
        char b[16];
        for (uint32_t c : g_traceSeq) {
            snprintf(b, sizeof(b), "%x\n", c);
            out += b;
        }
        WriteFileBytes(ModDataDir() + U2W(name), out);
    }
    g_traceSeq.clear();
    if (!S.isA) {
        KV kv;
        kv["t"] = "done";
        kv["n"] = std::to_string(S.boundary);
        kv["h"] = S.myHash;
        kv["end"] = ended ? "1" : "0";
        SendMsg(kv);
        if (ended)
            Ended(true, "the battle is over");
        else
            S.atBoundary = true;
        return;
    }
    if (ended) {  // the end of the battle counts even if the other player is about to be dropped
        Ended(true, "the battle is over");
        return;
    }
    if (S.dropPending) DoDrop();
    if (!S.on) return;
    S.atBoundary = true;
    if (brk && S.runLeft > 0) {
        S.runLeft = 0;
        ChatBoth("The battle paused (" + (g_lastBox.empty() ? std::string("an event") : g_lastBox.substr(0, 100)) + ").");
    }
    ACheck();
    ATryRelease();
}

// A: the clock's requests (from this PC's buttons, or B's).
static void AHandleRequest(int side, const std::string& what, int n, const std::string& why = "") {
    std::string who = Who(side);
    if (S.st != St::Running) {
        if (side == 0) Chat("The live battle starts when " + S.peerName + " has loaded it.");
        return;
    }
    if (what == "run") {
        int m = n <= 0 ? 1 : n;
        S.runLeft = m >= 100000 ? INT_MAX / 2 : m;
        S.runWhat = m >= 100000 ? "until a break" : std::to_string(m) + " min";
        ChatBoth(who + (m == 1 ? " runs one minute." : m >= 100000 ? " runs the battle until a break." : " runs " + std::to_string(m) + " minutes."));
    } else if (what == "pause") {
        if (S.runLeft > 0) ChatBoth(who + " paused the battle.");
        S.runLeft = 0;
    } else if (what == "speed") {
        if (n >= -3 && n <= 5) {
            S.wantSpeed = n;
            ChatBoth(who + " set the game speed to " + std::to_string(n) + " (from the next minute).");
        }
    } else if (what == "end") {
        S.endWant[side] = !S.endWant[side];
        if (S.endWant[0] && S.endWant[1])
            ChatBoth("Both players agreed to end the battle.");
        else if (S.endWant[side])
            ChatBoth(who + " wants to end the battle. Press End to agree.");
        else
            ChatBoth(who + " no longer wants to end the battle.");
    } else if (what == "resync") {
        RequestResync(who + " reported " + (why.empty() ? std::string("a clock difference") : why));
    }
    SendInfo();
    ATryRelease();
}

static void ClockRequest(const std::string& what, int n) {
    if (!S.on || S.st == St::Ended) return;
    if (S.isA) {
        AHandleRequest(0, what, n);
        return;
    }
    if (S.st != St::Running) {
        Chat("The live battle has not started yet.");
        return;
    }
    KV kv;
    kv["t"] = "req";
    kv["what"] = what;
    kv["n"] = std::to_string(n);
    SendMsg(kv);
}

// A: snapshot the battle at this boundary, send it, and load it here too.
static bool StartTransfer(bool resync, std::string& err) {
    if (!BattleForm()) {
        err = "no battle";
        return false;
    }
    int speed = S.wantSpeed != INT_MIN ? S.wantSpeed : CurSpeed();
    S.wantSpeed = INT_MIN;
    if (S.st == St::Running) RestoreAgreed();  // pending orders are dropped with a new snapshot
    ClearOrders();
    S.epoch++;
    S.seed = NewSeed();
    S.endWant[0] = S.endWant[1] = false;
    S.proposal = false;
    S.peerDoneN = S.verifiedN = S.myHashN = -1;
    S.peerReady = false;
    S.peerReadyHash.clear();
    S.mySeq = S.peerSeq = 0;
    if (!SaveSnapshot(err)) return false;
    int slot = game::CurrentSlot();
    Bundle b;
    if (!BuildBundle(slot, b, err)) return false;
    std::string body = b.Pack();
    KV h;
    h["t"] = "start";
    h["rs"] = resync ? "1" : "0";
    h["seed"] = std::to_string(S.seed);
    h["speed"] = std::to_string(speed);
    h["prefs"] = PrefsHex();
    h["gd"] = W2U(GameDir());
    h["cw"] = std::to_string(S.cw0);
    h["s0"] = S.sideNames[0];
    h["s1"] = S.sideNames[1];
    h["from"] = S.myName;
    h["opp"] = std::to_string(S.opp);
    S.st = St::Loading;
    S.loadStart = GetTickCount();
    S.lastPeerMsg = GetTickCount();
    SendMsg(h, body);
    Log("lockstep: battle %d epoch %d sent (%u files, %u bytes, seed %u, speed %d)", S.bid, S.epoch,
        (unsigned)b.files.size(), (unsigned)body.size(), S.seed, speed);
    if (!LoadSnapshot(slot, S.seed, speed, !resync && !S.everRunning, err)) return false;
    S.st = St::WaitReady;
    CanonBegin();
    S.myReadyHash = HashNow();
    CanonEnd();
    S.lastPeerMsg = GetTickCount();
    if (S.peerAI) S.peerReady = true;
    ACheckReady();
    return true;
}

static void ACheckReady() {
    if (!S.isA || S.st != St::WaitReady || (!S.peerReady && !S.peerAI)) return;
    if (!S.peerAI && S.peerReadyHash != S.myReadyHash) {
        Log("lockstep: the loaded battles differ: here %s, %s %s", S.myReadyHash.c_str(), S.peerName.c_str(),
            S.peerReadyHash.c_str());
        RequestResync("the loaded battles differ");
        return;
    }
    KV kv;
    kv["t"] = "go";
    kv["n"] = std::to_string(S.boundary);
    SendMsg(kv);
    bool first = !S.everRunning;
    S.st = St::Running;
    S.everRunning = true;
    S.peerDoneN = S.verifiedN = S.myHashN = S.boundary;
    S.myHash = S.myReadyHash;
    S.waitSince = 0;
    Log("lockstep: battle %d epoch %d running at minute %d", S.bid, S.epoch, S.boundary);
    if (first)
        ChatBoth("The live battle starts: " + Who(0) + " against " + Who(1) +
                 ". Run, Pause, the speed and End act for both players; both players' orders apply from the next minute.");
    else
        ChatBoth("The battle is synchronised again (minute " + std::to_string(S.boundary) + ").");
}

// ---- B ----

static void TryLoadQueued() {
    if (!S.loadQueued || S.inAction || S.minuteRunning || g_loading) return;
    std::string wait;
    if (!CleanStack(&wait)) return;
    if (S.firstLoad && g_hooks.loadReady && !g_hooks.loadReady()) return;  // a dialog or event of the player's game
    S.loadQueued = false;
    std::string err, body;
    body.swap(S.pendingStart);
    Bundle b;
    if (!b.Unpack(body))
        err = "damaged battle files";
    else if (WriteBattleFiles(b, S.slot, err)) {
        FixFlagFiles(S.slot, S.gameDirA);
        if (!S.prefsA.empty()) SetPrefsHex(S.prefsA);
        LoadSnapshot(S.slot, S.seed, S.startSpeed, S.firstLoad, err);
    }
    if (!err.empty()) {
        KV kv;
        kv["t"] = "abort";
        kv["why"] = "could not load the battle: " + err;
        SendMsg(kv);
        AbortB("the battle could not be loaded: " + err);
        return;
    }
    S.firstLoad = false;
    S.st = St::WaitReady;
    KV kv;
    kv["t"] = "ready";
    CanonBegin();
    kv["h"] = HashNow();
    CanonEnd();
    kv["n"] = std::to_string(S.boundary);
    SendMsg(kv);
}

static void BOnStart(const KV& h, std::string& body, SendFn replyTo) {
    int bid = KVInt(h, "bid"), epoch = KVInt(h, "epoch");
    bool resync = KVInt(h, "rs") != 0;
    auto refuse = [&](const std::string& why) {
        KV kv;
        kv["t"] = "abort";
        kv["bid"] = std::to_string(bid);
        kv["epoch"] = std::to_string(epoch);
        kv["why"] = why;
        Log("lockstep: battle %d refused: %s", bid, why.c_str());
        if (replyTo) replyTo(PackHB(KVEncode(kv), ""));
    };
    if (S.on && (S.isA || S.bid != bid)) {
        refuse("busy with another battle");
        return;
    }
    if (S.on && epoch <= S.epoch) return;  // stale
    if (!S.on) {
        if (resync) {
            refuse("this PC is not in that battle");
            return;
        }
        std::string why;
        if (!Resolve() || !E.sessionOk || !EnsureHooks())
            why = "the live battle engine is not available on this PC";
        else if (g_hooks.canLoad && !g_hooks.canLoad(why)) {
        }
        int slot = g_hooks.battleSlot ? g_hooks.battleSlot() : -1;
        if (why.empty() && slot < 50) why = "no battle slot";
        if (!why.empty()) {
            refuse(why);
            return;
        }
        S = Session();
        S.on = true;
        S.isA = false;
        S.mySide = 1;
        S.bid = bid;
        S.slot = slot;
        S.send = replyTo;
        if (!g_cwSaved) g_cwSaved = FpuCw();  // this PC's own x87 control word (A's is used during the battle)
        S.peerName = KVStr(h, "from", "the other player");
        S.sideNames[0] = KVStr(h, "s0");
        S.sideNames[1] = KVStr(h, "s1");
        S.savedPrefs = PrefsHex();
        S.firstLoad = true;
        if (g_hooks.slotMode) g_hooks.slotMode(true);
        Chat(S.peerName + " starts a live battle: " + Who(0) + " against " + Who(1) + ". You command " + Who(1) +
             ". Loading...");
        if (g_hooks.notice) g_hooks.notice("Live battle: you command " + Who(1));
    } else {
        Log("lockstep: battle %d resync (epoch %d)", bid, epoch);
    }
    S.epoch = epoch;
    S.seed = (uint32_t)strtoul(KVStr(h, "seed", "1").c_str(), nullptr, 10);
    S.startSpeed = KVInt(h, "speed", 1);
    S.prefsA = KVStr(h, "prefs");
    S.gameDirA = KVStr(h, "gd");
    S.cw0 = (unsigned)KVInt(h, "cw");
    S.pendingStart.swap(body);
    S.loadQueued = true;
    S.st = St::Loading;
    S.loadStart = S.lastPeerMsg = GetTickCount();
    S.hasPendingRel = false;
    S.atBoundary = false;
    ClearOrders();
    S.mySeq = S.peerSeq = 0;
    // The load frees the battle screen: it runs from Tick, at the outermost message loop (never from inside the
    // handling of this message, which a nested loop may be dispatching).
    Log("lockstep: battle %d epoch %d queued for loading", bid, epoch);
}

static void HandleMessage(const KV& h, std::string& body, SendFn replyTo) {
    std::string t = KVStr(h, "t");
    int bid = KVInt(h, "bid"), epoch = KVInt(h, "epoch");
    if (t == "start") {
        if (S.on && S.isA) return;
        BOnStart(h, body, replyTo);
        return;
    }
    if (!S.on || bid != S.bid) {
        if (t != "hb") Log("lockstep: '%s' for battle %d ignored (this PC: %d, %s)", t.c_str(), bid, S.bid, S.on ? "on" : "off");
        return;
    }
    S.lastPeerMsg = GetTickCount();
    if (t == "hb") {
        S.peerBusy = KVInt(h, "busy") != 0;
        return;
    }
    if (t == "peerlost") {
        OnPeerLost(KVStr(h, "why", "the other player disconnected"));
        return;
    }
    if (t == "abort" || t == "drop") {
        std::string why = KVStr(h, "why", t);
        if (S.isA)
            DropPeer(S.peerName + ": " + why);
        else
            AbortB(t == "drop" ? "the AI commands your side now (" + why + ")" : why);
        return;
    }
    if (t == "fin") {
        if (!S.isA && S.st != St::Ended) {
            S.finSince = GetTickCount();
            Log("lockstep: %s ended the battle (%s)", S.peerName.c_str(), KVStr(h, "why").c_str());
        }
        return;
    }
    if (epoch != S.epoch) {
        Log("lockstep: '%s' of epoch %d ignored (now %d)", t.c_str(), epoch, S.epoch);
        return;
    }
    if (t == "batch") {
        int seq = KVInt(h, "seq"), side = KVInt(h, "side", -1);
        if (side != 1 - S.mySide || (S.isA && S.peerAI)) return;
        Batch b;
        b.seq = seq;
        b.items = Split(body, '\x1e');
        S.peerBatches[seq] = b;
        if (seq > S.peerSeq) S.peerSeq = seq;
        Log("lockstep: orders #%d of %s received (%d items)", seq, S.peerName.c_str(), (int)b.items.size());
        return;
    }
    if (S.isA) {
        if (t == "ready" && S.st == St::WaitReady) {
            S.peerReady = true;
            S.peerReadyHash = KVStr(h, "h");
            ACheckReady();
        } else if (t == "done") {
            S.peerDoneN = KVInt(h, "n", -1);
            S.peerHash = KVStr(h, "h");
            S.peerBusy = false;
            if (KVInt(h, "end") && !S.peerEnded) {  // B's battle is over: it stops talking (no timeouts from here)
                S.peerEnded = true;
                S.peerEndN = S.peerDoneN;
                Log("lockstep: %s's battle ended at minute %d", S.peerName.c_str(), S.peerEndN);
            }
            ACheck();
            ATryRelease();
        } else if (t == "req") {
            AHandleRequest(1, KVStr(h, "what"), KVInt(h, "n"), KVStr(h, "why"));
        }
        return;
    }
    // B
    if (t == "go") {
        if (S.st != St::WaitReady) return;
        S.st = St::Running;
        S.boundary = Minute();
        if (KVInt(h, "n", -1) != S.boundary) Log("lockstep: go at minute %s, here %d", KVStr(h, "n").c_str(), S.boundary);
        Log("lockstep: battle %d epoch %d running at minute %d", S.bid, S.epoch, S.boundary);
    } else if (t == "rel") {
        if (S.st != St::Running) return;
        S.runLeft = KVInt(h, "left");
        if (game::ModalOpen() || S.minuteRunning || S.inAction || g_nFrames > 0 || !S.atBoundary) {
            S.pendingRel = h;
            S.hasPendingRel = true;
            return;
        }
        ApplyRelease(h);
    } else if (t == "info") {
        S.runLeft = KVInt(h, "left");
        S.endWant[0] = KVInt(h, "e0") != 0;
        S.endWant[1] = KVInt(h, "e1") != 0;
        std::string msg = KVStr(h, "msg");
        if (!msg.empty()) Chat(msg);
    }
}

// ---------------------------------------------------------------------------------------------
// The session API

void SetHooks(const Hooks& h) {
    g_hooks = h;
}

bool Available() { return Resolve() && E.sessionOk; }
bool Prepare() { return Available(); }
bool Active() { return S.on && S.st != St::Ended; }
bool Busy() { return (S.on && S.st != St::Ended) || S.leavePending; }
bool IsAuthority() { return S.on && S.isA; }
int BattleId() { return S.on ? S.bid : -1; }
bool IsHolding() { return S.on && S.isA && S.st == St::Holding; }

void Hold(const std::string& why, int bid) {
    if (S.on && S.st != St::Ended) return;
    if (!Available() || !EnsureHooks()) return;
    S = Session();
    S.on = true;
    S.isA = true;
    S.mySide = 0;
    S.bid = bid;
    S.st = St::Holding;
    S.why = why;
    S.lastPeerMsg = GetTickCount();
    Log("lockstep: battle held at its start (%s)", why.c_str());
    Chat("Live battle: " + why);
}

void ReleaseHold(const std::string& why) {
    if (!S.on || !S.isA || S.st != St::Holding) return;
    S.on = false;
    S.st = St::Idle;
    Log("lockstep: hold released (%s)", why.c_str());
    Chat("The live battle could not start (" + why + "); the battle goes on against the AI.");
}

bool StartAsA(const StartInfo& info, std::string& err, bool& retry) {
    retry = false;
    if (!S.on || !S.isA || S.st != St::Holding) {
        err = "not held";
        return false;
    }
    if (!BattleVisible()) {
        err = "no battle screen";
        ReleaseHold(err);
        return false;
    }
    std::string wait;
    if (game::InTurnProcessing() || !CleanStack(&wait)) {
        retry = true;
        err = wait.empty() ? "busy" : wait;
        return false;
    }
    if (info.bid != S.bid) Log("lockstep: battle id %d (held as %d)", info.bid, S.bid);
    S.bid = info.bid;
    S.opp = info.opp;
    S.myName = info.myName;
    S.peerName = info.peerName;
    S.sideNames[0] = info.sideNames[0];
    S.sideNames[1] = info.sideNames[1];
    S.send = info.send;
    S.cw0 = FpuCw();
    S.everRunning = false;
    if (info.slot >= 0 && info.slot != game::CurrentSlot()) Log("lockstep: battle slot %d, open slot %d", info.slot, game::CurrentSlot());
    Chat("Live battle against " + S.peerName + " (" + Who(1) + "): sending the battle...");
    if (StartTransfer(false, err)) return true;
    // Nothing reached B or the load here failed.
    if (!BattleVisible()) {  // the battle screen was freed for the load: reopen the battle save without lockstep
        RecoverAlone(err);
        return false;
    }
    KV kv;
    kv["t"] = "abort";
    kv["why"] = err;
    SendMsg(kv);
    S.peerAI = true;
    if (S.st == St::WaitReady || S.st == St::Running) {
        SharedBegin();
        TryRun([] { SetSideControl(1, true, false); });
        SharedEnd();
    }
    Cleanup();
    S.on = false;
    S.st = St::Ended;
    Chat("The live battle could not start (" + err + "); the battle goes on against the AI.");
    if (g_hooks.onEnded) g_hooks.onEnded(false, err);
    return false;
}

static void NoteDispatchInSim(const char* what);

void OnMessage(const std::string& payload, SendFn replyTo) {
    PruneStale();
    NoteDispatchInSim("a network message");
    if (g_muted) return;
    KV h;
    std::string body;
    if (!UnpackHB(payload, h, body)) return;
    try {
        HandleMessage(h, body, replyTo);
    } catch (...) {
        Log("lockstep: exception handling '%s'", KVStr(h, "t").c_str());
    }
}

int MessageBattleId(const std::string& payload) {
    KV h;
    std::string body;
    if (!UnpackHB(payload, h, body)) return -1;
    return KVInt(h, "bid", -1);
}

std::string PeerLostMessage(int bid, const std::string& why) {
    KV kv;
    kv["t"] = "peerlost";
    kv["bid"] = std::to_string(bid);
    kv["why"] = why;
    return PackHB(KVEncode(kv), "");
}

void OnPeerLost(const std::string& why) {
    if (!S.on || S.st == St::Ended) return;
    if (S.isA)
        DropPeer(why);
    else
        AbortB(why);
}

void PruneStale() { PruneBelow((uint32_t)(uintptr_t)_AddressOfReturnAddress(), false, "dispatcher"); }

// Diagnostics: a message of the mod (a tick, a network message, a bridge command) handled while a simulation step is on
// the stack (the game runs a message loop inside it): the player's edits there would bypass the order guard.
static void NoteDispatchInSim(const char* what) {
    if (!S.on || g_sim == 0 || g_manual > 0 || g_nFrames == 0 || g_results > 0) return;
    static DWORD last = 0;
    if (GetTickCount() - last < 2000) return;
    last = GetTickCount();
    Log("lockstep: WARNING %s handled inside a simulation step (%s, %d frames, t=%s)", what,
        FrameName(g_frames[g_nFrames - 1]), g_nFrames, ClockText().c_str());
}

bool BattleLost() { return S.battleLost; }

// A minute that stopped before its end: no interval timer, no minute about to start, no hooked routine or dialog on the
// stack. An exception the game caught (its own "Error in ..." handlers) or a minute the game refused leaves this state.
static void Watchdog(DWORD now) {
    void* m = BattleForm();
    if (!S.minuteRunning || S.inAction || g_loading || !m) {
        S.stalledSince = 0;
        return;
    }
    bool starting = TimerOn(m, E.fRunTimer) && dl::At<uint8_t>(m, E.fFinished) && dl::At<int>(m, E.fMinutesLeft) > 0;
    bool stalled = !TimerOn(m, E.fMoveTimer) && !starting && !game::ModalOpen() && g_results == 0 && g_nFrames == 0;
    if (!stalled) {
        S.stalledSince = 0;
        return;
    }
    if (!S.stalledSince) S.stalledSince = now;
    DWORD limit = S.minuteBroken ? 1000 : 10000;
    if (now - S.stalledSince < limit) return;
    MinuteFailed(S.minuteBroken ? "an exception interrupted it" : "the battle's timers stopped");
}

void Tick() {
    if (S.battleLost && BattleVisible()) S.battleLost = false;  // the player loaded the battle again
    if (g_closeAfterEnd && !g_closePosted && !S.on) {  // the live battle ended: the deferred "Exit battle"
        g_closePosted = true;
        RunOnMainThread(CloseAfterEnd);
    }
    if (!S.on && !S.leavePending) return;
    PruneStale();
    NoteDispatchInSim("a tick");
    DWORD now = GetTickCount();
    if (S.leavePending) {
        TryLeave();
        return;
    }
    if (S.st == St::Ended || S.st == St::Holding) return;
    if (g_muted) S.lastPeerMsg = now;  // tests: a muted PC doesn't time the other one out by itself
    if (now - S.lastHbSent > g_tHb) {
        S.lastHbSent = now;
        KV kv;
        kv["t"] = "hb";
        kv["busy"] = game::ModalOpen() || S.hasPendingRel ? "1" : "0";
        kv["st"] = StName(S.st);
        SendMsg(kv);
    }
    static DWORD lastDetect = 0;
    if (S.st == St::Running && !S.minuteRunning && !S.inAction && g_nFrames == 0 && now - lastDetect > 250) {
        lastDetect = now;
        DetectEdits("paused");
    }
    Watchdog(now);
    if (!S.on || S.st == St::Ended) return;
    void* m = BattleForm();
    // The battle's end is final: its result screens take as long as the player wants (no timeouts from here).
    bool over = g_results > 0 || (m && dl::At<uint8_t>(m, E.fEndGame));
    if (!S.isA) {
        if (S.loadQueued) {  // (a load takes seconds: the timeouts are checked on the next tick)
            if ((int)(now - S.loadStart) > (int)(g_tLoad + 5000)) {  // A gave up by now
                KV kv;
                kv["t"] = "abort";
                kv["why"] = "the battle could not be loaded in time";
                SendMsg(kv);
                AbortB("the battle could not be loaded within " + std::to_string(g_tLoad / 1000) + " s");
                return;
            }
            TryLoadQueued();
            return;
        }
        if (S.hasPendingRel && S.st == St::Running && S.atBoundary && !game::ModalOpen() && !S.minuteRunning &&
            !S.inAction && g_nFrames == 0) {
            S.hasPendingRel = false;
            ApplyRelease(S.pendingRel);
            return;
        }
        if (!S.on) return;
        now = GetTickCount();
        if ((S.st == St::Running || S.st == St::WaitReady) && !g_loading && !BattleVisible()) {
            KV kv;
            kv["t"] = "abort";
            kv["why"] = "the battle screen was closed";
            SendMsg(kv);
            AbortB("the battle screen was closed");
        } else if (over) {
        } else if (S.st != St::Loading && (int)(now - S.lastPeerMsg) > (int)g_tLost)
            AbortB("no contact with " + S.peerName + " for " + std::to_string(g_tLost / 1000) + " s");
        else if (S.finSince && (int)(now - S.finSince) > 30000)
            AbortB(S.peerName + " ended the battle");
        return;
    }
    // A
    if ((S.st == St::Running || S.st == St::WaitReady) && !g_loading && !BattleVisible() && !S.inAction) {
        Ended(false, "the battle screen was closed");
        return;
    }
    if (g_resyncPending && !S.minuteRunning && !S.inAction && !g_loading) {
        std::string wait;
        if (CleanStack(&wait)) {
            DoResync();  // (seconds: the timeouts are checked on the next tick)
            return;
        }
    }
    if (!S.on || S.st == St::Ended) return;
    now = GetTickCount();
    if (S.dropPending && (!S.minuteRunning || S.minuteBroken) && !S.inAction && !g_loading) {
        DoDrop();
        return;
    }
    if (S.peerAI || over) return;
    if (S.peerEnded) {  // B's battle ended at peerEndN; this PC's didn't (or it isn't there yet)
        if (S.st == St::Running && S.atBoundary && S.boundary >= S.peerEndN)
            DropPeer(S.peerName + "'s battle ended at minute " + std::to_string(S.peerEndN) + ", this one did not");
        return;
    }
    bool loadingThere = S.st == St::WaitReady && !S.peerReady;  // B loads without sending (g_tLoad applies)
    if (!loadingThere && (int)(now - S.lastPeerMsg) > (int)g_tLost) {
        DropPeer("no answer for " + std::to_string(g_tLost / 1000) + " s");
        return;
    }
    if (S.st == St::WaitReady && !S.peerReady && (int)(now - S.loadStart) > (int)g_tLoad) {
        DropPeer("the battle was not loaded within " + std::to_string(g_tLoad / 1000) + " s");
        return;
    }
    if (S.st == St::Running) {
        bool wantRun = S.runLeft > 0 || (S.endWant[0] && S.endWant[1]);
        bool waiting = wantRun && !S.minuteRunning && !(S.peerDoneN == S.boundary);
        if (!waiting) {
            S.waitSince = 0;
        } else {
            if (!S.waitSince) S.waitSince = now;
            DWORD limit = S.peerBusy ? g_tBusyCap : g_tBoundary;
            if ((int)(now - S.waitSince) > (int)limit) {
                DropPeer("minute " + std::to_string(S.boundary) + " was not finished within " +
                         std::to_string(limit / 1000) + " s");
                return;
            }
        }
        ATryRelease();
    }
}

std::string Status() {
    char b[760];
    void* m = BattleForm();
    snprintf(b, sizeof(b),
             "ok on=%d role=%s st=%s bid=%d epoch=%d side=%d minute=%d t=%s boundary=%d running=%d left=%d speed=%d "
             "peer=%s peerAI=%d muted=%d seq=%d/%d pending=%d/%d overlay=%d verified=%d peerDone=%d resyncs=%d ok=%d "
             "end=%d/%d proposal=%d busy=%d leave=%d atb=%d broken=%d failed=%d peerEnded=%d lost=%d frames=%d "
             "disp=%s hash=%s",
             S.on ? 1 : 0, S.isA ? "A" : "B", StName(S.st), S.bid, S.epoch, S.mySide, Minute(), ClockText().c_str(),
             S.boundary, S.minuteRunning ? 1 : 0, S.runLeft, m ? dl::At<int>(m, E.fGameSpeed) : -99,
             S.peerName.empty() ? "-" : S.peerName.c_str(), S.peerAI ? 1 : 0, g_muted ? 1 : 0, S.mySeq, S.peerSeq,
             (int)S.myBatches.size(), (int)S.peerBatches.size(), (int)g_overlay.size(), S.verifiedN, S.peerDoneN,
             S.resyncs, S.okMinutes, S.endWant[0] ? 1 : 0, S.endWant[1] ? 1 : 0, S.proposal ? 1 : 0, Busy() ? 1 : 0,
             S.leavePending ? 1 : 0, S.atBoundary ? 1 : 0, S.minuteBroken ? 1 : 0, S.brokenMinutes, S.peerEnded ? 1 : 0,
             S.battleLost ? 1 : 0, g_nFrames, DispatchDepthText().c_str(), S.myHash.empty() ? "-" : S.myHash.c_str());
    return b;
}

bool DebugIsAsync(const std::vector<std::string>& a) {
    std::string c = a.size() > 1 ? a[1] : "";
    return c == "resync" || c == "detach" || c == "nest";
}

std::string Debug(const std::vector<std::string>& a) {
    auto arg = [&](size_t i, const std::string& def = "") { return i < a.size() ? a[i] : def; };
    std::string c = arg(1, "status");
    if (c == "status") return Status();
    if (c == "resolve") return Available() ? "ok" : "error " + Missing();
    if (c == "hash") {
        if (!BattleForm()) return "error no battle";
        CanonBegin();
        std::string h = HashLine("ls hash");
        CanonEnd();
        return "ok " + h;
    }
    if (c == "timeout") {  // ls timeout <boundary s> [lost s] [load s] [heartbeat s]
        if (a.size() > 2) g_tBoundary = (DWORD)atoi(arg(2).c_str()) * 1000;
        if (a.size() > 3) g_tLost = (DWORD)atoi(arg(3).c_str()) * 1000;
        if (a.size() > 4) g_tLoad = (DWORD)atoi(arg(4).c_str()) * 1000;
        if (a.size() > 5) g_tHb = (DWORD)atoi(arg(5).c_str()) * 1000;
        return "ok boundary=" + std::to_string(g_tBoundary / 1000) + " lost=" + std::to_string(g_tLost / 1000) +
               " load=" + std::to_string(g_tLoad / 1000) + " hb=" + std::to_string(g_tHb / 1000);
    }
    if (c == "mute") {  // ls mute 0|1: this PC neither sends nor receives lockstep messages (a silent player)
        g_muted = arg(2, "1") != "0";
        Log("lockstep: TEST mute %d", g_muted ? 1 : 0);
        return "ok muted=" + std::to_string(g_muted ? 1 : 0);
    }
    if (c == "leakcheck") {  // ls leakcheck 0|1: the simulation state may not change between its steps (logs LEAK)
        g_leakCheck = arg(2, "1") != "0";
        g_exitHashOk = false;
        g_leaks = 0;
        return "ok leakcheck=" + std::to_string(g_leakCheck ? 1 : 0);
    }
    if (c == "trace") {  // ls trace 0|1: per-minute files of the shared draws' call sites (RTW3MP data folder)
        g_trace = arg(2, "1") != "0";
        return "ok trace=" + std::to_string(g_trace ? 1 : 0);
    }
    if (c == "view") {  // ls view 0|1: the display swap to this PC's side (from the next load)
        g_noView = arg(2, "1") == "0";
        return "ok view=" + std::to_string(g_noView ? 0 : 1);
    }
    if (c == "nest") {  // ls nest <s>: a nested message loop on the main thread for s seconds (as ProcessMessages)
        int sec = std::max(1, std::min(60, atoi(arg(2, "5").c_str())));
        Log("lockstep: TEST nested message loop for %d s (dispatcher %s)", sec, DispatchDepthText().c_str());
        DWORD t0 = GetTickCount();
        MSG msg;
        while (GetTickCount() - t0 < (DWORD)sec * 1000) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    PostQuitMessage((int)msg.wParam);
                    return "ok quit";
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        }
        Log("lockstep: TEST nested message loop done");
        return "ok";
    }
    if (!S.on) return "error no live battle";
    NoteDispatchInSim(("bridge `ls " + c + "`").c_str());
    if (c == "run") {  // ls run [1|5|until|<n>]
        std::string w = arg(2, "1");
        int n = w == "until" ? 0x423a35cb : w == "5" ? 5 : atoi(w.c_str());
        ClockRequest("run", n <= 0 ? 1 : n);
        return "ok";
    }
    if (c == "pause") return ClockRequest("pause", 0), "ok";
    if (c == "speed") return ClockRequest("speed", atoi(arg(2, "1").c_str())), "ok";
    if (c == "end") return ClockRequest("end", 0), "ok";
    if (c == "desync") return S.forceDesync = 1, "ok (this PC's next minute differs)";
    if (c == "endsoon") {  // ls endsoon (A): the game itself ends the battle inside the next released minute
        if (!S.isA) return "error A only";
        if (!E.scenLenVar) return "error scenario length not found";
        S.endSoon = true;
        return "ok length=" + std::to_string(*E.scenLenVar) + " minute=" + std::to_string(Minute());
    }
    if (c == "throw") return g_testThrow = true, "ok (an exception in this PC's next MoveAllShips)";
    if (c == "guard") {  // ls guard 0|1: the sighting fields' display/simulation sets (tests: 0 = the old leak)
        g_guardOn = arg(2, "1") != "0";
        return "ok guard=" + std::to_string(g_guardOn ? 1 : 0);
    }
    if (c == "dispwrite") {  // ls dispwrite: change the sighting fields of every ship here, outside the simulation
        if (g_sim > 0 || S.minuteRunning) return "error not between minutes";
        int n = 0;
        void* ships = List(E.allShipsVar);
        for (int i = 0; i < dl::ListCount(ships); i++)
            if (void* sp = dl::ListItem(ships, i))
                for (auto& f : g_guard)
                    if (f.off >= 0 && f.size == 4) GuardWrite(sp, f, GuardRead(sp, f) + 1), n++;
        Log("lockstep: TEST %d sighting fields changed outside the simulation (as the display does)", n);
        return "ok " + std::to_string(n);
    }
    if (c == "stall") return g_testStall = true, "ok (this PC's next released minute does not start)";
    if (c == "drop") {  // A: hand side 1 to the AI now
        if (!S.isA) return "error A only";
        DropPeer("test");
        return "ok";
    }
    if (c == "abort") {  // B: leave the battle
        if (S.isA) return "error B only";
        AbortB("test");
        return "ok";
    }
    if (c == "resync") {  // A: send the battle again now
        if (!S.isA) return "error A only";
        RequestResync("test");
        return "ok";
    }
    if (c == "order") {  // ls order <division index> <course> <speed> [ai]: as the course/speed boxes write them
        if (!SetDivisionOrder(atoi(arg(2, "-1").c_str()), atoi(arg(3, "-1").c_str()), atoi(arg(4, "-1").c_str()),
                              a.size() > 5 ? atoi(arg(5).c_str()) : -1))
            return "error division";
        return "ok";
    }
    if (c == "detach") {  // ls detach <division index> <ship index>: as the battle screen's Detach (between minutes)
        void* m = BattleForm();
        void* dv = dl::ListItem(List(E.divisionsVar), atoi(arg(2, "-1").c_str()));
        void* ship = dv ? dl::ListItem(dl::At<void*>(dv, E.fShips), atoi(arg(3, "0").c_str())) : nullptr;
        if (!m || !ship) return "error division/ship";
        if (S.minuteRunning || S.st != St::Running) return "error not between minutes";
        bool ok;
        void* nd = (void*)(uintptr_t)SafeCall(E.mDetachShip, U(m), U(dv), U(ship), 0, nullptr, &ok);
        RefreshView();
        return ok && nd ? "ok " + W2U(ObjName(nd)) : "error detach";
    }
    if (c == "divs") {  // ls divs: the divisions with their index, side and orders
        std::string out = "ok";
        void* divs = List(E.divisionsVar);
        int fc = dl::Field("TDivision", "DivisionCourse"), fs = dl::Field("TDivision", "DivisionSpeed");
        for (int i = 0; i < dl::ListCount(divs); i++)
            if (void* dv = dl::ListItem(divs, i))
                out += " " + std::to_string(i) + ":" + std::to_string(dl::At<int>(dv, E.fDivSide)) + ":" +
                       W2U(ObjName(dv)) + ":" + std::to_string(dl::At<int>(dv, fc)) + "/" +
                       std::to_string(dl::At<int>(dv, fs)) + ":" + std::to_string(dl::ListCount(dl::At<void*>(dv, E.fShips))) +
                       (dl::At<uint8_t>(dv, E.fDAI) ? ":ai" : "") + ";";
        return out;
    }
    return "error unknown ls command";
}

// ---------------------------------------------------------------------------------------------
// Engine helpers used by the POC commands (poc_battle.cpp)

static void DumpFields(std::string& out, const char* tag, void* obj, const FieldSpec* fs, size_t n) {
    out += tag;
    for (size_t i = 0; i < n; i++) {
        const FieldSpec& f = fs[i];
        if (f.off < 0) continue;
        uint8_t* p = (uint8_t*)obj + f.off;
        out += std::string(" ") + f.name + "=";
        if (f.size == -1)
            out += W2U(dl::ReadUStr(*(void**)p));
        else if (f.size == -2)
            out += W2U(ObjName(*(void**)p));
        else
            out += Hex(p, f.size);
    }
    out += "\r\n";
}

bool DumpState(const std::wstring& path) {
    if (!BattleForm()) return false;
    ResolveSpecs();
    std::string out = "t=" + ClockText() + "\r\n";
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* dv = dl::ListItem(divs, i);
        if (!dv) continue;
        DumpFields(out, ("D" + std::to_string(i) + " " + W2U(ObjName(dv))).c_str(), dv, g_divFields,
                   sizeof(g_divFields) / sizeof(g_divFields[0]));
        void* ships = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(ships); j++)
            if (void* s = dl::ListItem(ships, j))
                DumpFields(out, ("  S" + std::to_string(i) + "." + std::to_string(j) + " " + W2U(ObjName(s))).c_str(), s,
                           g_shipFields, sizeof(g_shipFields) / sizeof(g_shipFields[0]));
    }
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* af = dl::ListItem(air, i))
            DumpFields(out, ("A" + std::to_string(i)).c_str(), af, g_airFields, sizeof(g_airFields) / sizeof(g_airFields[0]));
    return WriteFileBytes(path, out);
}

std::string StateText() {
    void* m = Frm();
    char b[760];
    if (!m) return "ok frm=0";
    HWND h = game::FormHwnd(m);
    snprintf(b, sizeof(b),
             "ok running=%d moving=%d frm=1 visible=%d t=%s min=%d left=%d finished=%d pause=%d autorun=%d endgame=%d "
             "engaged=%d speed=%d intervals=%d ivsec=%d playerSide=%d forces=%d divs=%d allships=%d af=%d torps=%d "
             "draws=%llu/%llu/%llu gate=%d/%d ls=%s",
             TimerOn(m, E.fRunTimer) ? 1 : 0, TimerOn(m, E.fMoveTimer) ? 1 : 0, h && IsWindowVisible(h) ? 1 : 0,
             ClockText().c_str(), Minute(), dl::At<int>(m, E.fMinutesLeft), dl::At<uint8_t>(m, E.fFinished),
             dl::At<uint8_t>(m, E.fPause), E.fAutoRun >= 0 ? dl::At<uint8_t>(m, E.fAutoRun) : -1,
             dl::At<uint8_t>(m, E.fEndGame), E.fEngaged >= 0 ? dl::At<uint8_t>(m, E.fEngaged) : -1,
             dl::At<int>(m, E.fGameSpeed), E.fIntervals >= 0 ? dl::At<int>(m, E.fIntervals) : -1,
             E.intervalSecVar ? *E.intervalSecVar : -1, E.playerSideVar ? *E.playerSideVar : -1,
             dl::ListCount(List(E.forcesVar)), dl::ListCount(List(E.divisionsVar)), dl::ListCount(List(E.allShipsVar)),
             dl::ListCount(List(E.airVar)), dl::ListCount(List(E.torpVar)), (unsigned long long)g_draws[0],
             (unsigned long long)g_draws[1], (unsigned long long)g_draws[2], g_pocGate ? 1 : 0, g_pocTokens,
             S.on ? StName(S.st) : "off");
    return b;
}

void PocHashOn(bool on) {
    g_pocHash = on;
    g_pocLastHashClock = -1;
    g_pocHashCount = 0;
}

void PocGate(int mode, int tokens) {
    if (mode == 0) g_pocGate = false;
    if (mode == 1) g_pocGate = true, g_pocTokens = 0;
    if (mode == 2) g_pocTokens += tokens;
}

std::string PocGateState() {
    return "ok gate=" + std::to_string(g_pocGate ? 1 : 0) + " tokens=" + std::to_string(g_pocTokens) +
           " holding=" + std::to_string(g_pocGateHeld ? 1 : 0);
}

bool RunMinutes(int n) {
    void* m = BattleForm();
    if (!m) return false;
    bool own = S.ownRun, ok;
    S.ownRun = true;
    SafeCall(E.startRun, U(m), (uint32_t)n, 0, 0, nullptr, &ok);
    S.ownRun = own;
    return ok;
}

bool SetSpeed(int s) {
    void* m = BattleForm();
    if (!m) return false;
    dl::At<int>(m, E.fGameSpeed) = s;
    bool ok;
    SafeCall(E.setSpeed, U(m), 0, 0, 0, nullptr, &ok);
    return ok;
}

bool SaveSac(const std::wstring& path) {
    void* m = BattleForm();
    if (!m || !E.saveScenario) return false;
    dl::ConstUStr p(path);
    bool ok;
    DWORD t0 = GetTickCount();
    SafeCall(E.saveScenario, U(m), p.ptr(), 0, 0, nullptr, &ok);
    Log("pocsac: %s written in %lu ms", W2U(path).c_str(), GetTickCount() - t0);
    return ok;
}

bool SetPlayerSide(int side) {
    void* m = BattleForm();
    if (!m || !E.playerSideVar) return false;
    *E.playerSideVar = side;
    RefreshView();
    return true;
}

bool SetDivisionOrder(int idx, int course, int speed, int ai) {
    void* dv = dl::ListItem(List(E.divisionsVar), idx);
    if (!dv) return false;
    if (course >= 0) dl::At<int>(dv, dl::Field("TDivision", "DivisionCourse")) = course % 360;
    if (speed >= 0) dl::At<int>(dv, dl::Field("TDivision", "DivisionSpeed")) = speed;
    if (ai >= 0) dl::At<uint8_t>(dv, E.fDAI) = (uint8_t)ai;
    Log("division order: %s course %d speed %d ai %d at t=%s", W2U(ObjName(dv)).c_str(), course, speed, ai,
        ClockText().c_str());
    return true;
}




}  // namespace lockstep
