// Live player-vs-player tactical battles in lockstep (see lockstep.h and the battle investigation of 2026-10-14).
//
// Both PCs run the same battle: the authority A (side 0) snapshots the battle (the game's own battle save .sac + the
// campaign with ScenarioInProgress), sends it to B (side 1), and both load it with the same RNG seed. From then on only
// orders and clock decisions travel:
//   - the clock belongs to A: every battle minute is released by A ("rel") and runs on both PCs through the game's own
//     Run-one-minute routine; minute starts that A did not release are blocked;
//   - orders are field values of the divisions/ships/forces of the player's own side (and the side's land-CAP request);
//     a guard at every simulation step turns local edits into numbered batches, and each release says which batches both
//     apply before the minute;
//   - after every minute both PCs hash the battle state; B sends its hash with "done", A compares and resyncs from a new
//     snapshot when they differ;
//   - display code draws its random numbers from a local stream, the simulation from the shared seeded stream; the
//     battle's human side (PlayerSide) is canonical inside the simulation (0; 2 while side 0's ships are on auto-run)
//     and the player's own side in the display;
//   - a player A drops goes to the computer; the battle goes on on A and stays rejoinable: when the player is back, A
//     holds the battle at its next minute boundary and starts it again with them (Rejoin, engine level 8).
#include "lockstep.h"
#include "datafiles.h"
#include "delphi.h"
#include "game.h"
#include "saveio.h"
#include "x87.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <commctrl.h>
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
    uint32_t smpRet = 0;  // ShowMessagePos: the return address of its call of MessageDlgPosHelp
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
    void* releaseFastest = nullptr;  // TfrmMain: the "Fastest available" button up again (SetDown(False) + its glyph)
    int fFastestBtn = -1, fDownOff = -1;  // TfrmMain.sbFastestAvailable; TSpeedButton.FDown (private)
    void* setDown = nullptr;   // TSpeedButton.SetDown(Value)
    int fGroupOff = -1;        // TSpeedButton.FGroupIndex (private; 0 = not a toggle button)
    int setEnabledSlot = -1;   // TControl.SetEnabled's VMT offset
    void *mClearData = nullptr, *airCleanup = nullptr;  // the battle screen's close: TfrmMain.ClearData(2, True) + air
    void* setImageIndex = nullptr;  // TSpeedButton's ImageIndex setter (the Fastest available button's glyph)
    // Live-battle rules for the human sides (engine level 2): the routines that treat "the player's side" (PlayerSide)
    // differently from the computer's run with a simulation PlayerSide of the mod's choosing.
    void* aiAirstrike = nullptr;   // CheckAIAirstrike(Self, Side) (unnamed; StartMinute calls it for side 0, then 1)
    void* searchPlan = nullptr;    // TfrmMain.SetAIAirSearchPlan(IncludePlayerControlledShips) (EOTP: every 2 hours)
    void* missileFire = nullptr;   // CheckMissileFiringForAllShips(Self) (unnamed; EOTP after 1940)
    void* missilePanel = nullptr;  // UpdateMissilePanel(Self) (unnamed; EOTP after 1942; display only)
    int fAfId = -1, fAfSide = -1, fAfMission = -1, fAfStatus = -1, fAfConfirm = -1;  // TAirFormation
    // Force orders (engine level 3): deferred actions both PCs carry out when the next minute is released.
    void* messageDlg = nullptr;  // MessageDlg(Msg, DlgType, Buttons; HelpCtx) (the click handlers' confirmations)
    void *listIndexOf = nullptr, *listDelete = nullptr, *listAdd = nullptr;  // TList
    void *setForceLead = nullptr, *doTurnAway = nullptr, *getForceFlagship = nullptr, *mShow = nullptr;
    int fSelDiv = -1, fSelForce = -1, fSelNewLead = -1, fSelTurnAway = -1, fTag = -1;  // TfrmMain; TComponent.FTag
    int fFDivs = -1, fHasChangedLead = -1, fTimeToTurnAway = -1;                       // TForce
    int fMyForce = -1, fAdmin = -1, fLeadDiv = -1, fFLead = -1;                        // TDivision
    int fLbLeadShips = -1, lbIndexSlot = -1, fLbItems = -1, getObjectSlot = -1;  // TdlgSelectLead's list and its items
    std::wstring chforceText, chforceTail;  // Changeforce2Click's question: text + the force's name + tail
    int chforceButtons = -1;                // ... and its buttons ([mbYes, mbNo])
    // Auto-run per player (engine level 4): the battle screen's auto-run button becomes a deferred action per side.
    void** autoDlgVar = nullptr;   // dlgEventAnswer, the handler's question dialog (TdlgEventAnswer.ExecuteGeneral)
    void *autoClearHints = nullptr, *getFlagDiv = nullptr;  // TdlgEventAnswer.ClearHints, TForce.GetForceFlagDivision
    int fWithdraw = -1, fMission = -1, fAutoBtn = -1;  // TForce.ForceWithdraw, ForceMission; TfrmMain.sbAutoRunBattle
    int homeWithdraw = 0, homeMission = 0;  // the handler's "move to closest friendly base" orders (6, tfmWithdraw)
    std::wstring supportForce;              // ... also given to forces whose name contains this ('Support Force')
    // Manual torpedo salvoes (engine level 5): the torpedo dialog's call of the game's fire routine becomes a deferred
    // action. FireMount(Self, aMount, aShip; aTarget, FireAngle: Single, TorpSpeed, Range, Manual), ret 0x14.
    void *fireMount = nullptr, *isSubmerged = nullptr;  // ... and TTorpedomount.IsSubmerged (its first early return)
    int subSpeed = -1, subMtb = -1;  // the routine's rule: submerged tubes refused above this speed, unless this ship type
    int fMountList = -1, fTorpsLeft = -1, fReloadTime = -1, fReloads = -1, fMountDestroyed = -1, fTubes = -1,
        fMountType = -1, fMountPos = -1;  // Tship.TorpedomountList; TFightingTorpedoMount / TTorpedomount
    int fFiredMount = -1, fCurSpeed = -1, fShipType = -1, fSunk = -1, fSelShip = -1, fTorpMissiles = -1;
    void *torpsClick = nullptr, *torpFireClick = nullptr;  // TdlgDivisionReport.bnTorpsClick, TdlgTorpTargets.bnFireClick
    // The bridge's path through the dialogs (`ls torp`): the handlers a click calls and the list controls' virtual slots.
    void *showDivDialog = nullptr, *lbTargetsClick = nullptr, *lbMountsClick = nullptr, *cbShipsChange = nullptr;
    int lbGetIdxSlot = -1, lbSetIdxSlot = -1, fListItems = -1, itemsObjSlot = -1;
    // Routine air operations and land-based CAP (engine level 6). The routine air ops dialog's commit (unnamed, called
    // by TdlgSearch.bnSaveClick) writes nine TForce fields and calls TForce.ProcessSearchDirectons; the division
    // report's OK writes the land-CAP request into LandCAPDivision[0]; CheckLandBasedCAP(Side) (unnamed, StartMinute's
    // CheckObjectives) reads LandCAPDivision[Side] every minute.
    void *searchCommit = nullptr, *processSearch = nullptr, *searchExecute = nullptr;
    int fSearchForce = -1;  // TdlgSearch.CurrentForce
    int fCapLevel = -1, fNightCap = -1, fSearchLeft = -1, fSearchRight = -1, fSearchDist = -1, fSearchInterval = -1,
        fTwoPhase = -1, fRepeatSearch = -1, fNightSearch = -1, fOrdered = -1, ordSize = 0, fCurrent = -1, curSize = 0;
    void** landCap = nullptr;  // LandCAPDivision[2]: side 0's and side 1's requested division
    void *landCapFn = nullptr, *getFlagship = nullptr, *isAirfield = nullptr, *rangeToShip = nullptr,
         *fighterRole = nullptr;  // CheckLandBasedCAP and the game routines it calls (the fighter test is unnamed)
    int fCapped = -1, fAfHome = -1, fAfType = -1, fTypeRole = -1;  // TDivision.CappedDivision; TAirFormation.ScenHomeBase,
                                                                   // AircraftType; TAircraftType.Role
    void* searchAir = nullptr;  // TForce.CheckSearchAircraft (the force's search launches, StartMinute's CheckObjectives)
    // Air strikes (engine level 7). The strike dialog (TdlgStrike, modeless) launches and readies aircraft through the
    // unnamed LaunchSomeAircraft(Self = the source formation, aNumber, aMission; const aTarget: TPointLL; aName: string;
    // aOrderedState: TAircraftStatus; aLoadout: Integer; aSpotNow: Boolean): TAirFormation, ret 0x14 (0xea4a18 in
    // 1.01.44): an immediate action in a live battle.
    void* launchFn = nullptr;
    int *afIdCounter = nullptr, *scoutCount = nullptr;  // the AFID counter (its allocator) and ScoutCount (generated names)
    void** strikeDlgVar = nullptr;                       // dlgStrike (sbStrikeClick's cell)
    void *strikeStyles = nullptr, *strikeExecute = nullptr, *strikeFill = nullptr;  // sbStrikeClick's two calls; the
                                                                                     // dialog's FillSquadronList
    int fStrikeDiv = -1, fStrikeLv = -1, fLvItems = -1, fItemData = -1;  // TdlgStrike.TheOriginatingDivision,
                                                                         // lvAirFormations; TListView.FListItems,
                                                                         // TListItem.FData
    void* lvGetItem = nullptr;                                           // TListItems.GetItem
    int fAfName = -1, fAfNumber = -1, fAfLoaded = -1, fAfOrdered = -1, fAfStatusCount = -1, fAfLead = -1,
        fAfParent = -1, fAfGroup = -1, fAfTarget = -1, fAfLocation = -1, fAfLoadout = -1;  // TAirFormation
    int fAirTorps = -1, fAirAsm = -1;  // TFightingShip.AirTorpedoes, AirToSurfaceMissiles (Stand down returns weapons)
    int fLbReports = -1;               // TfrmMain.lbReports (a strike's target report: `ls strikedlg <report>`)
    // Missiles (engine level 9). Automatic fire: CheckMissileFiringForAllShips skips a division with HoldMissileFire
    // exactly where it skips the player's divisions while "Fire at will" is unchecked (its gate is read in
    // ResolveMissiles), so each side's own Fire at will is honoured by holding that side's divisions for the call. Manual
    // salvoes: the missile dialog (TdlgMissileFire, modal) calls TfrmMain.FireMissileMount(Self, aMount, aShip;
    // aTargetShip, aTargetLocation, TargetBearing, NumberToFire): Integer, ret 0x10, which becomes a deferred action.
    int fHoldMsl = -1, fCbFireAtWill = -1, fRgDoctrine = -1, fCbState = -1, fRgButtons = -1;  // TDivision; TfrmMain;
                                                                    // TCustomCheckBox.FState; TCustomRadioGroup.FButtons
    int fCtlHint = -1, fCtlShowHint = -1, fCtlParentShowHint = -1, fCtlParent = -1, fCtlEnabled = -1;  // TControl
    uint8_t* nations = nullptr;  // the nations array the gate reads (an AI force of a nation below its threshold fires)
    int nationStride = 0, nationGate = -1, nationGateMin = 0, fShipNation = -1;
    void *fireMissileMount = nullptr, *fireSSM = nullptr, *fireMissiles = nullptr;
    void *mountCanFire = nullptr, *distToPoint = nullptr, *minRange = nullptr, *maxRange = nullptr;  // its preconditions
    int fElectric = -1, fPendingMsl = -1, mslAnyBearing = -1;  // TFightingShip; the ship type firing at any bearing
    void** mslDlgVar = nullptr;  // dlgMissileFire (sbFireSalvoClick's cell)
    void *mslFillTargets = nullptr, *mslFillShooters = nullptr, *mslTechText = nullptr, *mslTargetsClick = nullptr;
    void *mslFireOne = nullptr, *mslFireHalf = nullptr, *mslFireAll = nullptr;  // the dialog's buttons (the bridge)
    int fMslTargets = -1, fMslShooters = -1;  // TdlgMissileFire.lbMissileTargets, lvMissileShooters
    void *lvCount = nullptr, *lvItemChecked = nullptr, *lvItemSetChecked = nullptr;  // TListItems.Count, TListItem.Checked
    void** subsVar = nullptr;  // the battle's submarines (a TList of TSubmarine; FillShooterList's cell)
    int fSubSide = -1, fSubType = -1, fSubDowntime = -1, fSubLocation = -1, fSubTorpsFired = -1;  // TSubmarine
    int fTorpCalibre = -1, fTorpSide = -1;  // TTorpedo (a missile: Calibre >= 30, TTorpedo.IsMissile)
} E;

static int g_speedGetIdx = -1;  // the speed combo box's GetItemIndex slot in its VMT (from cbGameSpeedChange)
static void* g_ustrAsg = nullptr;  // System._UStrAsg(var Dest; Source): refcounted string assignment
static int g_fIdType = -1, g_fIdClass = -1, g_fHidden = -1;
static bool DualOk() { return g_ustrAsg && g_fIdType >= 0 && g_fIdClass >= 0 && g_fHidden >= 0; }
// The identification a ship has after a battle load: TFightingShip.Create's IdentifiedAsType (mov byte ptr
// [ebx+IdentifiedAsType], imm8) and no IdentifiedAsClass; the battle save keeps neither. -1: not found in Create.
static int g_idTypeDefault = -1;
static bool g_identCanon = false;  // SetView after a live load: the first capture gives the simulation that identification
static int g_identReset = 0;       // ships whose identification this PC's display had drawn during the load
// Sighting fields the battle screen's own drawing also writes (GlobeRender, for this PC's side): the simulation keeps its
// values, the display its own (and takes the simulation's new values), as for the identification fields. Engine level 14:
// LightState too. Between minutes (Intervals 0) the map's sighting circles of this PC's divisions (0xd8d084, from
// GlobeRender) call the sighting-range routine (0xe596ac), which writes the flagship's LightState from the battle clock;
// StartMinute's division routine (0xdab764) reads it before the simulation's own calls refresh it, so at dawn and dusk
// one PC took another path (the lockstep_ui e) DESYNCs: an extra Random(100) at 0xdabbbe on the PC whose map had drawn
// that division).
struct GuardField {
    const char* name;
    int size;
    int off;
};
static GuardField g_guard[] = {{"SightingLevel", 4, -1},
                               {"RadarSightingLevel", 4, -1},
                               {"LastSighted", 4, -1},
                               {"SeenByHumanControlledShips", 1, -1},
                               {"LightState", 4, -1}};
static const int kGuard = sizeof(g_guard) / sizeof(g_guard[0]);
static int g_guardHits = 0;  // display writes to these fields since the last boundary (kept out of the simulation)
static bool g_guardOn = true;  // tests (`ls guard 0`): the display's writes reach the simulation (as before 0.2.9)

static void Need(bool ok, const char* what) {
    if (!ok) {
        E.ok = false;
        E.missing += std::string(what) + " ";
    }
}

static void Le32(std::vector<int>& p, uint32_t v) {
    for (int i = 0; i < 4; i++) p.push_back((int)((v >> (8 * i)) & 0xFF));
}

// The characters of a Delphi string literal (code page 1200, element size 2, reference count -1, length, characters)
// whose text starts with `text`; nullptr if the image has none.
static uint8_t* FindLiteral(const wchar_t* text) {
    std::vector<int> pat = {0xB0, 0x04, 0x02, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, -1, -1, 0x00, 0x00};
    for (const wchar_t* c = text; *c; c++) {
        pat.push_back(*c & 0xFF);
        pat.push_back((*c >> 8) & 0xFF);
    }
    uint8_t* hit = dl::FindPattern((uint8_t*)dl::g_base + 0x1000, dl::g_size - 0x1000, pat);
    return hit ? hit + 12 : nullptr;
}

// The routine at fn (its first len bytes) uses the address `a` as an operand (e.g. its error handler's message).
static bool RefersTo(void* fn, size_t len, const void* a) {
    if (!fn || !a || !InImage((uintptr_t)fn)) return false;
    std::vector<int> p;
    Le32(p, U(a));
    return Find(fn, len, p) != nullptr;
}

// The routines and fields of the live-battle rules for human sides (see Engine). Each one that is not found turns its
// rule off (logged); the battle engine itself doesn't depend on them.
static void ResolveLiveRules() {
    uint8_t* hit;
    // StartMinute: xor edx, edx; mov eax, ebx; call CheckAIAirstrike; mov edx, 1; mov eax, ebx; call CheckAIAirstrike
    if (E.startMinute && (hit = Find(E.startMinute, 0x300, {0x33, 0xD2, 0x8B, 0xC3, 0xE8, -1, -1, -1, -1, 0xBA, 0x01,
                                                            0x00, 0x00, 0x00, 0x8B, 0xC3, 0xE8}))) {
        void* t = CallTarget(hit + 4);
        if (t == CallTarget(hit + 16) && RefersTo(t, 0x400, FindLiteral(L"Error in CheckAIAirstrike procedure!")))
            E.aiAirstrike = t;
    }
    E.searchPlan = dl::Method("TfrmMain", "SetAIAirSearchPlan");
    // EOTP: mov eax, [year cell]; cmp dword ptr [eax], <year>; jle +8; mov eax, [ebp-4]; call <routine>
    auto eotpYearCall = [&](int year) -> void* {
        std::vector<int> q = {0xA1, -1, -1, -1, -1, 0x81, 0x38, year & 0xFF, (year >> 8) & 0xFF, 0x00, 0x00,
                              0x7E, 0x08, 0x8B, 0x45, 0xFC, 0xE8};
        uint8_t* h = E.eotp ? Find(E.eotp, 0x800, q) : nullptr;
        return h ? CallTarget(h + 16) : nullptr;
    };
    void* fire = eotpYearCall(1940);
    if (RefersTo(fire, 0x400, FindLiteral(L"Error in CheckMissileFiringForAllShips procedure!"))) E.missileFire = fire;
    // UpdateMissilePanel writes the panel's target count and shows the panel: mov eax, [eax+stEnemyMissileTargets] ...
    // mov eax, [eax+pnMissiles]
    void* panel = eotpYearCall(1942);
    int fTargets = dl::Field("TfrmMain", "stEnemyMissileTargets"), fPanel = dl::Field("TfrmMain", "pnMissiles");
    if (panel && InImage(U(panel)) && fTargets > 0 && fPanel > 0) {
        std::vector<int> a = {0x8B, 0x80}, b = {0x8B, 0x80};
        Le32(a, (uint32_t)fTargets);
        Le32(b, (uint32_t)fPanel);
        if (Find(panel, 0x300, a) && Find(panel, 0x300, b)) E.missilePanel = panel;
    }
    // The Fastest available button's glyph: its release (UpDown1Click's) is SetDown(False), then mov eax,
    // [ebx+sbFastestAvailable]; xor edx, edx; call <ImageIndex setter>; sbFastestAvailableClick sets the pressed glyph
    // with mov edx, 1; call <the same setter>.
    if (E.releaseFastest && E.setDown && E.fFastestBtn > 0) {
        std::vector<int> q = {0x8B, 0x83};
        Le32(q, (uint32_t)E.fFastestBtn);
        q.insert(q.end(), {0x33, 0xD2, 0xE8});
        uint8_t* h0 = Find(E.releaseFastest, 0x40, q, 0);
        uint8_t* h1 = Find(E.releaseFastest, 0x40, q, 1);
        uint8_t* hc = Find(dl::Method("TfrmMain", "sbFastestAvailableClick"), 0x60, {0xBA, 0x01, 0x00, 0x00, 0x00, 0xE8});
        if (h0 && h1 && hc && CallTarget(h0 + 8) == E.setDown && CallTarget(h1 + 8) == CallTarget(hc + 5))
            E.setImageIndex = CallTarget(h1 + 8);
    }
    E.fAfId = dl::Field("TAirFormation", "AFId");
    E.fAfSide = dl::Field("TAirFormation", "Side");
    E.fAfMission = dl::Field("TAirFormation", "Mission");
    E.fAfStatus = dl::Field("TAirFormation", "Status");
    E.fAfConfirm = dl::Field("TAirFormation", "ConfirmTargetLoc");
    Log("lockstep resolve (live-battle engine %d): CheckAIAirstrike=%p SetAIAirSearchPlan=%p "
        "CheckMissileFiringForAllShips=%p UpdateMissilePanel=%p Fastest(SetDown=%p ImageIndex=%p) "
        "air(id=%#x side=%#x mission=%#x status=%#x confirm=%#x)",
        MP_LS_LEVEL, E.aiAirstrike, E.searchPlan, E.missileFire, E.missilePanel, E.setDown, E.setImageIndex, E.fAfId,
        E.fAfSide, E.fAfMission, E.fAfStatus, E.fAfConfirm);
}

// Force orders in live battles (engine level 3): change force, change lead, battle turn-away. Each feature whose pieces
// are not all found stays off (its click handlers keep the 0.3.x blocker).
static bool g_featChforce = false, g_featLead = false;

static void ResolveForceOrders() {
    uint8_t* hit;
    E.listIndexOf = dl::Method("TList", "IndexOf");
    E.listDelete = dl::Method("TList", "Delete");
    E.listAdd = dl::Method("TList", "Add");
    E.setForceLead = dl::Method("TfrmMain", "SetForceLeadDiv");
    E.doTurnAway = dl::Method("TfrmMain", "DoBattleTurnAway");
    E.getForceFlagship = dl::Method("TForce", "GetForceFlagship");
    E.mShow = dl::Method("TCustomForm", "Show");
    E.fSelDiv = dl::Field("TfrmMain", "SelectedDivision");
    E.fSelForce = dl::Field("TfrmMain", "SelectedForce");
    E.fSelNewLead = dl::Field("TfrmMain", "SelectingNewLead");
    E.fSelTurnAway = dl::Field("TfrmMain", "SelectingLeadForBattleTurnAway");
    E.fTag = dl::Field("TComponent", "FTag");
    E.fFDivs = dl::Field("TForce", "Divisions");
    E.fHasChangedLead = dl::Field("TForce", "HasChangedLead");
    E.fTimeToTurnAway = dl::Field("TForce", "TimeToTurnAway");
    E.fMyForce = dl::Field("TDivision", "myForce");
    E.fAdmin = dl::Field("TDivision", "Admin");
    E.fLeadDiv = dl::Field("TDivision", "LeadDivision");
    E.fFLead = dl::Field("TDivision", "ForceLeadDivision");
    auto calls = [](void* fn, size_t len, std::initializer_list<void*> targets) {
        if (!fn) return false;
        std::vector<uint8_t*> t = dl::CallTargets((uint8_t*)fn, len);
        for (void* x : targets)
            if (!x || std::find(t.begin(), t.end(), (uint8_t*)x) == t.end()) return false;
        return true;
    };
    // TfrmMain.Changeforce2Click(Self, Sender): the selected division moves to Forces[Sender.Tag] after a Yes/No question:
    // old.Divisions.Delete(old.Divisions.IndexOf(div)); new.Divisions.Add(div); div.myForce := new; dec div.Admin.
    void* chf = dl::Method("TfrmMain", "Changeforce2Click");
    bool chfCalls = calls(chf, 0xf0, {E.listIndexOf, E.listDelete, E.listAdd, E.messageDlg, E.mFillShipTree});
    // push 0; push <question>; push dword ptr [esi+TForce.Name]; push <tail>
    if (chf && (hit = Find(chf, 0x80, {0x6A, 0x00, 0x68, -1, -1, -1, -1, 0xFF, 0x76, -1, 0x68, -1, -1, -1, -1})) &&
        hit[9] == E.fForceName && InImage(*(uint32_t*)(hit + 3)) && InImage(*(uint32_t*)(hit + 11))) {
        E.chforceText = dl::ReadUStr((void*)(uintptr_t) * (uint32_t*)(hit + 3));
        E.chforceTail = dl::ReadUStr((void*)(uintptr_t) * (uint32_t*)(hit + 11));
    }
    // movzx ecx, word ptr [<buttons>]; mov dl, 3 (mtConfirmation); call MessageDlg
    if (chf && (hit = Find(chf, 0x80, {0x0F, 0xB7, 0x0D, -1, -1, -1, -1, 0xB2, 0x03, 0xE8})) &&
        CallTarget(hit + 9) == E.messageDlg && InImage(*(uint32_t*)(hit + 3)))
        E.chforceButtons = *(uint16_t*)(uintptr_t) * (uint32_t*)(hit + 3);
    g_featChforce = chfCalls && !E.chforceText.empty() && E.chforceButtons > 0 && E.fSelDiv > 0 && E.fTag >= 0 &&
                    E.fFDivs >= 0 && E.fMyForce >= 0 && E.fAdmin >= 0 && E.fTvShips > 0 && E.playerSideVar;
    // TdlgSelectLead.bnOKClick: mov eax, [ebx+lbLeadShips]; mov edx, [eax]; call [edx+GetItemIndex]; ... mov eax,
    // [esi+FItems]; mov ecx, [eax]; call [ecx+GetObject]; ... SetForceLeadDiv / DoBattleTurnAway (the item, as TDivision)
    void* okClick = dl::Method("TdlgSelectLead", "bnOKClick");
    E.fLbLeadShips = dl::Field("TdlgSelectLead", "lbLeadShips");
    if (okClick && (hit = Find(okClick, 0x20, {0x8B, 0x83, -1, -1, -1, -1, 0x8B, 0x10, 0xFF, 0x92, -1, -1, -1, -1})) &&
        E.fLbLeadShips > 0 && *(int32_t*)(hit + 2) == E.fLbLeadShips)
        E.lbIndexSlot = *(int32_t*)(hit + 10);
    if (okClick && (hit = Find(okClick, 0x60, {0x8B, 0x86, -1, -1, -1, -1, 0x8B, 0x08, 0xFF, 0x51, -1}))) {
        E.fLbItems = *(int32_t*)(hit + 2);
        E.getObjectSlot = hit[10];
        int rtti = dl::Field("TCustomListBox", "FItems");
        if (rtti >= 0 && rtti != E.fLbItems) E.fLbItems = -1;
    }
    bool okCalls = calls(okClick, 0xe0, {E.setForceLead, E.doTurnAway});
    // Both routines read the force from SelectedForce: SetForceLeadDiv starts with cmp dword ptr [ebx+SelectedForce], 0
    // and DoBattleTurnAway with mov eax, [esp]; cmp dword ptr [eax+SelectedForce], 0.
    std::vector<int> p1 = {0x83, 0xBB}, p2 = {0x83, 0xB8};
    Le32(p1, (uint32_t)E.fSelForce);
    Le32(p2, (uint32_t)E.fSelForce);
    p1.push_back(0x00);
    p2.push_back(0x00);
    bool selForce = E.fSelForce > 0 && Find(E.setForceLead, 0x20, p1) && Find(E.doTurnAway, 0x20, p2);
    g_featLead = okCalls && selForce && calls(E.setForceLead, 0xd0, {E.getForceFlagship, E.listIndexOf, E.mClose}) &&
                 calls(E.doTurnAway, 0x1c0, {E.getForceFlagship, E.listIndexOf, E.mClose}) && E.lbIndexSlot > 0 &&
                 E.fLbItems > 0 && E.getObjectSlot > 0 && E.fSelNewLead > 0 && E.fSelTurnAway > 0 && E.fSelDiv > 0 &&
                 E.fFDivs >= 0 && E.fHasChangedLead >= 0 && E.fTimeToTurnAway >= 0 && E.fLeadDiv >= 0 && E.mShow;
    Log("lockstep resolve (force orders): change force %s (question %d chars, buttons %d), change lead and battle "
        "turn-away %s (list index slot %#x, items %#x, object slot %#x)",
        g_featChforce ? "on" : "OFF", (int)E.chforceText.size(), E.chforceButtons, g_featLead ? "on" : "OFF",
        E.lbIndexSlot, E.fLbItems, E.getObjectSlot);
}

// Auto-run per player (engine level 4). The pieces come from the game's own handler, TfrmMain.sbAutoRunBattleClick
// (0xe02824 in 1.01.44): its question dialog, the "move to closest friendly base" orders and the force name it also
// gives them to. Without all of them the button keeps the 0.3.x blocker.
static bool g_featAutoRun = false;

static void ResolveAutoRun() {
    uint8_t* hit;
    void* h = dl::Method("TfrmMain", "sbAutoRunBattleClick");
    E.autoClearHints = dl::Method("TdlgEventAnswer", "ClearHints");
    E.getFlagDiv = dl::Method("TForce", "GetForceFlagDivision");
    E.fWithdraw = dl::Field("TForce", "ForceWithdraw");
    E.fMission = dl::Field("TForce", "ForceMission");
    E.fAutoBtn = dl::Field("TfrmMain", "sbAutoRunBattle");
    // mov eax, [dlgEventAnswer cell]; mov eax, [eax]; call ClearHints
    if (h && (hit = Find(h, 0x60, {0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0xE8})) && E.autoClearHints &&
        CallTarget(hit + 7) == E.autoClearHints)
        E.autoDlgVar = (void**)VarFromCell(hit + 1);
    // cmp byte ptr [edi+TForce.AIControlled], 0; jne +0b; mov dword ptr [edi+ForceWithdraw], <v>; mov byte ptr
    // [edi+ForceMission], <v>
    if (h && E.fFAI > 0 && E.fFAI < 0x80 &&
        (hit = Find(h, 0x100, {0x80, 0x7F, E.fFAI, 0x00, 0x75, 0x0B, 0xC7, 0x47, -1, -1, -1, -1, -1, 0xC6, 0x47, -1, -1})) &&
        hit[8] == E.fWithdraw && hit[15] == E.fMission) {
        E.homeWithdraw = *(int32_t*)(hit + 9);
        E.homeMission = hit[16];
    }
    // mov ecx, 1; mov edx, eax (the force's name); mov eax, <literal>; call Pos
    if (h && (hit = Find(h, 0x100, {0xB9, 0x01, 0x00, 0x00, 0x00, 0x8B, 0xD0, 0xB8, -1, -1, -1, -1, 0xE8})) &&
        InImage(*(uint32_t*)(hit + 8)))
        E.supportForce = dl::ReadUStr((void*)(uintptr_t) * (uint32_t*)(hit + 8));
    std::vector<int> div = {0xC6, 0x40, E.fDAI, 0x01};  // mov byte ptr [eax+TDivision.AIControlled], 1
    bool divAi = h && E.fDAI > 0 && E.fDAI < 0x80 && Find(h, 0x100, div);
    bool calls = false;
    if (h) {
        std::vector<uint8_t*> t = dl::CallTargets((uint8_t*)h, 0x1e0);
        calls = E.getFlagDiv && E.mExecGeneral &&
                std::find(t.begin(), t.end(), (uint8_t*)E.getFlagDiv) != t.end() &&
                std::find(t.begin(), t.end(), (uint8_t*)E.mExecGeneral) != t.end();
    }
    g_featAutoRun = E.autoDlgVar && E.homeWithdraw > 0 && E.homeMission > 0 && !E.supportForce.empty() && divAi && calls &&
                    E.fFAI >= 0 && E.fDAI >= 0 && E.fHuman >= 0 && E.fSDAI >= 0 && E.forcesVar && E.divisionsVar;
    Log("lockstep resolve (auto-run): %s (dialog %p, home orders ForceWithdraw+%#x := %d ForceMission+%#x := %d, also "
        "'%s', flag division %p, button +%#x)",
        g_featAutoRun ? "on" : "OFF", E.autoDlgVar, E.fWithdraw, E.homeWithdraw, E.fMission, E.homeMission,
        W2U(E.supportForce).c_str(), E.getFlagDiv, E.fAutoBtn);
}

// Manual torpedo salvoes (engine level 5). The pieces come from the game's fire routine TfrmMain.FireMount (0xda8cc4 in
// 1.01.44) and the torpedo dialog's Fire (TdlgTorpTargets.bnFireClick, 0xadda28), which calls it. Without all of them the
// Fire torpedoes buttons keep the 0.3.x blocker; without FireMount's early-return pattern the routine isn't hooked either.
static bool g_featTorp = false;
static bool g_torpHookable = false;  // FireMount's code is the one read (its hook may be installed)

static void ResolveTorpedoes() {
    uint8_t* hit;
    E.fireMount = dl::Method("TfrmMain", "FireMount");
    E.isSubmerged = dl::Method("TTorpedomount", "IsSubmerged");
    E.torpsClick = dl::Method("TdlgDivisionReport", "bnTorpsClick");
    E.torpFireClick = dl::Method("TdlgTorpTargets", "bnFireClick");
    E.fMountList = dl::Field("Tship", "TorpedomountList");
    E.fTorpsLeft = dl::Field("TFightingTorpedoMount", "TorpsLeft");
    E.fReloadTime = dl::Field("TFightingTorpedoMount", "ReloadTime");
    E.fMountDestroyed = dl::Field("TFightingTorpedoMount", "Destroyed");
    E.fReloads = dl::Field("TTorpedomount", "Reloads");
    E.fTubes = dl::Field("TTorpedomount", "Tubes");
    E.fMountType = dl::Field("TTorpedomount", "MountType");
    E.fMountPos = dl::Field("TTorpedomount", "Position");
    E.fFiredMount = dl::Field("TFightingShip", "FiredMountThisTurn");
    E.fCurSpeed = dl::Field("TFightingShip", "CurrentSpeed");
    E.fShipType = dl::Field("Tship", "ShipType");
    E.fSunk = dl::Field("TFightingShip", "Sunk");
    E.fSelShip = dl::Field("TfrmMain", "SelectedShip");
    E.fTorpMissiles = dl::Field("TdlgTorpTargets", "Missiles");
    // FireMount's early returns, which change nothing: mov eax, [ebp-8]; cmp dword ptr [eax+TorpsLeft], 0; je <exit>;
    // mov eax, [ebp-8]; call IsSubmerged; test al, al; je; cmp dword ptr [ebx+CurrentSpeed], <limit>; jle;
    // cmp byte ptr [ebx+ShipType], <MTB>; je; cmp byte ptr [ebp+8] (Manual), 0; je <exit>; or ecx, -1; or edx, -1;
    // mov eax, <message>; call ShowMessagePos (push 0; push edx; push ecx; push 0; mov dl, 4; movzx ecx, word ptr
    // [<buttons>]; call MessageDlgPosHelp: the dialog policy's hook)
    bool head = false;
    if (E.fireMount && E.isSubmerged &&
        (hit = Find(E.fireMount, 0x80, {0x8B, 0x45, 0xF8, 0x83, 0xB8, -1, -1, -1, -1, 0x00, 0x0F, 0x84, -1, -1, -1, -1,
                                        0x8B, 0x45, 0xF8, 0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x0F, 0x84, -1, -1, -1, -1,
                                        0x83, 0xBB, -1, -1, -1, -1, -1, 0x7E, -1, 0x80, 0xBB, -1, -1, -1, -1, -1, 0x74, -1,
                                        0x80, 0x7D, 0x08, 0x00, 0x0F, 0x84, -1, -1, -1, -1, 0x83, 0xC9, 0xFF, 0x83, 0xCA,
                                        0xFF, 0xB8, -1, -1, -1, -1, 0xE8}))) {
        uint8_t* smp = (uint8_t*)CallTarget(hit + 71);
        uint8_t* box = InImage(U(smp)) ? Find(smp, 0x20, {0xB2, 0x04, 0x0F, 0xB7, 0x0D, -1, -1, -1, -1, 0xE8}) : nullptr;
        head = CallTarget(hit + 19) == E.isSubmerged && *(int32_t*)(hit + 5) == E.fTorpsLeft &&
               *(int32_t*)(hit + 34) == E.fCurSpeed && *(int32_t*)(hit + 43) == E.fShipType && box &&
               CallTarget(box + 9) == E.msgDlg;
        if (head) {
            E.subSpeed = (int8_t)hit[38];
            E.subMtb = hit[47];
        }
    }
    g_torpHookable = head && Find(E.fireMount, 0x500, {0xC2, 0x14, 0x00}) && E.fTorpsLeft > 0 && E.fCurSpeed > 0 &&
                     E.fShipType > 0;
    // bnFireClick: call FireMount; mov eax, [ebp-xx] (the ship); mov byte ptr [eax+FiredMountThisTurn], 1; and its
    // missile branch's test: cmp byte ptr [eax+Missiles], 0; je <torpedoes>
    bool fire = false, missiles = false;
    if (E.torpFireClick && E.fireMount &&
        (hit = Find(E.torpFireClick, 0x600, {0xE8, -1, -1, -1, -1, 0x8B, 0x45, -1, 0xC6, 0x80, -1, -1, -1, -1, 0x01})))
        fire = CallTarget(hit) == E.fireMount && *(int32_t*)(hit + 10) == E.fFiredMount;
    if (E.torpFireClick && E.fTorpMissiles > 0) {
        std::vector<int> p = {0x80, 0xB8};
        Le32(p, (uint32_t)E.fTorpMissiles);
        p.insert(p.end(), {0x00, 0x0F, 0x84});
        missiles = Find(E.torpFireClick, 0xc0, p) != nullptr;
    }
    g_featTorp = g_torpHookable && fire && missiles && E.torpsClick && E.fMountList > 0 && E.fFiredMount > 0 &&
                 E.fSunk > 0 && E.fSelShip > 0 && E.fMountType >= 0 && E.torpVar && E.mHide;
    // The bridge (`ls torp`): bnFireClick reads lbMounts' ItemIndex (mov eax, [eax+lbMounts]; mov edx, [eax]; call
    // [edx+GetItemIndex]) and Items.Objects[] (mov eax, [ebx+FItems]; mov ecx, [eax]; call [ecx+GetObject]);
    // bnTorpsClick selects the ship in the dialog's combo box (... mov edx, eax; mov eax, ebx; mov ecx, [eax]; call
    // [ecx+SetItemIndex]). TCustomListControl declares SetItemIndex right after GetItemIndex.
    E.showDivDialog = dl::Method("TfrmMain", "ShowDivisionDialog");
    E.lbTargetsClick = dl::Method("TdlgTorpTargets", "lbTargetsClick");
    E.lbMountsClick = dl::Method("TdlgTorpTargets", "lbMountsClick");
    E.cbShipsChange = dl::Method("TdlgTorpTargets", "cbShipsInDivisionChange");
    E.fListItems = dl::Field("TCustomListBox", "FItems");
    int fMounts = dl::Field("TdlgTorpTargets", "lbMounts");
    if (E.torpFireClick && fMounts > 0) {
        std::vector<int> p = {0x8B, 0x80};
        Le32(p, (uint32_t)fMounts);
        p.insert(p.end(), {0x8B, 0x10, 0xFF, 0x92});
        if ((hit = Find(E.torpFireClick, 0x60, p))) E.lbGetIdxSlot = *(int32_t*)(hit + 10);
    }
    if (E.torpFireClick && E.fListItems > 0) {
        std::vector<int> p = {0x8B, 0x83};
        Le32(p, (uint32_t)E.fListItems);
        p.insert(p.end(), {0x8B, 0x08, 0xFF, 0x51});
        if ((hit = Find(E.torpFireClick, 0x100, p))) E.itemsObjSlot = hit[10];
    }
    if (E.torpsClick &&
        (hit = Find(E.torpsClick, 0x200, {0x8B, 0x08, 0xFF, 0x51, -1, 0x8B, 0xD0, 0x8B, 0xC3, 0x8B, 0x08, 0xFF, 0x91})))
        E.lbSetIdxSlot = *(int32_t*)(hit + 13);
    if (E.lbGetIdxSlot <= 0 || E.lbSetIdxSlot != E.lbGetIdxSlot + 4) E.lbSetIdxSlot = -1;
    Log("lockstep resolve (torpedo salvoes): %s (FireMount %p: submerged tubes up to %d knots unless ship type %d; "
        "hook %s, dialog fire %d, missile test %d; mounts +%#x, TorpsLeft +%#x, FiredMountThisTurn +%#x; bridge list "
        "slots %#x/%#x, items +%#x object slot %#x)",
        g_featTorp ? "on" : "OFF", E.fireMount, E.subSpeed, E.subMtb, g_torpHookable ? "possible" : "OFF", fire ? 1 : 0,
        missiles ? 1 : 0, E.fMountList, E.fTorpsLeft, E.fFiredMount, E.lbGetIdxSlot, E.lbSetIdxSlot, E.fListItems,
        E.itemsObjSlot);
}

// Routine air operations and land-based CAP (engine level 6; limits-air A and B).
// - The routine air ops dialog (TdlgSearch, modal) commits through an unnamed routine (TdlgSearch.bnSaveClick: mov edx,
//   [eax+CurrentForce]; call <commit>; 0xb4bef4 in 1.01.44) that writes nine TForce fields from its controls and calls
//   TForce.ProcessSearchDirectons (SearchPatternOrdered from the sector): those ten fields are force order fields. The
//   commit's stores are read here, so the guard covers what the dialog writes.
// - The division report's OK (bnOkClick) writes the land-CAP request into LandCAPDivision[0] (the game's human side)
//   when its sbLandCAP button is down. CheckLandBasedCAP(Side) (unnamed: StartMinute -> CheckObjectives, 'Error in
//   CheckLandBasedCAP procedure!', 0xea5630) gives the nearest airfield division with a fighter formation based there
//   the CappedDivision LandCAPDivision[Side], whatever the airfield's side. In a live battle the mod runs its own copy
//   with the airfield's side checked (LandCapForSide), so the routine's code is read here to be sure the copy does what
//   it does.
// Without every piece the buttons keep their 0.3.x blocker.
static bool g_featAirOps = false, g_featLandCap = false;
static bool g_landCapHookable = false;  // CheckLandBasedCAP is the routine read here (its hook may be installed)
static bool g_scoutHookable = false;    // ShipLaunchSearchAircraft's question tests PlayerSide (CheckSearchAircraft's hook)

// TForce's two static arrays have no type in the RTTI: their sizes come from the layout, up to the next field (19
// Booleans and 19 Integers in 1.01.44; the game uses 18 sectors of 20 degrees). 0 = not known.
static int ForceArraySize(const char* name) {
    const char* next = !strcmp(name, "SearchPatternOrdered")   ? "SearchPatternCurrent"
                       : !strcmp(name, "SearchPatternCurrent") ? "SearchLimitLeft"
                                                               : nullptr;
    int a = next ? dl::Field("TForce", name) : -1, b = next ? dl::Field("TForce", next) : -1;
    int n = a > 0 && b > a ? b - a : 0;
    return n >= 18 && n <= 128 ? n : 0;
}

static void ResolveAirOps() {
    uint8_t* hit;
    E.fCapLevel = dl::Field("TForce", "CapLevel");
    E.fNightCap = dl::Field("TForce", "NightCAP");
    E.fSearchLeft = dl::Field("TForce", "SearchLimitLeft");
    E.fSearchRight = dl::Field("TForce", "SearchLimitRight");
    E.fSearchDist = dl::Field("TForce", "SearchDistance");
    E.fSearchInterval = dl::Field("TForce", "SearchInterval");
    E.fTwoPhase = dl::Field("TForce", "TwoPhaseSearch");
    E.fRepeatSearch = dl::Field("TForce", "RepeatSearch");
    E.fNightSearch = dl::Field("TForce", "NightSearch");
    E.fOrdered = dl::Field("TForce", "SearchPatternOrdered");
    E.fCurrent = dl::Field("TForce", "SearchPatternCurrent");
    E.ordSize = ForceArraySize("SearchPatternOrdered");
    E.curSize = ForceArraySize("SearchPatternCurrent");
    E.fSearchForce = dl::Field("TdlgSearch", "CurrentForce");
    E.processSearch = dl::Method("TForce", "ProcessSearchDirectons");
    E.searchExecute = dl::Method("TdlgSearch", "Execute");
    void* save = dl::Method("TdlgSearch", "bnSaveClick");
    if (save && E.fSearchForce > 0) {
        std::vector<int> p = {0x8B, 0x90};
        Le32(p, (uint32_t)E.fSearchForce);
        p.push_back(0xE8);
        if ((hit = Find(save, 0x10, p))) E.searchCommit = CallTarget(hit + 6);
    }
    // The commit's stores into the force (esi): mov [esi+field], eax (Integers) / mov [esi+field], al (Booleans); then
    // mov eax, esi; call ProcessSearchDirectons.
    struct Store {
        int off, op;
    } stores[] = {{E.fCapLevel, 0x89},    {E.fNightCap, 0x88},       {E.fSearchLeft, 0x89},
                  {E.fSearchRight, 0x89}, {E.fSearchDist, 0x89},     {E.fSearchInterval, 0x89},
                  {E.fRepeatSearch, 0x88}, {E.fNightSearch, 0x88},   {E.fTwoPhase, 0x88}};
    int found = 0;
    bool commit = E.searchCommit && InImage(U(E.searchCommit));
    for (auto& s : stores) {
        std::vector<int> p = {s.op, 0x86};
        Le32(p, (uint32_t)s.off);
        if (commit && s.off > 0 && Find(E.searchCommit, 0xc0, p)) found++;
    }
    bool psd = commit && E.processSearch && (hit = Find(E.searchCommit, 0xc0, {0x8B, 0xC6, 0xE8})) &&
               CallTarget(hit + 2) == E.processSearch;
    // ProcessSearchDirectons: cmp ebx, 18 (the sectors) and mov byte ptr [esi+ebx+SearchPatternOrdered], 0
    bool sectors = false;
    if (E.processSearch && E.fOrdered > 0) {
        std::vector<int> p = {0xC6, 0x84, 0x1E};
        Le32(p, (uint32_t)E.fOrdered);
        p.push_back(0x00);
        sectors = Find(E.processSearch, 0x40, {0x83, 0xFB, 0x12}) && Find(E.processSearch, 0x40, p);
    }
    g_featAirOps = found == 9 && psd && sectors && E.ordSize > 0 && E.curSize > 0 && E.searchExecute && E.fForceSide >= 0 &&
                   E.forcesVar;
    // The force's search launches: TForce.CheckSearchAircraft (CheckObjectives, every force every minute). Its carriers'
    // TFightingShip.ShipLaunchSearchAircraft asks "Do you want <ship> to launch scout aircraft?" for PlayerSide's ships
    // that see enemy ships (unless the Floatplane launch preference is Automatic): mov eax, [esi+Side]; cmp eax,
    // [PlayerSide]. The only PlayerSide test in CheckSearchAircraft's call tree (static scan, depth 5).
    E.searchAir = dl::Method("TForce", "CheckSearchAircraft");
    void* sls = dl::Method("TFightingShip", "ShipLaunchSearchAircraft");
    if (E.searchAir && sls && E.fShipSide > 0 && E.playerSideVar) {
        std::vector<int> p = {0x8B, 0x86};
        Le32(p, (uint32_t)E.fShipSide);
        p.insert(p.end(), {0x3B, 0x05});
        Le32(p, U(E.playerSideVar));
        g_scoutHookable = Find(sls, 0x100, p) != nullptr;
    }

    // The land-CAP slots, from the division report's OK: mov eax, [ebx+sbLandCAP]; cmp byte ptr [eax+FDown], 0; je;
    // mov eax, [esi] (frmMain); mov eax, [eax+SelectedDivision]; mov edx, [<LandCAPDivision cell>]; mov [edx], eax
    void* okClick = dl::Method("TdlgDivisionReport", "bnOkClick");
    int fBtn = dl::Field("TdlgDivisionReport", "sbLandCAP");
    if (okClick && fBtn > 0 && E.fDownOff > 0 && E.fSelDiv > 0) {
        std::vector<int> p = {0x8B, 0x83};
        Le32(p, (uint32_t)fBtn);
        p.insert(p.end(), {0x80, 0xB8});
        Le32(p, (uint32_t)E.fDownOff);
        p.insert(p.end(), {0x00, 0x74, -1, 0x8B, 0x06, 0x8B, 0x80});
        Le32(p, (uint32_t)E.fSelDiv);
        p.insert(p.end(), {0x8B, 0x15, -1, -1, -1, -1, 0x89, 0x02});
        if ((hit = Find(okClick, 0x800, p))) E.landCap = (void**)VarFromCell(hit + 25);
    }
    // CheckLandBasedCAP. StartMinute: xor edx, edx; mov eax, ebx; call <division AI>; mov eax, ebx; call CheckObjectives;
    // mov eax, ebx; call <force withdrawal>. CheckObjectives: xor eax, eax; call X; mov eax, 1; call X.
    void* checkObj = nullptr;
    if (E.startMinute && (hit = Find(E.startMinute, 0x200, {0x33, 0xD2, 0x8B, 0xC3, 0xE8, -1, -1, -1, -1, 0x8B, 0xC3, 0xE8,
                                                            -1, -1, -1, -1, 0x8B, 0xC3, 0xE8})))
        checkObj = CallTarget(hit + 11);
    if (checkObj && InImage(U(checkObj)) &&
        (hit = Find(checkObj, 0x80, {0x33, 0xC0, 0xE8, -1, -1, -1, -1, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xE8}))) {
        void* t = CallTarget(hit + 2);
        if (t == CallTarget(hit + 12) && InImage(U(t)) && RefersTo(t, 0x200, FindLiteral(L"Error in CheckLandBasedCAP procedure!")))
            E.landCapFn = t;
    }
    E.getFlagship = dl::Method("TDivision", "GetFlagship");
    E.isAirfield = dl::Method("TFightingShip", "IsAirfield");
    E.rangeToShip = dl::Method("TFightingShip", "GetRangeToShip");
    E.fCapped = dl::Field("TDivision", "CappedDivision");
    E.fAfHome = dl::Field("TAirFormation", "ScenHomeBase");
    E.fAfType = dl::Field("TAirFormation", "AircraftType");
    E.fTypeRole = dl::Field("TAircraftType", "Role");
    // The routine, step by step (LandCapForSide does the same with the side test added):
    //   mov eax, [eax*4+slots]; test eax, eax; je; mov eax, [eax+Ships]; cmp dword ptr [eax+8], 0; jle   (the target)
    //   mov eax, [<divisions cell>]; mov eax, [eax]; mov eax, [eax+8]                                     (the divisions)
    //   mov ebx, eax; mov eax, [ebx+Ships]; cmp dword ptr [eax+8], 0; jle; mov eax, ebx; call GetFlagship;
    //   call IsAirfield; test al, al; je                                                                  (a candidate)
    //   mov eax, [eax*4+slots]; call GetFlagship; push eax; mov eax, ebx; call GetFlagship; pop edx;
    //   call GetRangeToShip; cmp eax, [ebp-10h]; jge                                                      (nearer)
    //   mov eax, [AirFormations]                                                                          (the formations)
    //   mov eax, ebx; call GetFlagship; cmp eax, [esi+ScenHomeBase]; jne; mov eax, [esi+AircraftType];
    //   movzx eax, byte ptr [eax+Role]; call <fighter test>; test al, al; je                              (a fighter there)
    //   mov eax, [ebp-4]; mov eax, [eax*4+slots]; mov edx, [ebp-14h]; mov [edx+CappedDivision], eax       (the result)
    uint8_t* lc = (uint8_t*)E.landCapFn;
    bool small = E.fShips > 0 && E.fShips < 0x80 && E.fAfHome > 0 && E.fAfHome < 0x80 && E.fAfType > 0 &&
                 E.fAfType < 0x80 && E.fCapped > 0 && E.fCapped < 0x80 && E.fTypeRole > 0;
    bool shape = false;
    if (lc && E.landCap && small && E.getFlagship && E.isAirfield && E.rangeToShip && E.airVar && E.divisionsVar) {
        std::vector<int> head = {0x8B, 0x04, 0x85};
        Le32(head, U(E.landCap));
        head.insert(head.end(), {0x85, 0xC0, 0x0F, 0x84, -1, -1, -1, -1, 0x8B, 0x40, E.fShips, 0x83, 0x78, 0x08, 0x00, 0x0F,
                                 0x8E});
        uint8_t* divs = Find(lc, 0x100, {0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x8B, 0x40, 0x08});
        uint8_t* cand = Find(lc, 0x100, {0x8B, 0xD8, 0x8B, 0x43, E.fShips, 0x83, 0x78, 0x08, 0x00, 0x0F, 0x8E, -1, -1, -1, -1,
                                         0x8B, 0xC3, 0xE8, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x0F, 0x84});
        std::vector<int> range = {0x8B, 0x04, 0x85};
        Le32(range, U(E.landCap));
        range.insert(range.end(), {0xE8, -1, -1, -1, -1, 0x50, 0x8B, 0xC3, 0xE8, -1, -1, -1, -1, 0x5A, 0xE8, -1, -1, -1, -1,
                                   0x3B, 0x45, 0xF0, 0x7D});
        uint8_t* rg = Find(lc, 0x100, range);
        std::vector<int> air = {0xA1};
        Le32(air, U(E.airVar));
        std::vector<int> fighter = {0x8B, 0xC3, 0xE8, -1, -1, -1, -1, 0x3B, 0x46, E.fAfHome, 0x75, -1, 0x8B, 0x46, E.fAfType,
                                    0x0F, 0xB6, 0x80};
        Le32(fighter, (uint32_t)E.fTypeRole);
        fighter.insert(fighter.end(), {0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x74});
        uint8_t* fg = Find(lc, 0x140, fighter);
        std::vector<int> write = {0x8B, 0x45, 0xFC, 0x8B, 0x04, 0x85};
        Le32(write, U(E.landCap));
        write.insert(write.end(), {0x8B, 0x55, 0xEC, 0x89, 0x42, E.fCapped});
        if (fg) E.fighterRole = CallTarget(fg + 22);
        // the fighter test is a pure role test (Role 0, 10 or 11): test al, al; je; add al, -10; sub al, 2; jb;
        // xor eax, eax; ret; mov al, 1; ret
        bool role = E.fighterRole && InImage(U(E.fighterRole)) &&
                    Find(E.fighterRole, 0x10, {0x84, 0xC0, 0x74, -1, 0x04, 0xF6, 0x2C, 0x02, 0x72, -1, 0x33, 0xC0, 0xC3,
                                               0xB0, 0x01, 0xC3}) == (uint8_t*)E.fighterRole;
        shape = Find(lc, 0x80, head) && divs && VarFromCell(divs + 1) == (void*)E.divisionsVar && cand &&
                CallTarget(cand + 17) == E.getFlagship && CallTarget(cand + 22) == E.isAirfield && rg &&
                CallTarget(rg + 7) == E.getFlagship && CallTarget(rg + 15) == E.getFlagship &&
                CallTarget(rg + 21) == E.rangeToShip && Find(lc, 0x140, air) && fg && CallTarget(fg + 2) == E.getFlagship &&
                role && Find(lc, 0x180, write);
    }
    g_landCapHookable = shape;
    g_featLandCap = shape && E.fDivSide >= 0;  // (and the hooks: EnsureHooks)
    // The slots are used (guarded, hashed, swapped for the display) only when the routine reads the same address.
    std::vector<int> slotRead = {0x8B, 0x04, 0x85};
    Le32(slotRead, U(E.landCap));
    if (E.landCap && !(lc && InImage(U(lc)) && Find(lc, 0x80, slotRead))) {
        Log("lockstep: the land-CAP slots %p were not confirmed by CheckLandBasedCAP %p: not used", E.landCap, lc);
        E.landCap = nullptr;
    }
    Log("lockstep resolve (routine air ops and land CAP): air ops %s (commit %p: %d of 9 stores, ProcessSearchDirectons "
        "%d, 18 sectors %d; SearchPatternOrdered +%#x %d bytes, SearchPatternCurrent +%#x %d bytes; scout question's "
        "PlayerSide test %s), land CAP %s (slots %p, CheckLandBasedCAP %p read %s, fighter test %p)",
        g_featAirOps ? "on" : "OFF", E.searchCommit, found, psd ? 1 : 0, sectors ? 1 : 0, E.fOrdered, E.ordSize, E.fCurrent,
        E.curSize, g_scoutHookable ? "found" : "NOT found", g_featLandCap ? "on" : "OFF", E.landCap, E.landCapFn,
        shape ? "ok" : "NOT as expected", E.fighterRole);
}

// The land-CAP slots, LandCAPDivision[side]. The game's dialog reads and writes slot 0 (the game's human side): on the
// PC that commands side 1 the two slots are swapped while this PC's display state is in place (the same moments as
// PlayerSide, ApplyViewSide), so the division report shows and changes this player's own request. The simulation, the
// hash and the battle save see the game's layout (CanonBegin), and the order guard always does (LandCapSlot).
static bool g_lcSwapped = false;  // the slots hold the display layout (side 1's request in slot 0)

static void** LandCapSlot(int side) { return E.landCap ? &E.landCap[g_lcSwapped ? 1 - side : side] : nullptr; }

static void SwapLandCap(bool want) {
    if (!E.landCap || want == g_lcSwapped) return;
    std::swap(E.landCap[0], E.landCap[1]);
    g_lcSwapped = want;
}

// Air strikes (engine level 7; limits-air C, D, E). The strike dialog's Launch (bnLaunchClick) and Ready (bnReadyClick)
// call the unnamed LaunchSomeAircraft (0xea4a18 in 1.01.44: movzx ecx, byte ptr [ebx+Mission]; mov eax, ebx; call, at
// 0xc294a1 and 0xc2b83d), which appends a new formation to the air list (AFId from the AFID counter's allocator; a
// generated name from ScoutCount when aName is empty) and takes its aircraft from the source formation; everything else
// the dialog does is plain field writes on formations (orders of the guard). Its code is read here: the call in both
// handlers, its error message, the five stack arguments (aTarget copied from [ebp+18h], aName [ebp+14h], aOrderedState
// [ebp+10h], aLoadout [ebp+0Ch], aSpotNow [ebp+8]; ret 14h), the allocator (inc dword ptr [ctr]; mov eax, [ctr]; ret)
// and ScoutCount's increment. The battle screen's "Order an airstrike" (sbStrikeClick) is read too: if the minute timer
// runs, PauseGame := 1, then two calls on dlgStrike (the list views' styles, Execute(nil, nil)); in a live battle the
// mod makes those calls itself and a pause request replaces PauseGame. Stand down returns the formation's torpedoes and
// missiles to its carrier (TFightingShip.AirTorpedoes / AirToSurfaceMissiles, order fields from this level). Without
// every piece the strike buttons keep their 0.3.x blocker.
static bool g_featStrike = false;
static bool g_launchHookable = false;  // LaunchSomeAircraft is the routine read here (its context hook may be installed)

static void ResolveAirStrikes() {
    uint8_t* hit;
    E.fAfName = dl::Field("TAirFormation", "Name");
    E.fAfNumber = dl::Field("TAirFormation", "AircraftNumber");
    E.fAfLoaded = dl::Field("TAirFormation", "LoadedNumber");
    E.fAfOrdered = dl::Field("TAirFormation", "OrderedState");
    E.fAfStatusCount = dl::Field("TAirFormation", "StatusCount");
    E.fAfLead = dl::Field("TAirFormation", "LeadAirFormation");
    E.fAfParent = dl::Field("TAirFormation", "ParentAirFormation");
    E.fAfGroup = dl::Field("TAirFormation", "GroupIndex");
    E.fAfTarget = dl::Field("TAirFormation", "TargetLoc");
    E.fAfLocation = dl::Field("TAirFormation", "Location");
    E.fAfLoadout = dl::Field("TAirFormation", "Loadout");
    E.fAirTorps = dl::Field("TFightingShip", "AirTorpedoes");
    E.fAirAsm = dl::Field("TFightingShip", "AirToSurfaceMissiles");
    E.fStrikeDiv = dl::Field("TdlgStrike", "TheOriginatingDivision");
    E.fStrikeLv = dl::Field("TdlgStrike", "lvAirFormations");
    E.fLvItems = dl::Field("TCustomListView", "FListItems");
    E.fItemData = dl::Field("TListItem", "FData");
    E.lvGetItem = dl::Method("TListItems", "GetItem");
    E.fLbReports = dl::Field("TfrmMain", "lbReports");
    E.strikeExecute = dl::Method("TdlgStrike", "Execute");
    void* launchClick = dl::Method("TdlgStrike", "bnLaunchClick");
    void* readyClick = dl::Method("TdlgStrike", "bnReadyClick");
    // TPointLL (iLongX, iLatY, iHeightZ: Integer) by the layout: Location .. Side and TargetLoc .. Status are 12 bytes.
    int fStatus = E.fAfStatus;
    bool points = E.fAfLocation > 0 && E.fAfSide - E.fAfLocation == 12 && E.fAfTarget > 0 && fStatus - E.fAfTarget == 12;
    bool small = E.fAfMission > 0 && E.fAfMission < 0x80 && E.fAfId > 0 && E.fAfId < 0x80 && E.fAfNumber > 0 &&
                 E.fAfNumber < 0x80 && E.fAfName > 0 && E.fAfName < 0x80 && E.fAfLoadout > 0 && E.fAfLoadout < 0x80;
    void *l1 = nullptr, *l2 = nullptr, *launch = nullptr;
    if (small) {
        std::vector<int> call = {0x0F, 0xB6, 0x4B, E.fAfMission, 0x8B, 0xC3, 0xE8};
        if (launchClick && (hit = Find(launchClick, 0x1000, call))) l1 = CallTarget(hit + 6);
        if (readyClick && (hit = Find(readyClick, 0x400, call))) l2 = CallTarget(hit + 6);
        if (l1 && l1 == l2 && InImage(U(l1))) launch = l1;
    }
    bool named = launch && RefersTo(launch, 0x300, FindLiteral(L"Error in LaunchSomeAircraft procedure!"));
    bool args = launch && Find(launch, 0x30, {0x8B, 0x75, 0x18, 0x8D, 0x7D, 0xEC, 0xA5, 0xA5, 0xA5}) &&  // aTarget
                Find(launch, 0x200, {0x8B, 0x55, 0x14}) &&                                             // aName
                Find(launch, 0x200, {0x80, 0x7D, 0x10, 0x06}) &&                                       // aOrderedState
                Find(launch, 0x200, {0x8B, 0x45, 0x0C, 0x89, 0x43, E.fAfLoadout}) &&                   // aLoadout
                Find(launch, 0x200, {0x80, 0x7D, 0x08, 0x00}) &&                                       // aSpotNow
                Find(launch, 0x300, {0xC2, 0x14, 0x00});                                               // ret 14h
    if (launch && (hit = Find(launch, 0x200, {0xE8, -1, -1, -1, -1, 0x89, 0x43, E.fAfId}))) {
        uint8_t* a = (uint8_t*)CallTarget(hit);
        if (InImage(U(a)) && a[0] == 0xFF && a[1] == 0x05 && a[6] == 0xA1 && a[11] == 0xC3 &&
            *(uint32_t*)(a + 2) == *(uint32_t*)(a + 7) && InImage(*(uint32_t*)(a + 2)))
            E.afIdCounter = (int*)(uintptr_t) * (uint32_t*)(a + 2);
    }
    if (launch && (hit = Find(launch, 0x300, {0xFF, 0x05, -1, -1, -1, -1, 0x29, 0x7E, E.fAfNumber}))) {
        uint32_t sc = *(uint32_t*)(hit + 2);
        std::vector<int> rd = {0xA1};
        Le32(rd, sc);
        if (InImage(sc) && Find(launch, 0x300, rd) && Find(launch, 0x300, {0x83, 0x7B, E.fAfName, 0x00, 0x75}))
            E.scoutCount = (int*)(uintptr_t)sc;
    }
    g_launchHookable = launch && named && args && E.afIdCounter && E.scoutCount;
    E.launchFn = g_launchHookable ? launch : nullptr;
    // sbStrikeClick: mov edx, [eax+tmrRunMinute]; cmp byte ptr [edx+FEnabled], 0; je +7; mov byte ptr [eax+PauseGame], 1;
    // mov eax, [dlgStrike cell]; mov eax, [eax]; call <styles>; mov eax, [cell]; mov eax, [eax]; xor ecx, ecx;
    // xor edx, edx; call TdlgStrike.Execute; ret
    void* sbStrike = dl::Method("TfrmMain", "sbStrikeClick");
    if (sbStrike && E.fRunTimer > 0 && E.timerEnabled > 0 && E.timerEnabled < 0x80 && E.fPause > 0) {
        std::vector<int> p = {0x8B, 0x90};
        Le32(p, (uint32_t)E.fRunTimer);
        p.insert(p.end(), {0x80, 0x7A, E.timerEnabled, 0x00, 0x74, 0x07, 0xC6, 0x80});
        Le32(p, (uint32_t)E.fPause);
        p.insert(p.end(), {0x01, 0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0xE8, -1, -1, -1, -1, 0xA1, -1, -1, -1, -1, 0x8B, 0x00,
                           0x33, 0xC9, 0x33, 0xD2, 0xE8, -1, -1, -1, -1, 0xC3});
        if ((hit = Find(sbStrike, 0x40, p)) == (uint8_t*)sbStrike && *(uint32_t*)(hit + 20) == *(uint32_t*)(hit + 32) &&
            CallTarget(hit + 42) == E.strikeExecute && InImage(U(CallTarget(hit + 26)))) {
            E.strikeDlgVar = (void**)VarFromCell(hit + 20);
            E.strikeStyles = CallTarget(hit + 26);
        }
    }
    // Execute ends with FillSquadronList(Self); Show: mov eax, ebx; call <fill>; mov eax, ebx; call TCustomForm.Show. The
    // fill lists TheOriginatingDivision's PlayerSide formations (cmp dword ptr [eax+TheOriginatingDivision], 0 first).
    if (E.strikeExecute && E.mShow && (hit = Find(E.strikeExecute, 0x200, {0x8B, 0xC3, 0xE8, -1, -1, -1, -1, 0x8B, 0xC3, 0xE8})) &&
        CallTarget(hit + 9) == E.mShow) {
        void* fill = CallTarget(hit + 2);
        std::vector<int> div = {0x83, 0xB8};
        Le32(div, (uint32_t)E.fStrikeDiv);
        div.push_back(0x00);
        std::vector<uint8_t*> t1 = launchClick ? dl::CallTargets((uint8_t*)launchClick, 0x1690) : std::vector<uint8_t*>();
        std::vector<uint8_t*> t2 = readyClick ? dl::CallTargets((uint8_t*)readyClick, 0x560) : std::vector<uint8_t*>();
        if (InImage(U(fill)) && E.fStrikeDiv > 0 && Find(fill, 0x60, div) &&
            RefersTo(fill, 0x200, FindLiteral(L"Error in SetupStrike FillSquadronList procedure!")) &&
            std::find(t1.begin(), t1.end(), (uint8_t*)fill) != t1.end() &&
            std::find(t2.begin(), t2.end(), (uint8_t*)fill) != t2.end())
            E.strikeFill = fill;
    }
    // Stand down: mov eax, [ebx+AircraftNumber]; mov [ebx+StatusCount], eax; xor edx, edx; mov eax, ebx; call <return>,
    // which adds the formation's weapons to ScenHomeBase's AirTorpedoes / AirToSurfaceMissiles (add [eax+field], edx).
    bool standDown = false;
    void* sd = dl::Method("TdlgStrike", "bnStandDownClick");
    if (sd && E.fAfStatusCount > 0 && E.fAirTorps > 0 && E.fAirAsm > 0) {
        std::vector<int> p = {0x8B, 0x43, E.fAfNumber, 0x89, 0x83};
        Le32(p, (uint32_t)E.fAfStatusCount);
        p.insert(p.end(), {0x33, 0xD2, 0x8B, 0xC3, 0xE8});
        if ((hit = Find(sd, 0x120, p))) {
            void* ret = CallTarget(hit + 13);
            std::vector<int> t = {0x01, 0x90}, m = {0x01, 0x90};
            Le32(t, (uint32_t)E.fAirTorps);
            Le32(m, (uint32_t)E.fAirAsm);
            standDown = InImage(U(ret)) && Find(ret, 0x50, t) && Find(ret, 0x50, m);
        }
    }
    bool fields = points && E.fAfLoaded > 0 && E.fAfOrdered > 0 && E.fAfStatusCount > 0 && E.fAfLead > 0 &&
                  E.fAfParent > 0 && E.fAfGroup > 0 && E.fAfConfirm > 0 && E.fAfSide > 0 && E.fAfStatus > 0;
    bool dialog = E.strikeDlgVar && E.strikeStyles && E.strikeExecute && E.strikeFill && E.fStrikeLv > 0;
    g_featStrike = g_launchHookable && dialog && standDown && fields && E.airVar && E.mHide;  // (and the hooks: EnsureHooks)
    Log("lockstep resolve (air strikes): %s (LaunchSomeAircraft %p: both dialog calls %d, error message %d, stack "
        "arguments %d; AFID counter %p, ScoutCount %p; dlgStrike %p, styles %p, Execute %p, FillSquadronList %p; Stand "
        "down's weapons return %d; TPointLL 12 bytes %d; fields %d)",
        g_featStrike ? "on" : "OFF", launch, launch ? 1 : 0, named ? 1 : 0, args ? 1 : 0, E.afIdCounter, E.scoutCount,
        E.strikeDlgVar, E.strikeStyles, E.strikeExecute, E.strikeFill, standDown ? 1 : 0, points ? 1 : 0, fields ? 1 : 0);
}

// Missiles in live battles (engine level 9; limits-weapons §2, §3). Every piece is read from the game's code here; a
// piece that is not as expected turns its feature off (the 0.4.0 rules then stay: both sides fire at will under the
// computer's rules with the panel locked, and Manual fire keeps its blocker).
// - Each player's own Fire at will (g_featFireAtWill): CheckMissileFiringForAllShips' gate per division with ships and
//   a force: mov eax, [ebp-8]; mov eax, [eax+cbFireAtWill]; mov edx, [eax]; call [edx+GetChecked]; test al, al; jne <fire>;
//   mov eax, [esi+myForce]; cmp byte ptr [eax+AIControlled], 0; je <side test>; mov eax, esi; call GetFlagship;
//   imul eax, [eax+Nation], <n>; mov edx, [<nations cell>]; cmp dword ptr [edx+eax*2+<field>], <min>; jl <fire>;
//   <side test>: mov eax, [esi+Side]; mov edx, [<PlayerSide cell>]; cmp eax, [edx]; je <skip>;
//   <fire>: lea eax, [ebp-4]; mov edx, [esi+Name]; call <a string copy>; cmp byte ptr [esi+HoldMissileFire], 0; jne <skip>.
//   Both skips go to the same place, so a division held by HoldMissileFire is skipped exactly as the player's division
//   with the box unchecked; with the simulation PlayerSide 2 (no side matches) every path reaches <fire>, so the box's
//   value never decides anything.
// - Manual salvoes (g_featMissile): FireMissileMount's preconditions (it fires nothing and changes nothing when one
//   fails), its count min(Tubes, NumberToFire) and result; FireSSM's head; the dialog's commit FireMissiles (its shooter
//   list, the nested per-mount procedure that writes PendingMissileTarget or calls FireMissileMount, the submarine
//   branch: FireSSM with side 0, Downtime := 5000, the item unchecked); Manual fire's handler sbFireSalvoClick (PauseGame
//   if the minute timer runs, the dialog's three fills, ShowModal); FillShooterList's submarine list.
static bool g_featFireAtWill = false, g_featMissile = false;
static bool g_mslHookable = false;  // FireMissileMount is the routine read here (its hook may be installed)
static bool g_ssmHookable = false;  // FireSSM likewise

static void ResolveMissiles() {
    uint8_t* hit;
    E.fHoldMsl = dl::Field("TDivision", "HoldMissileFire");
    E.fCbFireAtWill = dl::Field("TfrmMain", "cbFireAtWill");
    E.fRgDoctrine = dl::Field("TfrmMain", "rgMissileDoctrine");
    E.fCbState = dl::Field("TCustomCheckBox", "FState");
    E.fRgButtons = dl::Field("TCustomRadioGroup", "FButtons");
    E.fCtlHint = dl::Field("TControl", "FHint");
    E.fCtlShowHint = dl::Field("TControl", "FShowHint");
    E.fCtlParentShowHint = dl::Field("TControl", "FParentShowHint");
    E.fCtlParent = dl::Field("TControl", "FParent");
    E.fCtlEnabled = dl::Field("TControl", "FEnabled");
    E.fShipNation = dl::Field("Tship", "Nation");
    bool gate = false;
    if (E.missileFire && InImage(U(E.missileFire)) &&
        (hit = Find(E.missileFire, 0x200, {0x8B, 0x45, 0xF8, 0x8B, 0x80, -1, -1, -1, -1, 0x8B, 0x10, 0xFF, 0x92, -1, -1, -1,
                                          -1, 0x84, 0xC0, 0x75, -1, 0x8B, 0x46, -1, 0x80, 0x78, -1, 0x00, 0x74, -1, 0x8B,
                                          0xC6, 0xE8, -1, -1, -1, -1, 0x69, 0x40, -1, -1, -1, -1, -1, 0x8B, 0x15, -1, -1,
                                          -1, -1, 0x83, 0xBC, 0x42, -1, -1, -1, -1, -1, 0x7C, -1, 0x8B, 0x46, -1, 0x8B,
                                          0x15, -1, -1, -1, -1, 0x3B, 0x02, 0x0F, 0x84, -1, -1, -1, -1, 0x8D, 0x45, 0xFC,
                                          0x8B, 0x56, -1, 0xE8, -1, -1, -1, -1, 0x80, 0x7E, -1, 0x00, 0x0F, 0x85}))) {
        uint8_t* fire = hit + 77;
        uint8_t* skipSide = hit + 77 + *(int32_t*)(hit + 73);
        uint8_t* skipHold = hit + 98 + *(int32_t*)(hit + 94);
        gate = *(int32_t*)(hit + 5) == E.fCbFireAtWill && hit + 21 + (int8_t)hit[20] == fire && hit[23] == E.fMyForce &&
               hit[26] == E.fFAI && hit + 30 + (int8_t)hit[29] == hit + 60 && CallTarget(hit + 32) == E.getFlagship &&
               hit[39] == E.fShipNation && hit + 60 + (int8_t)hit[59] == fire && hit[62] == E.fDivSide &&
               VarFromCell(hit + 65) == (void*)E.playerSideVar && hit[90] == E.fHoldMsl && skipSide == skipHold;
        if (gate) {
            E.nations = (uint8_t*)VarFromCell(hit + 46);
            E.nationStride = *(int32_t*)(hit + 40) * 2;
            E.nationGate = *(int32_t*)(hit + 53);
            E.nationGateMin = (int8_t)hit[57];
        }
    }
    g_featFireAtWill = gate && E.nations && E.nationStride > 0 && E.nationGate > 0 && E.fCbState > 0 && E.fShips >= 0 &&
                       E.fDivSide >= 0 && E.fMyForce >= 0 && E.fFAI >= 0 && E.fCtlEnabled > 0;  // (and the hooks)

    // FireMissileMount: mov esi, [ebp+10h]; lea edi, [ebp-24h]; movsd x3 (aTargetLocation); mov ebx, ecx (aShip);
    // mov esi, edx (aMount); mov [ebp-4], eax (Self); then cmp byte ptr [ebx+ElectricDisabled], 0; jne <exit>;
    // cmp byte ptr [esi+Destroyed], 0; jne <exit>; cmp dword ptr [esi+TorpsLeft], 0; jle <exit>; mov edx, [ebp+0Ch]
    // (TargetBearing); mov eax, esi; call MountCanFire; test al, al; jne; cmp byte ptr [ebx+ShipType], <type>; jne <exit>;
    // lea edx, [ebp-24h]; mov eax, ebx; call GetDistanceToPoint; mov edi, eax; movzx edx, byte ptr [esi+MountType];
    // mov eax, [ebp-4]; call GetMissileMinimumRange; cmp edi, eax; jle <exit>; lea edx, [ebp-24h]; mov eax, ebx;
    // call GetDistanceToPoint; mov edi, eax; movzx edx, byte ptr [esi+MountType]; mov eax, [ebx+Nation];
    // call <maximum range>; cmp edi, eax; jge <exit>. The count: mov eax, [esi+Tubes]; cmp eax, [ebp+8] (NumberToFire);
    // jle; mov eax, [ebp+8]; test eax, eax; jle. The result: mov eax, [ebp-8]; pop edi; pop esi; pop ebx; mov esp, ebp;
    // pop ebp; ret 10h.
    E.fireMissileMount = dl::Method("TfrmMain", "FireMissileMount");
    E.fireSSM = dl::Method("TfrmMain", "FireSSM");
    E.fireMissiles = dl::Method("TdlgMissileFire", "FireMissiles");
    E.mountCanFire = dl::Method("TFightingTorpedoMount", "MountCanFire");
    E.distToPoint = dl::Method("TFightingShip", "GetDistanceToPoint");
    E.minRange = dl::Method("TfrmMain", "GetMissileMinimumRange");
    E.fElectric = dl::Field("TFightingShip", "ElectricDisabled");
    E.fPendingMsl = dl::Field("TFightingShip", "PendingMissileTarget");
    uint8_t* fmm = (uint8_t*)E.fireMissileMount;
    bool head = false, count = false, tail = false;
    if (fmm && InImage(U(fmm)) && E.fMountType > 0 && E.fMountType < 0x80 && E.fShipNation > 0 && E.fShipNation < 0x80 &&
        Find(fmm, 0x30, {0x8B, 0x75, 0x10, 0x8D, 0x7D, 0xDC, 0xA5, 0xA5, 0xA5, 0x8B, 0xD9, 0x8B, 0xF2, 0x89, 0x45, 0xFC}) &&
        (hit = Find(fmm, 0x100, {0x80, 0xBB, -1, -1, -1, -1, 0x00, 0x0F, 0x85, -1, -1, -1, -1, 0x80, 0xBE, -1, -1, -1, -1,
                               0x00, 0x0F, 0x85, -1, -1, -1, -1, 0x83, 0xBE, -1, -1, -1, -1, 0x00, 0x0F, 0x8E, -1, -1,
                               -1, -1, 0x8B, 0x55, 0x0C, 0x8B, 0xC6, 0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x75, -1, 0x80,
                               0xBB, -1, -1, -1, -1, -1, 0x0F, 0x85, -1, -1, -1, -1, 0x8D, 0x55, 0xDC, 0x8B, 0xC3, 0xE8,
                               -1, -1, -1, -1, 0x8B, 0xF8, 0x0F, 0xB6, 0x56, -1, 0x8B, 0x45, 0xFC, 0xE8, -1, -1, -1, -1,
                               0x3B, 0xF8, 0x0F, 0x8E, -1, -1, -1, -1, 0x8D, 0x55, 0xDC, 0x8B, 0xC3, 0xE8, -1, -1, -1,
                               -1, 0x8B, 0xF8, 0x0F, 0xB6, 0x56, -1, 0x8B, 0x43, -1, 0xE8, -1, -1, -1, -1, 0x3B, 0xF8,
                               0x0F, 0x8D}))) {
        auto exitOf = [&](int jcc) { return hit + jcc + 6 + *(int32_t*)(hit + jcc + 2); };  // a jcc rel32 at hit + jcc
        uint8_t* ex = exitOf(7);
        head = *(int32_t*)(hit + 2) == E.fElectric && *(int32_t*)(hit + 15) == E.fMountDestroyed &&
               *(int32_t*)(hit + 28) == E.fTorpsLeft && CallTarget(hit + 44) == E.mountCanFire &&
               *(int32_t*)(hit + 55) == E.fShipType && CallTarget(hit + 71) == E.distToPoint && hit[81] == E.fMountType &&
               CallTarget(hit + 85) == E.minRange && CallTarget(hit + 103) == E.distToPoint && hit[113] == E.fMountType &&
               hit[116] == E.fShipNation && InImage(U(CallTarget(hit + 117))) && exitOf(20) == ex && exitOf(33) == ex &&
               exitOf(60) == ex && exitOf(92) == ex && exitOf(124) == ex;
        if (head) {
            E.mslAnyBearing = hit[59];
            E.maxRange = CallTarget(hit + 117);
        }
    }
    if (head && E.fTubes > 0 && E.fTubes < 0x80) {
        count = Find(fmm, 0x300, {0x8B, 0x46, E.fTubes, 0x3B, 0x45, 0x08, 0x7E, 0x03, 0x8B, 0x45, 0x08, 0x85, 0xC0, 0x0F,
                                  0x8E}) != nullptr;
        tail = Find(fmm, 0x400, {0x8B, 0x45, 0xF8, 0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 0x10, 0x00}) != nullptr;
    }
    g_mslHookable = head && count && tail && E.fElectric > 0 && E.fTorpsLeft > 0 && E.fMountDestroyed > 0;
    // FireSSM(Self, aSide, aFiringLocation; aFiringShipName, aTarget, aBearing, aSpeed, aRange, aCalibre, aDisruption),
    // ret 1Ch: mov esi, ecx; lea edi, [ebp-10h]; movsd x3 (the location); mov [ebp-4], edx (the side)
    g_ssmHookable = E.fireSSM && InImage(U(E.fireSSM)) &&
                    Find(E.fireSSM, 0x20, {0x8B, 0xF1, 0x8D, 0x7D, 0xF0, 0xA5, 0xA5, 0xA5, 0x89, 0x55, 0xFC}) &&
                    Find(E.fireSSM, 0x120, {0xC2, 0x1C, 0x00});

    // The submarines: FillShooterList's second loop (mov eax, [<cell>]; mov eax, [eax]; mov esi, [eax+8]; ... TList.Get;
    // mov ebx, eax; mov eax, [ebx+Side]; mov edx, [<PlayerSide cell>]; cmp eax, [edx]; jne; cmp byte ptr [ebx+SubType], 4;
    // jne; cmp dword ptr [ebx+Downtime], 0)
    E.fSubSide = dl::Field("TSubmarine", "Side");
    E.fSubType = dl::Field("TSubmarine", "SubType");
    E.fSubDowntime = dl::Field("TSubmarine", "Downtime");
    E.fSubLocation = dl::Field("TSubmarine", "Location");
    E.fSubTorpsFired = dl::Field("TSubmarine", "TorpsFired");
    E.fTorpCalibre = dl::Field("TTorpedo", "Calibre");
    E.fTorpSide = dl::Field("TTorpedo", "Side");
    E.mslFillTargets = dl::Method("TdlgMissileFire", "FillMissileTargetList");
    E.mslFillShooters = dl::Method("TdlgMissileFire", "FillShooterList");
    E.mslTechText = dl::Method("TdlgMissileFire", "FillTechExplanation");
    E.mslTargetsClick = dl::Method("TdlgMissileFire", "lbMissileTargetsClick");
    E.mslFireOne = dl::Method("TdlgMissileFire", "bnFireOneClick");
    E.mslFireHalf = dl::Method("TdlgMissileFire", "bnfireHalfClick");
    E.mslFireAll = dl::Method("TdlgMissileFire", "bnFireAllClick");
    E.fMslTargets = dl::Field("TdlgMissileFire", "lbMissileTargets");
    E.fMslShooters = dl::Field("TdlgMissileFire", "lvMissileShooters");
    if (E.mslFillShooters && E.fSubSide > 0 && E.fSubSide < 0x80 && E.fSubType > 0 && E.fSubType < 0x80 &&
        E.fSubDowntime > 0 && E.fSubDowntime < 0x80 &&
        (hit = Find(E.mslFillShooters, 0x300, {0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x8B, 0x70, 0x08, 0x4E, 0x85, 0xF6, 0x7C,
                                              -1, 0x46, 0xC7, 0x45, -1, 0x00, 0x00, 0x00, 0x00, 0xA1, -1, -1, -1, -1, 0x8B,
                                              0x00, 0x8B, 0x55, -1, 0xE8, -1, -1, -1, -1, 0x8B, 0xD8, 0x8B, 0x43, E.fSubSide,
                                              0x8B, 0x15, -1, -1, -1, -1, 0x3B, 0x02, 0x75, -1, 0x80, 0x7B, E.fSubType, 0x04,
                                              0x75, -1, 0x83, 0x7B, E.fSubDowntime, 0x00})) &&
        *(uint32_t*)(hit + 1) == *(uint32_t*)(hit + 24) && VarFromCell(hit + 45) == (void*)E.playerSideVar)
        E.subsVar = (void**)VarFromCell(hit + 1);

    // FireMissiles(aNumberToFire): the shooter list: mov eax, [ebp-40h]; mov eax, [eax+lvMissileShooters];
    // mov eax, [eax+FListItems]; call <Count>; ... mov edx, [ebp-44h]; call TListItems.GetItem; mov edi, eax; mov eax, edi;
    // call <TListItem.Checked>; test al, al; je; ... mov ebx, [edi+Data]; mov eax, ebx; mov edx, [<TFightingShip>]
    uint8_t* fm = (uint8_t*)E.fireMissiles;
    bool list = false, ship = false, pmt = false, sub = false, subTest = false;
    if (fm && InImage(U(fm)) && E.fMslShooters > 0 && E.fLvItems > 0) {
        std::vector<int> p = {0x8B, 0x45, 0xC0, 0x8B, 0x80};
        Le32(p, (uint32_t)E.fMslShooters);
        p.insert(p.end(), {0x8B, 0x80});
        Le32(p, (uint32_t)E.fLvItems);
        p.push_back(0xE8);
        if ((hit = Find(fm, 0x180, p))) E.lvCount = CallTarget(hit + 15);
        if ((hit = Find(fm, 0x200, {0x8B, 0x55, 0xBC, 0xE8, -1, -1, -1, -1, 0x8B, 0xF8, 0x8B, 0xC7, 0xE8, -1, -1, -1, -1,
                                    0x84, 0xC0, 0x0F, 0x84})) &&
            CallTarget(hit + 3) == E.lvGetItem)
            E.lvItemChecked = CallTarget(hit + 12);
        list = E.lvCount && InImage(U(E.lvCount)) && E.lvItemChecked && InImage(U(E.lvItemChecked));
        ship = E.fItemData > 0 && E.fItemData < 0x80 && Find(fm, 0x200, {0x8B, 0x5F, E.fItemData, 0x8B, 0xC3, 0x8B, 0x15});
        // the heavy SSM mounts: cmp byte ptr [eax+MountType], 4; jne; push ebp; call <the per-mount procedure>, which
        // writes the ship's PendingMissileTarget (mov [eax+PMT], ecx; mov ecx, [edx-8]; mov [eax+PMT+4], ecx;
        // mov ecx, [edx-4]; mov [eax+PMT+8], ecx) and calls FireMissileMount
        if (E.fPendingMsl > 0 && (hit = Find(fm, 0x300, {0x80, 0x78, E.fMountType, 0x04, 0x75, -1, 0x55, 0xE8}))) {
            uint8_t* nested = (uint8_t*)CallTarget(hit + 7);
            std::vector<int> w = {0x89, 0x88};
            Le32(w, (uint32_t)E.fPendingMsl);
            w.insert(w.end(), {0x8B, 0x4A, 0xF8, 0x89, 0x88});
            Le32(w, (uint32_t)E.fPendingMsl + 4);
            w.insert(w.end(), {0x8B, 0x4A, 0xFC, 0x89, 0x88});
            Le32(w, (uint32_t)E.fPendingMsl + 8);
            std::vector<uint8_t*> calls = InImage(U(nested)) ? dl::CallTargets(nested, 0x1a0) : std::vector<uint8_t*>();
            pmt = InImage(U(nested)) && Find(nested, 0x100, w) &&
                  std::find(calls.begin(), calls.end(), (uint8_t*)E.fireMissileMount) != calls.end();
        }
        // the submarine branch: mov eax, ebx; mov edx, [<TSubmarine>]; call <is>; test al, al; je; ... xor edx, edx;
        // call FireSSM; add dword ptr [ebp-38h], 2; mov dword ptr [esi+Downtime], 5000; xor edx, edx; mov eax, edi;
        // call <TListItem.SetChecked>
        for (int k = 0; k < 4 && !subTest; k++)
            if ((hit = Find(fm, 0x400, {0x8B, 0xC3, 0x8B, 0x15, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x0F, 0x84}, k)))
                subTest = InImage(*(uint32_t*)(hit + 4)) && **(void***)(hit + 4) == dl::ClassVmt("TSubmarine");
        if (E.fSubDowntime > 0 && E.fSubDowntime < 0x80 &&
            (hit = Find(fm, 0x600, {0x33, 0xD2, 0xE8, -1, -1, -1, -1, 0x83, 0x45, 0xC8, 0x02, 0xC7, 0x46, E.fSubDowntime,
                                    0x88, 0x13, 0x00, 0x00, 0x33, 0xD2, 0x8B, 0xC7, 0xE8})) &&
            CallTarget(hit + 2) == E.fireSSM) {
            E.lvItemSetChecked = CallTarget(hit + 22);
            sub = subTest && InImage(U(E.lvItemSetChecked));
        }
    }
    // Manual fire (sbFireSalvoClick): add esp, -0Ch; mov edx, [eax+tmrRunMinute]; cmp byte ptr [edx+FEnabled], 0; je +7;
    // mov byte ptr [eax+PauseGame], 1; mov eax, [<dlgMissileFire cell>]; mov eax, [eax]; call FillMissileTargetList;
    // mov eax, esp; call <EmptyPoint>; mov edx, esp; mov eax, [cell]; mov eax, [eax]; xor ecx, ecx; call FillShooterList;
    // mov eax, [cell]; mov eax, [eax]; call FillTechExplanation; mov eax, [cell]; mov eax, [eax]; mov edx, [eax];
    // call [edx+ShowModal]; add esp, 0Ch; ret
    bool salvo = false;
    void* salvoClick = dl::Method("TfrmMain", "sbFireSalvoClick");
    if (salvoClick && E.fRunTimer > 0 && E.timerEnabled > 0 && E.timerEnabled < 0x80 && E.fPause > 0) {
        std::vector<int> p = {0x83, 0xC4, 0xF4, 0x8B, 0x90};
        Le32(p, (uint32_t)E.fRunTimer);
        p.insert(p.end(), {0x80, 0x7A, E.timerEnabled, 0x00, 0x74, 0x07, 0xC6, 0x80});
        Le32(p, (uint32_t)E.fPause);
        p.insert(p.end(), {0x01, 0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0xE8, -1, -1, -1, -1, 0x8B, 0xC4, 0xE8, -1, -1, -1, -1,
                           0x8B, 0xD4, 0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x33, 0xC9, 0xE8, -1, -1, -1, -1, 0xA1, -1, -1, -1,
                           -1, 0x8B, 0x00, 0xE8, -1, -1, -1, -1, 0xA1, -1, -1, -1, -1, 0x8B, 0x00, 0x8B, 0x10, 0xFF, 0x92,
                           -1, -1, -1, -1, 0x83, 0xC4, 0x0C, 0xC3});
        if ((hit = Find(salvoClick, 0x60, p)) == (uint8_t*)salvoClick) {
            uint32_t cell = *(uint32_t*)(hit + 23);
            salvo = *(uint32_t*)(hit + 44) == cell && *(uint32_t*)(hit + 58) == cell && *(uint32_t*)(hit + 70) == cell &&
                    CallTarget(hit + 29) == E.mslFillTargets && CallTarget(hit + 52) == E.mslFillShooters &&
                    CallTarget(hit + 64) == E.mslTechText;
            if (salvo) E.mslDlgVar = (void**)VarFromCell(hit + 23);
        }
    }
    bool order = E.fPendingMsl > 0 && dl::Field("TFightingShip", "Sunk") - E.fPendingMsl == 12;  // a 12-byte TPointLL
    g_featMissile = g_mslHookable && g_ssmHookable && list && ship && pmt && sub && salvo && order && E.mslDlgVar &&
                    E.fMslShooters > 0 && E.fSunk > 0 && E.fShipSide >= 0 && E.torpVar;  // (and the hooks: EnsureHooks)
    Log("lockstep resolve (missiles): fire at will per side %s (gate %d: nations %p, record %d bytes, field +%#x < %d "
        "fires; HoldMissileFire +%#x), manual salvoes %s (FireMissileMount %p: head %d, count %d, result %d, any-bearing "
        "ship type %d, maximum range %p; FireSSM %p %d; dialog: list %d, data %d, pending target %d, submarine branch %d; "
        "Manual fire %d, dlgMissileFire %p; PendingMissileTarget +%#x, 12 bytes %d), submarines %p",
        g_featFireAtWill ? "on" : "OFF", gate ? 1 : 0, E.nations, E.nationStride, E.nationGate, E.nationGateMin, E.fHoldMsl,
        g_featMissile ? "on" : "OFF", E.fireMissileMount, head ? 1 : 0, count ? 1 : 0, tail ? 1 : 0, E.mslAnyBearing,
        E.maxRange, E.fireSSM, g_ssmHookable ? 1 : 0, list ? 1 : 0, ship ? 1 : 0, pmt ? 1 : 0, sub ? 1 : 0, salvo ? 1 : 0,
        E.mslDlgVar, E.fPendingMsl, order ? 1 : 0, E.subsVar);
}

// The campaign date (with the session API below): resolved with the engine, hashed, and kept after every live load.
static bool ResolveDate();
static bool CampaignDateNow(int32_t ymd[3]);
static bool KeepCampaignDate(const char* what);

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
    if (hit) {
        E.msgDlg = CallTarget(hit + 22);
        E.messageDlg = hit;
    }
    Need(E.msgDlg && InImage((uintptr_t)E.msgDlg), "MessageDlgPosHelp");
    // ShowMessagePos: push 0; push edx; push ecx; push 0; mov dl, 4; movzx ecx, word ptr [buttons]; call
    // MessageDlgPosHelp; ret (no frame: a box's own caller is the next return address; BoxSiteSide)
    for (uint8_t* p = (uint8_t*)dl::g_base + 0x1000; E.msgDlg && !E.smpRet;) {
        p = dl::FindPattern(p, (size_t)((uint8_t*)dl::g_base + dl::g_size - 32 - p),
                            {0x6A, 0x00, 0x52, 0x51, 0x6A, 0x00, 0xB2, 0x04, 0x0F, 0xB7, 0x0D, -1, -1, -1, -1, 0xE8, -1,
                             -1, -1, -1, 0xC3});
        if (!p) break;
        if (CallTarget(p + 15) == E.msgDlg) E.smpRet = U(p + 20);
        p++;
    }
    if (!E.smpRet) Log("lockstep: ShowMessagePos not found: the sighting boxes go to the first player (as before)");

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
    ResolveDate();  // (both PCs: the state hash covers the campaign date from engine level 10)
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
    // TFightingShip.Create: mov byte ptr [ebx+IdentifiedAsType], imm8 (0x16 in 1.01.44): every ship's type after a load
    if (g_fIdType > 0 && g_fIdType < 0x10000 &&
        (hit = Find(dl::Method("TFightingShip", "Create"), 0x400,
                    {0xC6, 0x83, g_fIdType & 0xFF, (g_fIdType >> 8) & 0xFF, 0x00, 0x00, -1})))
        g_idTypeDefault = hit[6];
    else
        Log("lockstep: WARNING the ships' initial identification was not found in TFightingShip.Create: after a load "
            "the simulation keeps the identification this PC's display drew");
    E.fCbSpeed = dl::Field("TfrmMain", "cbGameSpeed");
    E.fTvShipsTree = E.fTvShips;
    // UpDown1Click (the arrows next to the speed box), down arrow: cmp byte ptr [eax+FDown], 0; je +7; mov eax, ebx;
    // call <the Fastest available button up again>
    if ((hit = Find(dl::Method("TfrmMain", "UpDown1Click"), 0x80,
                    {0x80, 0xB8, -1, -1, 0x00, 0x00, 0x00, 0x74, 0x07, 0x8B, 0xC3, 0xE8}))) {
        E.releaseFastest = CallTarget(hit + 11);
        E.fDownOff = *(int32_t*)(hit + 2);
    }
    E.fFastestBtn = dl::Field("TfrmMain", "sbFastestAvailable");
    // The release routine: mov eax, [ebx+sbFastestAvailable]; xor edx, edx; call TSpeedButton.SetDown
    if (E.releaseFastest && (hit = Find(E.releaseFastest, 0x20, {0x33, 0xD2, 0xE8}))) E.setDown = CallTarget(hit + 2);
    E.fGroupOff = dl::Field("TCustomSpeedButton", "FGroupIndex");
    if (int fd = dl::Field("TCustomSpeedButton", "FDown"); fd >= 0 && fd != E.fDownOff) {
        Log("lockstep: TSpeedButton.FDown at +%x (RTTI), +%x in UpDown1Click: the button helpers are off", fd, E.fDownOff);
        E.setDown = nullptr;
    }
    // sbAutoRunBattleClick: mov eax, [eax+control]; xor edx, edx; mov ecx, [eax]; call [ecx+SetEnabled]
    if ((hit = Find(dl::Method("TfrmMain", "sbAutoRunBattleClick"), 0x200, {0x33, 0xD2, 0x8B, 0x08, 0xFF, 0x91, -1, -1, 0x00, 0x00})))
        E.setEnabledSlot = *(int32_t*)(hit + 6);
    // TfrmMain.FormClose clears the battle's data before the screen goes: mov cl, 1; mov edx, 2; mov eax, [ebp-4];
    // call ClearData; call <free the battle's air formations>
    if ((hit = Find(dl::Method("TfrmMain", "FormClose"), 0x600,
                    {0xB1, 0x01, 0xBA, 0x02, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xFC, 0xE8, -1, -1, -1, -1, 0xE8})) &&
        CallTarget(hit + 10) == dl::Method("TfrmMain", "ClearData")) {
        E.mClearData = CallTarget(hit + 10);
        E.airCleanup = CallTarget(hit + 15);
    }
    // cbGameSpeedChange: mov eax, [ebx+cbGameSpeed]; mov edx, [eax]; call [edx+GetItemIndex]
    if ((hit = Find(speed, 0x40, {0x8B, 0x83, -1, -1, -1, -1, 0x8B, 0x10, 0xFF, 0x92, -1, -1, -1, -1})))
        g_speedGetIdx = *(int*)(hit + 10);
    ResolveLiveRules();
    ResolveForceOrders();
    ResolveAutoRun();
    ResolveTorpedoes();
    ResolveAirOps();
    ResolveAirStrikes();
    ResolveMissiles();
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
enum { G_POS, G_DMG, G_AMMO, G_SIGHT, G_IDENT, G_ORD, G_AIR, G_TORP, G_FORCE, G_SUB, G_COUNT };
static const char* kGroupNames[G_COUNT] = {"pos", "dmg", "ammo", "sight", "ident", "ord", "air", "torp", "force", "sub"};

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
    {"TFightingShip", "LightState", 4, G_SIGHT},  // engine level 14 (the guard keeps the display's writes out)
    {"TFightingShip", "IdentifiedAsType", 1, G_IDENT}, {"TFightingShip", "IdentifiedAsClass", -1, G_IDENT},
    {"TFightingShip", "TargetMain", -2, G_ORD}, {"TFightingShip", "ManuallySelectedTarget", 1, G_ORD},
    // engine level 7: the aircraft weapons aboard (the simulation arms and lands, Stand down returns them: an order)
    {"TFightingShip", "AirTorpedoes", 4, G_AMMO}, {"TFightingShip", "AirToSurfaceMissiles", 4, G_AMMO},
    // engine level 9: the heavy SSM target the missile dialog sets when a launcher can't bear (an order; the simulation
    // fires it in FireAllGuns and clears it). A TPointLL: 8 bytes (its third dword is not set in empty points).
    {"TFightingShip", "PendingMissileTarget", 8, G_ORD},
};
static FieldSpec g_divFields[] = {
    {"TDivision", "DivisionCourse", 4, G_ORD}, {"TDivision", "DivisionSpeed", 4, G_ORD},
    {"TDivision", "Side", 4, G_ORD}, {"TDivision", "Formation", 1, G_ORD}, {"TDivision", "AIControlled", 1, G_ORD},
    {"TDivision", "TacticalStance", 1, G_ORD}, {"TDivision", "TurnTogether", 1, G_ORD},
    {"TDivision", "TargetDiv", -2, G_ORD}, {"TDivision", "DivisionObjective", 8, G_ORD},
    {"TDivision", "WithdrawMe", 1, G_ORD}, {"TDivision", "InCombat", 1, G_ORD},
    // the airfield's land-CAP assignment (CheckLandBasedCAP) or the division report's CAP choice (its OK, an order)
    {"TDivision", "CappedDivision", -2, G_ORD},
    // the order of battle (force orders): a missed or wrong replay of a change force / lead / turn-away shows here
    {"TDivision", "myForce", -2, G_FORCE}, {"TDivision", "LeadDivision", -2, G_FORCE}, {"TDivision", "Role", 1, G_FORCE},
    {"TDivision", "ForceLeadDivision", 1, G_FORCE}, {"TDivision", "Admin", 4, G_FORCE},
};
// Per force (forces list order), with its name and its divisions' names in list order. Not ForceFlagship: a cache that
// every GetForceFlagship call rewrites, display code too (the Force orders dialog, the change-force menu, the routine air
// ops dialog); the simulation's one direct read (EOTP 0xdefdbf in 1.01.44, the computer's forces following their main
// force) comes after StartMinute's CheckObjectives has called GetForceFlagship for every force (0xdab816). Engine level
// 6: the routine air ops settings (orders) and the search countdowns the simulation keeps per sector (size 0: a static
// array sized from the layout, ForceArraySize).
static FieldSpec g_forceFields[] = {
    {"TForce", "Side", 4, G_FORCE},          {"TForce", "HasChangedLead", 1, G_FORCE},
    {"TForce", "TimeToTurnAway", 4, G_FORCE}, {"TForce", "ForceWithdraw", 4, G_FORCE},
    {"TForce", "AIControlled", 1, G_FORCE},   {"TForce", "Rallied", 1, G_FORCE},
    {"TForce", "FlotillaAttack", 1, G_FORCE}, {"TForce", "ForceMission", 1, G_FORCE},
    {"TForce", "CapLevel", 4, G_ORD},         {"TForce", "NightCAP", 1, G_ORD},
    {"TForce", "SearchLimitLeft", 4, G_ORD},  {"TForce", "SearchLimitRight", 4, G_ORD},
    {"TForce", "SearchDistance", 4, G_ORD},   {"TForce", "SearchInterval", 4, G_ORD},
    {"TForce", "TwoPhaseSearch", 1, G_ORD},   {"TForce", "RepeatSearch", 1, G_ORD},
    {"TForce", "NightSearch", 1, G_ORD},      {"TForce", "SearchPatternOrdered", 0, G_ORD},
    {"TForce", "SearchPatternCurrent", 0, G_AIR},
};
static FieldSpec g_airFields[] = {
    {"TAirFormation", "AFId", 4, G_AIR}, {"TAirFormation", "AircraftNumber", 4, G_AIR},
    {"TAirFormation", "Location", 8, G_AIR}, {"TAirFormation", "Course", 4, G_AIR},
    {"TAirFormation", "Status", 1, G_AIR}, {"TAirFormation", "RemainingFuel", 4, G_AIR},
    {"TAirFormation", "Altitude", 4, G_AIR}, {"TAirFormation", "AircraftDestroyed", 4, G_AIR},
    // engine level 7: the strike dialog's fields (orders of the guard) and the formation links; a missed or wrong launch
    // replay or strike order shows here at the next boundary
    {"TAirFormation", "StatusCount", 4, G_AIR}, {"TAirFormation", "ParentAirFormation", -2, G_AIR},
    {"TAirFormation", "Mission", 1, G_ORD}, {"TAirFormation", "Loadout", 4, G_ORD},
    {"TAirFormation", "LoadedNumber", 4, G_ORD}, {"TAirFormation", "OrderedState", 1, G_ORD},
    {"TAirFormation", "GroupIndex", 4, G_ORD}, {"TAirFormation", "LeadAirFormation", -2, G_ORD},
    {"TAirFormation", "CombinedStrikeSpeed", 4, G_ORD}, {"TAirFormation", "PriorityTarget", 1, G_ORD},
    {"TAirFormation", "ConfirmTargetLoc", 1, G_ORD}, {"TAirFormation", "TargetLoc", 8, G_ORD},
};
static FieldSpec g_torpFields[] = {
    {"TTorpedo", "TorpedoLocation", 8, G_TORP}, {"TTorpedo", "CourseGU", 4, G_TORP},
    {"TTorpedo", "RemainingRangeGU", 4, G_TORP}, {"TTorpedo", "Side", 4, G_TORP},
    // engine level 5: what the battle save keeps of a torpedo (only the simulation, the fire routines and loads write it)
    {"TTorpedo", "Speed", 4, G_TORP}, {"TTorpedo", "Calibre", 4, G_TORP}, {"TTorpedo", "RunDist", 4, G_TORP},
    {"TTorpedo", "TorpedoReliability", 4, G_TORP}, {"TTorpedo", "CheckedFail", 1, G_TORP},
    {"TTorpedo", "Close", 1, G_TORP}, {"TTorpedo", "SettingLow", 1, G_TORP},
};
// Per ship, its torpedo and missile mounts in Tship.TorpedomountList order (engine level 5, group ammo): a missed or
// wrong replay of a torpedo salvo shows here at the next boundary. Only the simulation and loads write them (display
// code reads them, or copies a ship into a new object to draw it).
static FieldSpec g_mountFields[] = {
    {"TFightingTorpedoMount", "TorpsLeft", 4, G_AMMO}, {"TFightingTorpedoMount", "ReloadTime", 4, G_AMMO},
    {"TTorpedomount", "Reloads", 4, G_AMMO},           {"TFightingTorpedoMount", "Destroyed", 1, G_AMMO},
};
// The battle's submarines in list order (engine level 9, group sub): only the simulation (EOTP's submarine routines,
// the missile submarines' fire, the air search) and the battle load write them during a battle; the missile dialog's
// submarine salvo (Downtime := 5000 on the giving PC alone) is blocked in a live battle (UncheckMissileSubs).
static FieldSpec g_subFields[] = {
    {"TSubmarine", "Downtime", 4, G_SUB}, {"TSubmarine", "Location", 8, G_SUB}, {"TSubmarine", "TorpsFired", 4, G_SUB},
};
// Each side's agreed "Fire at will" (engine level 9): the memory of the order pseudo-objects Mfireatwill#0 and #1 (1 =
// the side's missile ships fire on their own under the computer's rules; 0 = only the player's salvoes). Session state:
// A sets it for a new live battle, the start and resync messages carry it (`faw=`).
static const uint8_t kFireAtWillDefault = 1;
static uint8_t g_faw[2] = {kFireAtWillDefault, kFireAtWillDefault};
static int g_fMountList = -1;  // Tship.TorpedomountList
static bool g_specsResolved = false;

static void ResolveSpecs() {
    if (g_specsResolved) return;
    g_specsResolved = true;
    for (auto& f : g_shipFields) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_divFields) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_forceFields) {
        f.off = dl::Field(f.cls, f.name);
        if (f.size == 0 && (f.size = ForceArraySize(f.name)) == 0) f.off = -1;  // a static array: sized from the layout
    }
    for (auto& f : g_airFields) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_torpFields) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_mountFields) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_subFields) f.off = dl::Field(f.cls, f.name);
    g_fMountList = dl::Field("Tship", "TorpedomountList");
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

// The ship's mounts as hashed: their number, then each mount's g_mountFields.
static std::string MountBytes(void* ship) {
    std::string b;
    void* ml = g_fMountList >= 0 ? dl::At<void*>(ship, g_fMountList) : nullptr;
    int n = dl::ListCount(ml);
    b.append((const char*)&n, 4);
    for (int i = 0; i < n; i++)
        if (void* mt = dl::ListItem(ml, i))
            for (auto& f : g_mountFields)
                if (f.off >= 0) b.append((const char*)mt + f.off, f.size);
    return b;
}

static std::wstring ObjName(void* obj) {
    if (!obj) return L"-";
    if (dl::IsInstanceOf(obj, "TDivision")) return dl::ReadUStr(dl::At<void*>(obj, E.fDivName));
    if (dl::IsInstanceOf(obj, "Tship")) return dl::ReadUStr(dl::At<void*>(obj, E.fShipName));
    if (dl::IsInstanceOf(obj, "TForce")) return dl::ReadUStr(dl::At<void*>(obj, E.fForceName));
    if (E.fAfId >= 0 && dl::IsInstanceOf(obj, "TAirFormation")) return L"AF" + std::to_wstring(dl::At<int>(obj, E.fAfId));
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

static bool TypeFlagsNow(uint8_t out[3]);  // (the campaign's ship-type flags; below)
static bool g_typeFlagsHash = true;  // TEST ONLY (`ls shiptypes nohash`): the flags out of the state hash (as 0.4.1 RC)

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
            std::string mb = MountBytes(s);
            Fnv(r.group[G_AMMO], mb.data(), mb.size());
        }
    }
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* a = dl::ListItem(air, i)) {
            r.air++;
            HashFields(r.group, a, g_airFields, sizeof(g_airFields) / sizeof(g_airFields[0]));
        }
    // engine level 7: the AFID counter (launches from the screen step it back) and ScoutCount (the generated names)
    if (E.afIdCounter) Fnv(r.group[G_AIR], E.afIdCounter, 4);
    if (E.scoutCount) Fnv(r.group[G_AIR], E.scoutCount, 4);
    void* torps = List(E.torpVar);
    for (int i = 0; i < dl::ListCount(torps); i++)
        if (void* t = dl::ListItem(torps, i)) {
            r.torps++;
            HashFields(r.group, t, g_torpFields, sizeof(g_torpFields) / sizeof(g_torpFields[0]));
        }
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++) {
        void* fc = dl::ListItem(forces, i);
        if (!fc) continue;
        FnvStr(r.group[G_FORCE], ObjName(fc));
        HashFields(r.group, fc, g_forceFields, sizeof(g_forceFields) / sizeof(g_forceFields[0]));
        void* fd = E.fFDivs >= 0 ? dl::At<void*>(fc, E.fFDivs) : nullptr;
        for (int j = 0; j < dl::ListCount(fd); j++) FnvStr(r.group[G_FORCE], ObjName(dl::ListItem(fd, j)));
        Fnv(r.group[G_FORCE], "|", 1);
    }
    for (int s = 0; s < 2 && E.landCap; s++) {  // the land-CAP requests, in the game's layout: the division's name and side
        void* d = *LandCapSlot(s);
        int ds = d && E.fDivSide >= 0 && dl::IsInstanceOf(d, "TDivision") ? dl::At<int>(d, E.fDivSide) : -1;
        FnvStr(r.group[G_ORD], ObjName(d));
        Fnv(r.group[G_ORD], &ds, 4);
    }
    if (g_featFireAtWill) Fnv(r.group[G_ORD], g_faw, 2);  // engine level 9: each side's agreed Fire at will
    void* subs = List(E.subsVar);  // engine level 9: the submarines
    int nsub = dl::ListCount(subs);
    Fnv(r.group[G_SUB], &nsub, 4);
    for (int i = 0; i < nsub; i++)
        if (void* sb = dl::ListItem(subs, i)) HashFields(r.group, sb, g_subFields, sizeof(g_subFields) / sizeof(g_subFields[0]));
    r.all = 1469598103934665603ull;
    for (int g = 0; g < G_COUNT; g++)
        if (g != G_IDENT) Fnv(r.all, &r.group[g], 8);
    if (E.clockVar) Fnv(r.all, E.clockVar, 8);
    int32_t date[3];  // engine level 10: the campaign's Year, Month and Day (the weather's season, the AI's day parity)
    if (CampaignDateNow(date)) Fnv(r.all, date, sizeof(date));
    uint8_t types[3];  // engine level 17: the ship-type flags the identification reads (TypeFlagsNow)
    if (g_typeFlagsHash && TypeFlagsNow(types)) Fnv(r.all, types, sizeof(types));
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
// DESYNC forensics (every live battle, engine-neutral): the call sites (rva) of this PC's shared draws in the running
// minute; OnBoundary keeps the last minutes' (g_flight), and a DESYNC report writes them (DesyncReport).
static std::vector<uint32_t> g_flightSeq;

extern "C" uint32_t __cdecl LS_Random32() {
    uint32_t* frame = (uint32_t*)_AddressOfReturnAddress();  // [0] into Random, [1] its pushed range, [2] its caller
    int kind = g_ui > 0 ? 1 : g_sim > 0 ? 0 : 2;
    g_draws[kind]++;
    if (kind != 0 && g_callers[kind].size() < 4096) g_callers[kind][frame[2]]++;
    bool shared = kind == 0 || (kind == 2 && g_sharedOther) || g_rngMode == 1;
    if (!shared) return g_uiState = g_uiState * 0x08088405u + 1;
    if (g_trace && g_traceSeq.size() < 200000) g_traceSeq.push_back(frame[2] - (uint32_t)dl::g_base);
    if (g_flightSeq.size() < 100000) g_flightSeq.push_back(frame[2] - (uint32_t)dl::g_base);
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
    g_flightSeq.clear();
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

static void CheckFpu(const char* where);  // (the x87 control word around the steps and hashes; below)

std::string HashLine(const char* tag) {
    CheckFpu("the hash");
    HashResult h = StateHash();
    char b[760];
    int n = snprintf(b, sizeof(b), "%s t=%s min=%d all=%016llx", tag, ClockText().c_str(), Minute(),
                     (unsigned long long)h.all);
    for (int g = 0; g < G_COUNT; g++)
        n += snprintf(b + n, sizeof(b) - n, " %s=%08x", kGroupNames[g], (unsigned)(h.group[g] ^ (h.group[g] >> 32)));
    void* m = Frm();
    n += snprintf(b + n, sizeof(b) - n,
                  " ships=%d(%d/%d) sunk=%d divs=%d af=%d torps=%d draws sim=%llu ui=%llu other=%llu rng=%08x speed=%d",
                  h.ships, h.sides[0], h.sides[1], h.sunk, h.divisions, h.air, h.torps,
                  (unsigned long long)(g_draws[0] - g_drawsAt[0]), (unsigned long long)(g_draws[1] - g_drawsAt[1]),
                  (unsigned long long)(g_draws[2] - g_drawsAt[2]), g_simState, m ? dl::At<int>(m, E.fGameSpeed) : -99);
    int32_t date[3];
    if (n > 0 && n < (int)sizeof(b) && CampaignDateNow(date))
        snprintf(b + n, sizeof(b) - n, " date=%d-%02d-%02d", date[0], date[1], date[2]);
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
// Simulation contexts the player can also reach from the battle screen (DetachShip, CheckSupportingDDToDetach and, from
// engine level 7, LaunchSomeAircraft) are "actions": run from the screen they are recorded and replayed on the other PC
// (see OnActionEnter, OnLaunchEnter).
enum { H_STARTMINUTE, H_MOVETORPS, H_MOVESHIPS, H_EOTP, H_DETACH, H_CSDD, H_LAUNCH, H_RESULTS, H_FIRST_UI };
static CtxHook g_ctx[] = {
    {nullptr, "StartMinute", K_SIM, 0}, {nullptr, "MoveTorpedoes", K_SIM, 0}, {nullptr, "MoveAllShips", K_SIM, 0},
    {nullptr, "EndOfTurnProcedures", K_SIM, 0}, {"TfrmMain", "DetachShip", K_SIM, 0},
    {"TfrmMain", "CheckSupportingDDToDetach", K_SIM, 0},
    // (Self, aNumber, aMission; const aTarget, aName, aOrderedState, aLoadout, aSpotNow): the strike dialog's launch
    {nullptr, "LaunchSomeAircraft", K_SIM, 0x14}, {nullptr, "ShowResultScreen", K_RES, 0},
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
    // (): the "Show air formations" list, which the end of a minute refreshes; it lists this PC's side
    {"TdlgShowAirFormations", "UpdateListSortedByBase", K_UI, 0},
    // (): the missile panel's counts (unnamed; the end of a minute, after 1942): PlayerSide's missile ships and targets
    {nullptr, "UpdateMissilePanel", K_UI, 0},
    // (): the strike dialog's squadron list (unnamed FillSquadronList; Execute, its buttons, and the mod after a minute):
    // PlayerSide's formations of the selected division; its exit marks the list as up to date (StrikeReady)
    {nullptr, "FillSquadronList", K_UI, 0},
    // (ShipToSelect): the division report's ship list (its Execute, the torpedo dialog's Close and Fire); it lowers each
    // listed ship's CurrentSpeed to its maximum speed in this weather, a simulation field (put back: ReportSpeeds*)
    {"TdlgDivisionReport", "FillShipList", K_UI, 0},
};
static const int kCtxHooks = sizeof(g_ctx) / sizeof(g_ctx[0]);
static const int kManual = -1;  // a frame of the mod's own shared section
static int g_ctxFillTree = -1;  // the index of FillShipTree in g_ctx
static int g_ctxStrikeFill = -1;  // the index of FillSquadronList in g_ctx
static int g_ctxReportFill = -1;  // the index of TdlgDivisionReport.FillShipList in g_ctx

struct Frame {
    uint32_t slot;  // the address of the caller's return address (shared sections: the owner's stack position)
    int idx;        // hook index, or kManual
    int kind;
};
static Frame g_frames[512];
static int g_nFrames = 0;
static bool g_leakCheck = false;  // tests (`ls leakcheck 1`): the simulation state must not change between its steps
static bool g_leakIdent = true;   // the leak check covers the simulation's identification (engine level 17; `ls leakcheck ident 0`)
static std::vector<int> g_uiSince;  // display routines entered since the last simulation step (leak check)
static int g_outerIdx = -2;          // the frame that entered the simulation (leak check)
static bool g_testThrow = false;  // tests: an exception inside this PC's next MoveAllShips (`ls throw`)
static bool g_testStall = false;  // tests: this PC's next released minute doesn't start (`ls stall`)
static int g_viewSide = -1;  // the side this PC shows outside the simulation (-1: never touch the display state)
// The simulation's PlayerSide: 0 (canonical: the same on both PCs), or 2 (no human side) while side 0's ships are on
// auto-run (SetSimSideFromAuto, changed only at releases and loads). A hook may run one game routine with another one
// (a human side, or 2 = no human side: the computer's rules for both, see CallWithSimSide); the display swaps restore
// this value, not 0, so nested hooked frames keep it.
static int g_simPlayerSide = 0;
static int g_results = 0;    // inside the game's result screen (its dialogs are the player's own)
static bool g_forceCanonical = false;  // the simulation's set stays in the ships (a snapshot is being saved)
static int g_manual = 0;               // open shared sections

// Session-level reactions (defined below).
static void ApplyViewSide();
static void OnSimOuterEnter();
static void OnSimOuterExit();
static void OnActionEnter(int idx, void* a, void* b);
static void OnActionExit(int idx, void* result);
static void OnLaunchEnter(uint32_t src, uint32_t number, uint32_t mission, const uint32_t* stackArgs);
static void OnLaunchPreExit(void* result);
static void OnLaunchExit();
static void OnSimFrameUnwound(int idx);
static void ReportSpeedsSave();
static void ReportSpeedsRestore();
static bool g_strikeListFresh = true;  // the strike dialog's list was filled after the last minute ended (StrikeReady)

static void LeakCheckEnter(int idx);
static int g_outerIdxFwd();
static void FlightMinuteStart();  // DESYNC forensics (defined with the state dump)
static void OnResultsExit();
static void OnResultsEnter();
static void MapAfterStep(int outerIdx);  // this PC's map after a simulation step (engine level 15)

static void FrameEnterCount(int kind, int dir) {
    if (kind == K_RES) {
        if (dir > 0) {
            if (++g_results == 1) OnResultsEnter();
        } else if (g_results > 0) {
            g_results--;
        }
        ApplyViewSide();
        return;
    }
    if (kind == K_SIM) {
        if (dir > 0) {
            if (g_sim++ == 0) {
                OnSimOuterEnter();
                ApplyViewSide();  // the simulation's set is in place for the leak check
                LeakCheckEnter(g_outerIdxFwd());
                if (g_outerIdxFwd() == H_STARTMINUTE) FlightMinuteStart();
            }
        } else if (g_sim > 0 && --g_sim == 0) {
            OnSimOuterExit();
            ApplyViewSide();
            MapAfterStep(g_outerIdxFwd());  // (this PC's map: the step may have set the first side's picture)
            return;
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
        // The end of a minute refills the ship tree for PlayerSide, which is the canonical side inside the simulation (0,
        // or g_simPlayerSide while a hook runs a routine with another one): the tree is display only, so it shows this
        // PC's own side (ECX = aSide; the thunk's popad uses it).
        if (idx == g_ctxFillTree && g_sim > 0 && g_viewSide >= 0 && (r->ecx == 0 || (int)r->ecx == g_simPlayerSide))
            r->ecx = (uint32_t)g_viewSide;
        if ((idx == H_DETACH || idx == H_CSDD) && g_sim == 0)
            OnActionEnter(idx, (void*)(uintptr_t)r->edx, (void*)(uintptr_t)r->ecx);
        // LaunchSomeAircraft from the screen: EAX = the source formation, EDX = the number, CL = the mission; the stack
        // arguments above the caller's return address (slot): aSpotNow, aLoadout, aOrderedState, aName, @aTarget.
        if (idx == H_LAUNCH && g_sim == 0) OnLaunchEnter(r->eax, r->edx, r->ecx, (const uint32_t*)(uintptr_t)slot + 1);
        if (idx == g_ctxReportFill && g_sim == 0) ReportSpeedsSave();
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
            // A launch from the screen: the new formation gets its final AFId before this frame's exit takes the agreed
            // state (OnSimOuterExit), so the dialog's later writes on it are orders under that key.
            if (idx == H_LAUNCH && g_sim == 1) OnLaunchPreExit((void*)(uintptr_t)r->eax);
            if (g_trace && g_ctx[idx].kind == K_SIM && g_sim == 1) TraceStateMark(idx, true);
            g_nFrames--;
            FrameEnterCount(g_ctx[idx].kind, -1);
            if (idx == g_ctxStrikeFill && g_sim == 0) g_strikeListFresh = true;
            if (idx == g_ctxReportFill && g_sim == 0) ReportSpeedsRestore();
        } else {
            Log("lockstep: context frame of %s not found at its exit", g_ctx[idx].name);
        }
        if ((idx == H_DETACH || idx == H_CSDD) && g_sim == 0) OnActionExit(idx, (void*)(uintptr_t)r->eax);
        if (idx == H_LAUNCH && g_sim == 0) OnLaunchExit();
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
void* g_lsOrigAirstrike = nullptr;
void* g_lsOrigSearchPlan = nullptr;
void* g_lsOrigMissileFire = nullptr;
void* g_lsThunkMissilePanel = nullptr;
void* g_lsOrigFireMount = nullptr;
void* g_lsOrigLandCap = nullptr;
void* g_lsOrigSearchAir = nullptr;
void* g_lsOrigFireMissileMount = nullptr;
void* g_lsOrigFireSSM = nullptr;
int g_lsMslCount = 0;  // what FireMissileMount's hook returns to the missile dialog (its predicted count)
int __stdcall LS_StartMinuteAllowed(void* self);
void __stdcall LS_AfterEOTP(void* self);
int __stdcall LS_StartRun(void* self, int n);
int __stdcall LS_MsgDlg(const wchar_t* msg, int type, int buttons, const uint32_t* entryEsp, uint32_t callerEbp);
int __stdcall LS_ShowPic(const wchar_t* text, const wchar_t* pic);
int __stdcall LS_ExecGeneral(const wchar_t* prompt, int preselect);
int __stdcall LS_Block(int idx, void* self, void* sender, uint32_t arg3);
void __stdcall LS_AirStrike(void* self, int side);
void __stdcall LS_SearchPlan(void* self, int include);
void __stdcall LS_MissileFire(void* self);
int __stdcall LS_MissilePanelAllowed();
int __stdcall LS_FireMount(void* self, void* mount, void* ship, void* target, uint32_t angle, int speed, int range,
                           int manual);
void __stdcall LS_LandCap(int side);
void __stdcall LS_SearchAir(void* force);
int __stdcall LS_FireMissileMount(void* self, void* mount, void* ship, void* target, void* loc, int bearing, int n);
int __stdcall LS_FireSSM(void* self, int side, void* loc);
}

// TfrmMain.FireMissileMount(Self=EAX, aMount=EDX, aShip=ECX; aTargetShip, aTargetLocation (a pointer to the TPointLL),
// TargetBearing, NumberToFire): Integer, ret 0x10. LS_FireMissileMount = 0 runs the routine; 1 returns as the routine
// would: its four stack arguments popped, EAX = g_lsMslCount (the count the dialog's stop rule and summary use).
extern "C" __declspec(naked) void LS_dFireMissileMount() {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 16]  // NumberToFire (the stack arguments shift by 4 with every push)
        push dword ptr [esp + 24]  // TargetBearing
        push dword ptr [esp + 32]  // aTargetLocation
        push dword ptr [esp + 40]  // aTargetShip
        push ecx
        push edx
        push eax
        call LS_FireMissileMount
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz skip
        jmp dword ptr [g_lsOrigFireMissileMount]
    skip:
        mov eax, dword ptr [g_lsMslCount]
        ret 16
    }
}

// TfrmMain.FireSSM(Self=EAX, aSide=EDX, aFiringLocation=ECX; aFiringShipName, aTarget, aBearing, aSpeed, aRange, aCalibre,
// aDisruption), ret 0x1c: LS_FireSSM = 0 runs the routine, 1 returns without a missile (its seven stack arguments popped;
// the string parameter's reference is the routine's own business).
extern "C" __declspec(naked) void LS_dFireSSM() {
    __asm {
        push eax
        push ecx
        push edx
        push ecx
        push edx
        push eax
        call LS_FireSSM
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz skip
        jmp dword ptr [g_lsOrigFireSSM]
    skip:
        ret 28
    }
}

// CheckLandBasedCAP(Side=EAX): a plain procedure (no Self, no stack arguments)
extern "C" __declspec(naked) void LS_dLandCap() {
    __asm {
        push eax
        call LS_LandCap
        ret
    }
}

// TForce.CheckSearchAircraft(Self=EAX)
extern "C" __declspec(naked) void LS_dSearchAir() {
    __asm {
        push eax
        call LS_SearchAir
        ret
    }
}

// TfrmMain.FireMount(Self=EAX, aMount=EDX, aShip=ECX; aTarget, FireAngle: Single, TorpSpeed, Range, Manual), ret 0x14:
// LS_FireMount = 0 runs the routine, 1 returns as the routine would (its five stack arguments popped).
extern "C" __declspec(naked) void LS_dFireMount() {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 16]  // Manual (the stack arguments shift by 4 with every push)
        push dword ptr [esp + 24]  // Range
        push dword ptr [esp + 32]  // TorpSpeed
        push dword ptr [esp + 40]  // FireAngle
        push dword ptr [esp + 48]  // aTarget
        push ecx
        push edx
        push eax
        call LS_FireMount
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz skip
        jmp dword ptr [g_lsOrigFireMount]
    skip:
        ret 20
    }
}

// CheckAIAirstrike(Self=EAX, Side=EDX)
extern "C" __declspec(naked) void LS_dAirStrike() {
    __asm {
        push edx
        push eax
        call LS_AirStrike
        ret
    }
}

// TfrmMain.SetAIAirSearchPlan(Self=EAX, IncludePlayerControlledShips=DL)
extern "C" __declspec(naked) void LS_dSearchPlan() {
    __asm {
        movzx edx, dl
        push edx
        push eax
        call LS_SearchPlan
        ret
    }
}

// CheckMissileFiringForAllShips(Self=EAX)
extern "C" __declspec(naked) void LS_dMissileFire() {
    __asm {
        push eax
        call LS_MissileFire
        ret
    }
}

// UpdateMissilePanel(Self=EAX): its display context (the thunk), or nothing at all (LS_MissilePanelAllowed)
extern "C" __declspec(naked) void LS_dMissilePanel() {
    __asm {
        push eax
        call LS_MissilePanelAllowed
        test eax, eax
        pop eax
        jz skip
        jmp dword ptr [g_lsThunkMissilePanel]
    skip:
        ret
    }
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

// MessageDlgPosHelp(Msg=EAX, DlgType=DL, Buttons=CX; HelpCtx, X, Y, HelpFileName): Integer. LS_MsgDlg also gets the
// stack at entry (the return address, then the four stack arguments; ShowMessagePos has no frame of its own) and EBP,
// the frame of the routine that called ShowMessagePos (a sighting box's side: BoxSiteSide).
extern "C" __declspec(naked) void LS_dMsgDlg() {
    __asm {
        push eax
        push ecx
        push edx
        lea eax, [esp + 12]
        push ebp
        push eax
        movzx ecx, word ptr [esp + 12]
        movzx edx, byte ptr [esp + 8]
        mov eax, dword ptr [esp + 16]
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
    bool on;     // hooked
};
enum { B_PAUSE, B_END, B_AUTORUN, B_DETACH, B_UNAVAILABLE, B_SAVE, B_SPEED, B_ARROW, B_FASTEST, B_CHFORCE, B_LEADBTN,
       B_SELECTLEAD, B_TORPS, B_AIROPS, B_LANDCAP, B_SEARCHDLG, B_STRIKE, B_STRIKEDLG, B_STRIKEBTN, B_SALVO, B_SALVOFIRE,
       B_FIREATWILL, B_PREFS };
static Blocker g_blockers[] = {
    {"TfrmMain", "sbPauseClick", B_PAUSE},
    {"TfrmMain", "sbEndGameClick", B_END},
    {"TfrmMain", "sbAutoRunBattleClick", B_AUTORUN},
    {"TfrmMain", "Detach1Click", B_DETACH},
    {"TdlgDivisionReport", "Detach1Click", B_DETACH},
    {"TfrmMain", "sbSaveClick", B_SAVE},
    // (Self, Sender): the battle screen's Preferences (engine level 15): the dialog opens; the battle preferences the
    // simulation uses stay this battle's until it ends (a change of them is put back, with a chat line)
    {"TfrmMain", "sbPrefsClick", B_PREFS},
    {"TfrmMain", "cbGameSpeedChange", B_SPEED},
    {"TfrmMain", "UpDown1Click", B_ARROW},
    {"TfrmMain", "cbFastestAvailableClick", B_UNAVAILABLE},
    {"TfrmMain", "sbFastestAvailableClick", B_FASTEST},
    {"TfrmMain", "sbStrikeClick", B_STRIKE},
    {"TfrmMain", "LaunchStrike1Click", B_STRIKEDLG},
    {"TfrmMain", "Launchstrike2Click", B_UNAVAILABLE},
    {"TfrmMain", "Launchaircraft1Click", B_UNAVAILABLE},
    {"TfrmMain", "Bombship1Click", B_UNAVAILABLE},
    {"TfrmMain", "TryforHome1Click", B_UNAVAILABLE},
    {"TfrmMain", "bnFireClick", B_UNAVAILABLE},
    {"TfrmMain", "sbFireSalvoClick", B_SALVO},
    // (Self, Sender): the base flags' and Fire at will's click handler (a repaint); Fire at will is this player's order
    {"TfrmMain", "cbShowBaseFlagsClick", B_FIREATWILL},
    {"TfrmMain", "sbRoutineAirOpsClick", B_AIROPS},
    {"TfrmMain", "Searchpattern1Click", B_AIROPS},
    {"TfrmMain", "ShowAircraft1Click", B_UNAVAILABLE},
    {"TfrmMain", "Changeforce2Click", B_CHFORCE},
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
    {"TdlgDivisionReport", "bnTorpsClick", B_TORPS},
    {"TdlgDivisionReport", "sbLandCAPClick", B_LANDCAP},
    {"TdlgForceOrders", "sbChangeLeadClick", B_LEADBTN},
    {"TdlgForceOrders", "sbTurnAwayClick", B_LEADBTN},
    {"TdlgSelectLead", "bnOKClick", B_SELECTLEAD},
    {"TdlgTorpTargets", "bnFireClick", B_TORPS},
    {"TdlgSearch", "Execute", B_SEARCHDLG},  // (Self, aForce, Editor): not a click handler; EDX = the force
    // The strike dialog's state-changing buttons: between minutes, once its list is up to date (StrikeReady)
    {"TdlgStrike", "bnLaunchClick", B_STRIKEBTN},
    {"TdlgStrike", "bnReadyClick", B_STRIKEBTN},
    {"TdlgStrike", "bnStandDownClick", B_STRIKEBTN},
    // The missile dialog's Fire one / half / all (FireMissiles(1|2|4)): between minutes; missile submarines unchecked
    {"TdlgMissileFire", "bnFireOneClick", B_SALVOFIRE},
    {"TdlgMissileFire", "bnfireHalfClick", B_SALVOFIRE},
    {"TdlgMissileFire", "bnFireAllClick", B_SALVOFIRE},
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
    b({0x51, 0x52, 0x50});  // push ecx (the third register argument); push edx (Sender); push eax (Self)
    b({0x68});              // push idx
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
// The live-battle rules' hooks that are in place (each one optional: without it that rule is off, as in 0.3.x).
static bool g_ruleAir = false, g_rulePlan = false, g_ruleMissile = false, g_rulePanel = false, g_ruleLandCap = false,
            g_ruleScout = false;

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

static void* DialogForm(const char* cls);

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
    g_ctx[H_LAUNCH].target = g_launchHookable ? E.launchFn : nullptr;  // (optional: without it strikes stay blocked)
    int panelIdx = -1;  // UpdateMissilePanel: a display context behind a gate (LS_dMissilePanel)
    for (int i = H_FIRST_UI; i < kCtxHooks; i++)
        if (!g_ctx[i].cls && !strcmp(g_ctx[i].name, "UpdateMissilePanel")) {
            g_ctx[i].target = E.missilePanel;
            panelIdx = i;
        } else if (!g_ctx[i].cls && !strcmp(g_ctx[i].name, "FillSquadronList")) {
            g_ctx[i].target = E.strikeFill;
            g_ctxStrikeFill = i;
        }
    int n = 0;
    for (int i = 0; i < kCtxHooks; i++) {
        CtxHook& h = g_ctx[i];
        if (h.cls) h.target = dl::Method(h.cls, h.name);
        if (h.cls && strcmp(h.name, "FillShipTree") == 0) g_ctxFillTree = i;
        if (h.cls && strcmp(h.name, "FillShipList") == 0) g_ctxReportFill = i;
        h.thunk = h.target ? EmitCtxThunk(i) : nullptr;
        void* detour = i == H_STARTMINUTE ? (void*)&LS_dStartMinute
                       : i == H_EOTP      ? (void*)&LS_dEOTP
                       : i == panelIdx    ? (void*)&LS_dMissilePanel
                                          : h.thunk;
        if (i == H_STARTMINUTE) g_lsThunkStartMinute = h.thunk;
        if (i == H_EOTP) g_lsThunkEOTP = h.thunk;
        if (i == panelIdx) g_lsThunkMissilePanel = h.thunk;
        h.on = h.thunk && HookOne(h.target, detour, &h.orig, h.name);
        if (h.on) n++;
        else if (i < H_FIRST_UI && i != H_LAUNCH) ok = false;  // the simulation contexts are required
    }
    g_rulePanel = panelIdx >= 0 && g_ctx[panelIdx].on;
    if (!g_ctx[H_LAUNCH].on || g_ctxStrikeFill < 0 || !g_ctx[g_ctxStrikeFill].on) g_featStrike = false;
    int nb = 0;
    for (int i = 0; i < kBlockers; i++) {
        Blocker& b = g_blockers[i];
        void* target = dl::Method(b.cls, b.name);
        void* thunk = target ? EmitBlockThunk(i) : nullptr;
        b.on = thunk && HookOne(target, thunk, &b.orig, b.name);
        if (b.on) nb++;
        else if (b.policy == B_CHFORCE) g_featChforce = false;
        else if (b.policy == B_SELECTLEAD) g_featLead = false;  // (the Force orders buttons stay blocked then)
        else if (b.policy == B_TORPS) g_featTorp = false;       // (FireMount's hook still refuses salvoes then)
        else if (b.policy == B_AIROPS || b.policy == B_SEARCHDLG) g_featAirOps = false;  // (logged as OFF)
        else if (b.policy == B_STRIKE || b.policy == B_STRIKEDLG || b.policy == B_STRIKEBTN) g_featStrike = false;
        else if (b.policy == B_SALVO || b.policy == B_SALVOFIRE) g_featMissile = false;  // (Manual fire: "not available")
        else if (b.policy == B_FIREATWILL) g_featFireAtWill = false;  // (the 0.4.0 rule: both fire at will, panel locked)
        else if (b.policy != B_UNAVAILABLE && b.policy != B_LEADBTN && b.policy != B_LANDCAP) ok = false;
    }
    // The force orders' replays restore the Force orders and Select lead dialogs they close.
    if (g_featLead && (!DialogForm("TdlgForceOrders") || !DialogForm("TdlgSelectLead"))) {
        g_featLead = false;
        Log("lockstep: change lead and battle turn-away OFF (their dialogs were not found)");
    }
    // Torpedo salvoes close the torpedo dialog when a minute starts (its firing solutions are for the boundary).
    if (g_featTorp && !DialogForm("TdlgTorpTargets")) {
        g_featTorp = false;
        Log("lockstep: torpedo salvoes OFF (the torpedo dialog was not found)");
    }
    ok &= HookOne(E.startRun, (void*)&LS_dStartRun, &g_lsOrigStartRun, "StartRun");
    ok &= HookOne(E.msgDlg, (void*)&LS_dMsgDlg, &g_lsOrigMsgDlg, "MessageDlgPosHelp");
    ok &= HookOne(E.mShowPic, (void*)&LS_dShowPic, &g_lsOrigShowPic, "ShowPicMessage");
    ok &= HookOne(E.mExecGeneral, (void*)&LS_dExecGeneral, &g_lsOrigExecGeneral, "ExecuteGeneral");
    // The live-battle rules for human sides: optional (a routine that was not found keeps 0.3.x's behaviour).
    g_ruleAir = HookOne(E.aiAirstrike, (void*)&LS_dAirStrike, &g_lsOrigAirstrike, "CheckAIAirstrike");
    g_rulePlan = HookOne(E.searchPlan, (void*)&LS_dSearchPlan, &g_lsOrigSearchPlan, "SetAIAirSearchPlan");
    g_ruleMissile = HookOne(E.missileFire, (void*)&LS_dMissileFire, &g_lsOrigMissileFire, "CheckMissileFiringForAllShips");
    // Manual torpedo salvoes: FireMount's hook, only on the routine read in ResolveTorpedoes (without it the Fire
    // torpedoes buttons keep the 0.3.x blocker; with it, a call outside the simulation never runs on one PC alone).
    if (!(g_torpHookable && HookOne(E.fireMount, (void*)&LS_dFireMount, &g_lsOrigFireMount, "FireMount")))
        g_featTorp = false;
    // Land-based CAP: CheckLandBasedCAP's hook, only on the routine read in ResolveAirOps (in a live battle the mod's copy
    // with the airfield's side checked runs instead; without it the land-CAP button keeps the 0.3.x blocker).
    g_ruleLandCap = g_landCapHookable && HookOne(E.landCapFn, (void*)&LS_dLandCap, &g_lsOrigLandCap, "CheckLandBasedCAP");
    if (!g_ruleLandCap) g_featLandCap = false;
    // Carrier scouts: CheckSearchAircraft runs with the simulation PlayerSide 2 in a live battle (no scout question for
    // either side); without the hook side 0's carriers keep 0.3.x's question (answered No) while they see enemy ships.
    g_ruleScout = g_scoutHookable && HookOne(E.searchAir, (void*)&LS_dSearchAir, &g_lsOrigSearchAir, "CheckSearchAircraft");
    // Air strikes from the strike dialog need the computer's strike routine hooked too: with the players launching their
    // own, the computer must not fly a human side's carrier strikes (AirstrikeSimSide).
    if (!g_ruleAir) g_featStrike = false;
    // Missiles (engine level 9). Each side's Fire at will lives in the automatic fire's hook. FireMissileMount's and
    // FireSSM's hooks, only on the routines read in ResolveMissiles: with them a call from the screen never runs on one
    // PC alone (a salvo is deferred, a submarine's is refused); without either, Manual fire keeps its blocker.
    if (!g_ruleMissile) g_featFireAtWill = false;
    if (!(g_mslHookable && HookOne(E.fireMissileMount, (void*)&LS_dFireMissileMount, &g_lsOrigFireMissileMount,
                                   "FireMissileMount")))
        g_featMissile = false;
    if (!(g_ssmHookable && HookOne(E.fireSSM, (void*)&LS_dFireSSM, &g_lsOrigFireSSM, "FireSSM"))) g_featMissile = false;
    if (g_featMissile && !(E.mslDlgVar && dl::IsInstanceOf(*E.mslDlgVar, "TdlgMissileFire"))) {
        g_featMissile = false;
        Log("lockstep: manual missile salvoes OFF (the missile dialog was not found)");
    }
    g_hooksOk = ok;
    x87::Install();  // engine level 16: the transcendental routines (live battles use them only if all are hooked)
    Log("lockstep hooks: %s (%d of %d context hooks, %d of %d click handlers); live-battle rules: air strikes %s, air "
        "search plan %s, missile fire %s, missile panel %s, land CAP airfield side %s, scout question %s; force orders: "
        "change force %s, change lead and turn-away %s; torpedo salvoes %s; routine air ops %s, land-based CAP %s; air "
        "strikes from the strike dialog %s; missiles: fire at will per side %s, manual salvoes %s",
        ok ? "ok" : "INCOMPLETE", n, kCtxHooks, nb, kBlockers, g_ruleAir ? "on" : "OFF", g_rulePlan ? "on" : "OFF",
        g_ruleMissile ? "on" : "OFF", g_rulePanel ? "on" : "OFF", g_ruleLandCap ? "on" : "OFF", g_ruleScout ? "on" : "OFF",
        g_featChforce ? "on" : "OFF", g_featLead ? "on" : "OFF", g_featTorp ? "on" : "OFF", g_featAirOps ? "on" : "OFF",
        g_featLandCap ? "on" : "OFF", g_featStrike ? "on" : "OFF", g_featFireAtWill ? "on" : "OFF",
        g_featMissile ? "on" : "OFF");
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
    // "V<obj key>\x1f<field>\x1f<value>" (an order), "A<action>\x1f<arg>..." (an immediate action, already carried out
    // on the PC that sent it) or "D<kind>\x1f<minute given at>\x1f<arg>..." (a deferred action, see RunDeferred)
    std::vector<std::string> items;
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
    int clockSpeed = INT_MIN;  // B: the game speed A announced last (the speed arrows step from it)
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
    std::string savedTypes;  // B: this PC's own ship-type flags (engine level 17; restored at the end)
    std::string typesA;      // B: A's ship-type flags (`st=`)
    int startSpeed = 1;      // B: the game speed of the snapshot
    bool firstLoad = false;  // B: the first load of this battle (side 1 becomes human)
    bool everRunning = false;  // A: the battle ran in lockstep at least once
    bool peerReady = false;    // A: B's load finished
    std::string peerReadyHash, myReadyHash;
    // Engine level 16: the READY hash's parts (ReadyParts) of both PCs, B's CPU vendor and own x87 control word (in its
    // `ready`), what differed in plain words (ReadyDiag), and the transcendental maths in software on both PCs.
    std::string peerReadyParts, myReadyParts, peerCpu, readyDiag;
    unsigned peerOwnCw = 0, ownCw = 0;
    int cwFixes = 0;
    bool softMath = false;
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
    // Live-battle rules (engine level 2)
    int fastest = 0;            // Fastest available as both PCs applied it last (0/1; clock state, not in the battle save)
    int wantFastest = INT_MIN;  // A: Fastest available to apply with the next release
    int clockFastest = 0;       // B: Fastest available as A announced it last
    int startFastest = 0;       // B: Fastest available of the snapshot
    int peerLevel = -1;         // the other PC's live-battle engine level (B: from the start, A: from "ready")
    int aiAirCalls = 0;         // CheckAIAirstrike calls in this battle (tests: ls airstat)
    int aiAir[2] = {0, 0}, aiNaval[2] = {0, 0};  // formations CheckAIAirstrike made per side (naval attack: of them)
    int confirmCleared = 0, planSkipped = 0, missileCalls = 0;
    // Force orders and Save (engine level 3)
    bool campaignSlot = false;  // A: the battle lives in the host's own campaign slot (Save writes a checkpoint there)
    int saveWant = -1;          // A: the side whose player asked to save the battle (-1: none)
    std::string startAi1;       // B: side 1's computer-control flags in the snapshot (a resync puts them back)
    std::string startAi0;       // B: side 0's (engine level 13: the game's resume changes them too)
    // Auto-run per player (engine level 4)
    bool autoRun[2] = {false, false};  // the side is under the computer's command (both PCs; changed only at releases)
    int clockAuto[2] = {0, 0};         // B: auto-run per side as A announced it last (info)
    bool autoClock = false;            // A: both sides on auto-run started the continuous run
    int preAutoFastest = 0, preAutoSpeed = INT_MIN;  // A: the clock before that run (put back when it ends)
    std::string startAuto = "00";      // B: auto-run per side in the start message (applied after the load)
    // The computer control of a side's forces (TForce.AIControlled, "0"/"1" in list order) before auto-run took it over:
    // Auto-run off gives back what single player gives back (AutoOff). Both PCs record it at the release; the start
    // message carries it (`pa=`).
    std::string preAutoF[2];
    std::string startPa;               // B: preAutoF of the start message ("<side 0>/<side 1>")
    // Routine air ops and land-based CAP (engine level 6)
    std::string startNcap;             // B: NightCAP per force in the start message (the battle save drops it)
    int landCapCalls = 0, landCapOther = 0;  // land-CAP checks run by the mod's copy; of them, where the game's own
                                             // choice was the other side's airfield
    int scoutCalls = 0;                      // the forces' air search runs with the simulation PlayerSide 2
    // Air strikes (engine level 7)
    bool autoStrike = false;   // the host's option: the computer flies both sides' carrier strikes (start message)
    bool autoStrikeNoted = false;  // the strike dialog said so once
    int launchK[2] = {0, 0};   // per side: the next offset of a side-partitioned AFId (launches from the screen)
    int launches = 0, launchReplays = 0, launchFailed = 0, launchSorts = 0;  // this PC's launches, replays (ls airlist)
    // Rejoin (engine level 8)
    bool rejoin = false;       // A: this start brings a dropped player back into the battle that went on without them
    bool rejoinAuto = false;   // A: the game's own auto-run was on then: side 0 stays on auto-run (the per-side one)
    bool restartLoad = false;  // B: a new start of the battle this PC still shows (no loadReady: its screen is open)
    bool resumeStart = false;  // A: this rejoin starts a battle resumed from the host's save, held since (5b: `hr=1`)
    // Missiles (engine level 9)
    int mslHeld[2] = {0, 0};       // divisions the side's Fire at will (off) held in the last automatic missile fire
    int mslHeldCalls[2] = {0, 0};  // automatic missile fire calls with that side held
    int mslOrders = 0, mslReplays = 0, mslFired = 0, mslSkipped = 0, ssmRefused = 0, mslSubsUnchecked = 0;
    std::string startFaw = "11", startPmt;  // B: Fire at will per side and the pending missile targets of the start
} S;

static unsigned g_cwSaved = 0;  // B: this PC's own x87 control word while A's is in use

static bool g_loading = false;  // a battle snapshot is loading (no guard, no display swaps)
static bool g_noView = false;   // tests: no display swap (B shows side 0's view)
static bool g_muted = false;    // tests: this PC drops all lockstep traffic both ways
// Timeouts (ms): a boundary the other PC doesn't reach, no message at all, a load, the heartbeat, a dialog open there.
static DWORD g_tBoundary = 30000, g_tLost = 30000, g_tLoad = 120000, g_tHb = 5000, g_tBusyCap = 300000;
static int g_quiet = 0;         // our own save/load: every message box gets its default answer
static int g_answerYes = 0;     // the agreed end: "Exit battle?" -> Yes
static int g_testAutostart = 0;  // tests (`ls autostart`): the answer to the next "Select autostart point." (1 or 2)
// Inside the replay of a deferred action (RunDeferred): the side whose player gave it, else -1. The game's message boxes
// are answered as in the simulation, and only that player sees their text (in the chat): the other player doesn't learn
// the move from them.
static int g_dfActor = -1;
static bool g_human[2] = {true, true};  // the simulation's human control per side
static bool g_viewOn = false;           // the display state (this PC's side) is applied

// Rejoin (engine level 8). A: the live battle this PC led and dropped its other player from, while its battle screen
// stays open and the battle goes on against the computer (the same battle: the form this PC's own loads made and no new
// DoBattle since). The rejoined battle keeps the battle id; its epochs go on from the last one.
// Phase 5b: the memo of a battle the game resumed from the host's save (ResumeFromMarker) is the same memo, `resumed`,
// and `held` until its player is back or the host presses Run.
static struct RejoinMemo {
    bool on = false;
    int bid = -1, epoch = 0, opp = -1, dropMinute = -1, battleSerial = -1;
    bool campaignSlot = false, autoStrike = false;
    std::string myName, peerName, sideNames[2], why;
    void* form = nullptr;
    DWORD since = 0;
    int slot = -1;          // the host's own battle: its campaign slot (where its marker is), else -1
    int serial = 0;         // counts the memos (the session offers a resumed battle once per memo)
    bool resumed = false;   // the game resumed it from the host's save (phase 5b)
    bool held = false;      // no minute starts until the player is back (the rejoin's start) or the host's Run
    bool slotMode = false;  // a resumed test battle outside a month (its close runs no month)
    bool gateLogged = false;
} g_rj;
static int g_rjSerial = 0;
// A: a rejoin waits for the next minute boundary (no new minute starts meanwhile: MinuteGate, LS_StartRun).
static struct RejoinTry {
    bool want = false;
    std::string peerName, side1;
    SendFn send;
    DWORD since = 0, lastLog = 0, lastRunNote = 0;
    bool gateLogged = false;
} g_rjTry;
static DWORD g_tRejoin = 60000;  // the longest wait for a quiet minute boundary (then the computer keeps side 1)

static void Chat(const std::string& s) {
    Log("lockstep: %s", s.c_str());
    if (g_hooks.chat) g_hooks.chat("* " + s);
}

// The battle the rejoin memo describes is no longer the one on screen (or it is over): why, else nullptr. `over`: the
// battle itself is over (it ended, or another battle started), not just out of sight.
static const char* RejoinInvalid(bool* over = nullptr) {
    if (over) *over = false;
    if (!g_rj.on) return "no live battle was dropped on this PC";
    void* m = BattleForm();
    if (!m || m != g_rj.form || !BattleVisible()) return "the battle screen was closed";
    if (g_rj.battleSerial >= 0 && game::DoBattleCalls() != g_rj.battleSerial) {
        if (over) *over = true;
        return "another battle started";
    }
    if (dl::At<uint8_t>(m, E.fEndGame) || g_results > 0) {
        if (over) *over = true;
        return "the battle is over";
    }
    return nullptr;
}

// Why this PC can't start a dropped battle again (RejoinInvalid, Rejoinable, Rejoin), in the words of the player who
// wanted it back: the host relays it to them (`norejoin`, a relayed battle between two joined players).
static std::string RejoinWhyForPlayer(const std::string& why) {
    std::string me = g_rj.on && !g_rj.myName.empty() ? g_rj.myName : !S.myName.empty() ? S.myName : "the other player";
    if (why == "no live battle was dropped on this PC") return me + "'s game has no battle of yours going on without you";
    if (why == "the battle screen was closed") return me + " closed that battle's screen";
    if (why == "another battle started") return me + " is in another battle now";
    if (why == "the battle is over") return "the battle is over";
    if (why == "a live battle runs on this PC") return me + " is in another live battle now";
    if (why.rfind("the battle is being started again", 0) == 0) return "the battle is being started again already";
    if (why.find("is not the battle dropped here") != std::string::npos) return me + "'s game has another battle to rejoin";
    if (why == "the live battle engine is not available") return me + "'s game can't run live battles now";
    return me + "'s game: " + why;
}

static void DeleteMarker(int slot, const char* why);

// over: the battle is over, so its marker (phase 5b) goes too. A battle screen that was merely closed keeps the marker:
// the game's exit closes it as well, and the battle save the marker describes is still the slot's battle in progress.
static void ForgetRejoin(const char* why, bool over = false) {
    if (!g_rj.on) return;
    Log("lockstep: rejoin: battle %d can no longer be rejoined (%s)%s", g_rj.bid, why,
        g_rj.held ? "; the resumed battle is no longer held" : "");
    if (over && g_rj.slot > 0) DeleteMarker(g_rj.slot, why);
    g_rj = RejoinMemo();
}

// A: the live battle ended here while its battle goes on against the computer (the other player was dropped, or a
// start or reload failed): remember it, so that player can get side 1 back when they return (RejoinTick).
static void RememberRejoin(const std::string& why) {
    void* m = BattleForm();
    if (!S.isA || !m || !BattleVisible() || dl::At<uint8_t>(m, E.fEndGame) || S.peerName.empty()) return;
    bool again = g_rj.on && g_rj.bid == S.bid;
    if (!again) {  // (a failed rejoin of a resumed battle stays the resumed battle: the session doesn't offer it again)
        g_rj.serial = ++g_rjSerial;
        g_rj.resumed = false;
        g_rj.slotMode = false;
    }
    g_rj.held = false;
    g_rj.gateLogged = false;
    g_rj.slot = S.campaignSlot ? game::CurrentSlot() : -1;
    g_rj.on = true;
    g_rj.bid = S.bid;
    g_rj.epoch = S.epoch;
    g_rj.opp = S.opp;
    g_rj.campaignSlot = S.campaignSlot;
    g_rj.autoStrike = S.autoStrike;
    g_rj.myName = S.myName;
    g_rj.peerName = S.peerName;
    g_rj.sideNames[0] = S.sideNames[0];
    g_rj.sideNames[1] = S.sideNames[1];
    g_rj.form = m;  // (this PC's own loads made it; a reload makes another one)
    g_rj.battleSerial = game::DoBattleCalls();
    g_rj.dropMinute = Minute();
    g_rj.why = why;
    g_rj.since = GetTickCount();
    Log("lockstep: rejoin: battle %d (epoch %d) can be rejoined by %s (%s) while it goes on against the computer%s "
        "(minute %d, t=%s: %s)",
        g_rj.bid, g_rj.epoch, g_rj.peerName.c_str(), S.sideNames[1].c_str(), again ? " again" : "", g_rj.dropMinute,
        ClockText().c_str(), why.c_str());
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
    uint32_t simG[kGuard] = {}, dispG[kGuard] = {}, lastSimG[kGuard] = {};  // g_guard
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
        d.dispType = type;
        d.dispHidden = hid;
        StrAsg(&d.dispClass, *cls);
        if (g_identCanon) {  // right after a live load (SetView): the simulation's identification is the battle save's
            if (type != (uint8_t)g_idTypeDefault || *cls) g_identReset++;
            type = (uint8_t)g_idTypeDefault;
            StrAsg(cls, 0);
        }
        d.simType = d.lastSimType = type;
        StrAsg(&d.simClass, *cls);
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

// Display swaps of this PC's view inside or outside the simulation (FrameEnterCount, the shared sections, CanonBegin/End).
static bool ViewInPlace() { return !g_forceCanonical && (g_results > 0 || !(g_sim > 0 && g_ui == 0)); }

static void ApplyViewSide() {
    if (g_viewSide < 0 || g_loading || !E.playerSideVar) return;
    bool view = ViewInPlace();
    *E.playerSideVar = view ? g_viewSide : g_simPlayerSide;
    if (view != g_viewOn) {
        g_viewOn = view;
        WriteHumanFlags(view);
    }
    SwapLandCap(view && g_viewSide == 1 && g_featLandCap);  // the division report shows this player's own request
}

// The simulation's set of the display-swapped fields (identification, sighting, human flags, the simulation's
// PlayerSide) in the ships while the battle is hashed or saved; then this PC's display set again.
static void CanonBegin() {
    g_forceCanonical = true;
    ApplyViewSide();
}
static void CanonEnd() {
    g_forceCanonical = false;
    ApplyViewSide();
}

// Runs a game routine (register convention, at most three arguments, none on the stack) with the simulation's
// PlayerSide `side` for its duration: the display swaps of nested hooked frames put `side` back, and the previous value
// returns when it ends or an exception unwinds it. PlayerSide itself is written unless this PC's display set is in
// place (then the next swap back into the simulation writes it).
static uint32_t CallWithSimSide(int side, void* fn, uint32_t a, uint32_t d = 0, uint32_t c = 0) {
    int prev = g_simPlayerSide;
    g_simPlayerSide = side;
    if (E.playerSideVar && !(g_viewSide >= 0 && !g_loading && ViewInPlace())) *E.playerSideVar = side;
    uint32_t r = 0;
    __try {
        r = dl::Call(fn, a, d, c);
    } __finally {
        g_simPlayerSide = prev;
        if (E.playerSideVar && !(g_viewSide >= 0 && !g_loading && ViewInPlace())) *E.playerSideVar = prev;
    }
    return r;
}

// Display swaps on for this PC's side; or off (side -1): the ships are about to be freed (a load), nothing is written in
// them (the land-CAP slots, globals that hold no reference count, go back to the game's layout).
static void SetView(int side) {
    if (side < 0) {
        SwapLandCap(false);
        g_viewSide = -1;
        g_viewOn = false;
        ReleaseDual();
        return;
    }
    ReleaseDual();
    g_viewSide = side;
    g_viewOn = false;  // the fields hold the simulation's values (just loaded)
    // The battle save keeps no identification and the load's own drawing identified enemy ships with this PC's display
    // draws: the first capture gives the simulation the identification of a freshly loaded ship on both PCs
    // (TFightingShip.Create's), this PC's display keeps what it drew. (Before, each PC kept its own as the simulation's,
    // and the sighting reports' IdentifyClass/IdentifyType in EOTP then drew a different number of shared random numbers.)
    g_identCanon = g_idTypeDefault >= 0 && DualOk();
    g_identReset = 0;
    ApplyViewSide();
    if (g_identCanon && !g_viewOn) WriteHumanFlags(false);  // (no swap yet: capture now, the fields stay the simulation's)
    if (g_identCanon)
        Log("lockstep: the simulation's ship identification as the battle save leaves it (type %d, no class); this PC's "
            "display had identified %d ship(s) during the load and keeps that", g_idTypeDefault, g_identReset);
    g_identCanon = false;
}

// The live battle is over on this PC: the display set stays in the ships (a single-player battle from here); the land-CAP
// slots go back to the game's layout (its own dialog and routine read slot 0 as the human side's).
static void FreezeView() {
    if (g_viewSide >= 0 && !g_loading) {
        if (!g_viewOn) WriteHumanFlags(true);
        if (E.playerSideVar) *E.playerSideVar = g_viewSide;
    }
    SwapLandCap(false);
    g_viewSide = -1;
    g_viewOn = false;
    __try {
        ReleaseDual();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ---------------------------------------------------------------------------------------------
// The live-battle rules for human sides. Some game routines treat the battle's human side (PlayerSide; canonical 0 in
// the simulation) differently from the computer's. In a live battle both sides are human, so these routines run with a
// simulation PlayerSide chosen here: from shared state only, so both PCs choose the same. Outside a live battle they
// run as the game calls them.

// A minute of a live battle runs on this PC (both PCs run it alike).
static bool LiveSimNow() { return S.on && S.st == St::Running && !g_loading && g_sim > 0 && g_ui == 0 && g_results == 0; }

// TdlgDivisionReport.FillShipList (the division report's Execute, the torpedo dialog's Close and Fire) lowers each
// listed ship's CurrentSpeed to its maximum speed in this weather (0xae1972..0xae1990): a display routine writing a
// simulation field, on the PC where the report opens only (the leak check's `<ship>.CurrentSpeed` LEAK, then a DESYNC).
// In a live battle every ship's CurrentSpeed is put back after it; the simulation lowers it at its next interval on both
// PCs (TFightingShip.ChangeSpeed).
static const int kReportSpeedsMax = 2048;
static void* g_rsShip[kReportSpeedsMax];
static int g_rsSpeed[kReportSpeedsMax];
static int g_rsN = 0;
struct SpeedChange {
    void* ship;
    int was, now;
};

static int CurrentSpeedOff() {
    static const int off = dl::Field("TFightingShip", "CurrentSpeed");
    return off;
}

static void ReportSpeedsSave() {
    g_rsN = 0;
    const int off = CurrentSpeedOff();
    if (off <= 0 || !S.on || S.st == St::Ended || g_loading) return;
    __try {
        void* all = List(E.allShipsVar);
        for (int i = 0, n = dl::ListCount(all); i < n && g_rsN < kReportSpeedsMax; i++)
            if (void* sp = dl::ListItem(all, i)) {
                g_rsShip[g_rsN] = sp;
                g_rsSpeed[g_rsN++] = dl::At<int>(sp, off);
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_rsN = 0;
    }
}

static int ReportSpeedsPutBack(SpeedChange* ch, bool* fault) {
    const int off = CurrentSpeedOff();
    int n = 0;
    __try {
        for (int i = 0; i < g_rsN; i++) {
            int& v = dl::At<int>(g_rsShip[i], off);
            if (v == g_rsSpeed[i]) continue;
            if (n < 3) {
                ch[n].ship = g_rsShip[i];
                ch[n].was = g_rsSpeed[i];
                ch[n].now = v;
            }
            n++;
            v = g_rsSpeed[i];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
    }
    g_rsN = 0;
    return n;
}

static void ReportSpeedsRestore() {
    SpeedChange ch[3] = {};
    bool fault = false;
    int n = ReportSpeedsPutBack(ch, &fault);
    for (int i = 0; i < n && i < 3; i++)
        Log("lockstep: the division report lowered %s's current speed %d -> %d on this PC only: put back (the next "
            "minute lowers it on both PCs)", W2U(ObjName(ch[i].ship)).c_str(), ch[i].was, ch[i].now);
    if (fault) Log("lockstep: exception while putting back the ships' current speeds after the division report");
}

// CheckAIAirstrike(side) skips the formations of PlayerSide's carriers (only its airfields' strikes are the computer's).
// Engine level 7: the players launch their own carrier strikes from the strike dialog, so a human side gets the
// single-player rule (PlayerSide = that side: the computer flies only its airfields' strikes); a side the computer
// commands (auto-run, a dropped player) gets 2 (no human side: the computer flies all its strikes). With the host's
// option "Computer flies carrier strikes in live battles" (S.autoStrike, from A's start message: the same on both PCs)
// both sides get 2, as before level 7.
static int AirstrikeSimSide(int side) {
    if (S.autoStrike || side < 0 || side > 1) return 2;
    return g_human[side] ? side : 2;
}

// CheckMissileFiringForAllShips: PlayerSide's ships fire only with the missile panel's "Fire at will" (each PC's own
// box) and with its "Fire mode". 2 = both sides fire under the computer's rules, whatever the panels say.
static int MissileSimSide() { return 2; }

// The AFIds of formations launched from the strike dialog in a live battle (engine level 7): side-partitioned, so both
// players can launch at the same boundary without their new formations' keys meeting (side 0: 1,000,000 + k, side 1:
// 1,100,000 + k). The game's own formations keep the AFID counter's ids (a few hundred per battle); ids are only looked
// up by a linear search (GetAirFormationById) and saved as numbers.
static const int kUiAfId = 1000000, kUiAfIdSide = 100000;
static bool IsUiAfId(int id) { return id >= kUiAfId && id < kUiAfId + 2 * kUiAfIdSide; }

// The highest AFId of the air formations now, launches from the screen not counted (the computer's new formations get
// higher ones; INT_MIN: none, INT_MAX: unreadable).
static int MaxAFId() {
    __try {
        int mx = INT_MIN;
        void* air = List(E.airVar);
        int n = dl::ListCount(air);
        for (int i = 0; i < n; i++)
            if (void* af = dl::ListItem(air, i))
                if (!IsUiAfId(dl::At<int>(af, E.fAfId))) mx = std::max(mx, dl::At<int>(af, E.fAfId));
        return mx;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return INT_MAX;
    }
}

// After CheckAIAirstrike: counts the formations it made (AFIds above maxBefore) per side, and clears every formation's
// "confirm the target when spotted" (AILaunchStrike sets it): the end of the minute would ask PlayerSide's player
// whether to adjust a strike's target, and retarget side 1's (hard-coded) and land-based strikes by the computer.
static bool AirStrikeAfter(int maxBefore, int made[2], int naval[2], int* cleared) {
    __try {
        void* air = List(E.airVar);
        int n = dl::ListCount(air);
        for (int i = 0; i < n; i++) {
            void* af = dl::ListItem(air, i);
            if (!af) continue;
            int sd = dl::At<int>(af, E.fAfSide);
            if (sd != 0 && sd != 1) continue;
            int id = dl::At<int>(af, E.fAfId);
            if (id > maxBefore && !IsUiAfId(id)) {
                made[sd]++;
                if (E.fAfMission >= 0 && dl::At<uint8_t>(af, E.fAfMission) == 3) naval[sd]++;  // amNavalAttack
            }
            if (E.fAfConfirm >= 0 && dl::At<uint8_t>(af, E.fAfConfirm)) {
                dl::At<uint8_t>(af, E.fAfConfirm) = 0;
                (*cleared)++;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

extern "C" void __stdcall LS_AirStrike(void* self, int side) {
    if (!LiveSimNow()) {
        dl::Call(g_lsOrigAirstrike, U(self), (uint32_t)side);
        return;
    }
    bool fields = E.fAfId >= 0 && E.fAfSide >= 0;  // (the counts and the confirmation need them; the rule doesn't)
    int maxBefore = fields ? MaxAFId() : INT_MAX;
    S.aiAirCalls++;
    CallWithSimSide(AirstrikeSimSide(side), g_lsOrigAirstrike, U(self), (uint32_t)side);
    if (!fields) return;
    int made[2] = {0, 0}, naval[2] = {0, 0}, cleared = 0;
    if (!AirStrikeAfter(maxBefore, made, naval, &cleared)) {
        Log("lockstep: exception while checking the air formations after CheckAIAirstrike(%d)", side);
        return;
    }
    S.confirmCleared += cleared;
    for (int k = 0; k < 2; k++) {
        S.aiAir[k] += made[k];
        S.aiNaval[k] += naval[k];
    }
    if (made[0] || made[1])
        Log("lockstep: the computer's air strikes (CheckAIAirstrike %d) made %d formation(s) for side 0 and %d for side 1 "
            "(naval attack %d/%d) at t=%s; this battle: %d/%d",
            side, made[0], made[1], naval[0], naval[1], ClockText().c_str(), S.aiAir[0], S.aiAir[1]);
}

// SetAIAirSearchPlan(False), every 2 hours after minute 600, re-plans the air search and CAP of every force except
// PlayerSide's (airfields excepted). Both sides human: no re-plan (it would undo side 1's settings); one side human:
// the game's rule with that side as PlayerSide; no side human: as the game calls it.
extern "C" void __stdcall LS_SearchPlan(void* self, int include) {
    include &= 0xFF;
    if (!LiveSimNow() || include) {
        dl::Call(g_lsOrigSearchPlan, U(self), (uint32_t)include);
        return;
    }
    if (g_human[0] && g_human[1]) {
        S.planSkipped++;
        Log("lockstep: the computer's air search re-plan skipped (both sides are human) at t=%s", ClockText().c_str());
        return;
    }
    if (g_human[0] != g_human[1]) {
        CallWithSimSide(g_human[0] ? 0 : 1, g_lsOrigSearchPlan, U(self), 0);
        return;
    }
    dl::Call(g_lsOrigSearchPlan, U(self), 0);
}

// CheckLandBasedCAP(side) in a live battle: the game's routine (read step by step in ResolveAirOps) with the airfield's
// side checked. The game gives the CappedDivision LandCAPDivision[side] to the airfield division (its flagship IsAirfield)
// nearest to the requested division (flagship to flagship, GetRangeToShip) that has a fighter formation based there
// (ScenHomeBase, the role test), of either side, so one player's request could task the other player's fighters. Here
// only side `side`'s airfields qualify; everything else is the routine's: the same game routines on the same lists in
// the same order, the same strict "nearer" test, and on an exception what its own handler does (the request is cleared).
// It runs inside the simulation on both PCs alike (no Random in it: randtree depth 6). Outside a live battle the game's
// own routine runs.

// The division the routine picks for `target`; side < 0: any side (the game's own choice, for the log).
static void* NearestCapAirfield(void* target, int side) {
    void* best = nullptr;
    int bestRange = INT_MAX;
    void* divs = List(E.divisionsVar);
    void* air = List(E.airVar);
    for (int i = 0, n = dl::ListCount(divs); i < n; i++) {
        void* dv = dl::ListItem(divs, i);
        if (dl::ListCount(dl::At<void*>(dv, E.fShips)) <= 0) continue;
        if (side >= 0 && dl::At<int>(dv, E.fDivSide) != side) continue;  // the mod's rule
        void* fs = (void*)(uintptr_t)dl::Call(E.getFlagship, U(dv));
        if (!(dl::Call(E.isAirfield, U(fs)) & 0xFF)) continue;
        int r = (int)dl::Call(E.rangeToShip, U(fs), dl::Call(E.getFlagship, U(target)));
        if (r >= bestRange) continue;
        for (int k = 0, na = dl::ListCount(air); k < na; k++) {
            void* af = dl::ListItem(air, k);
            if (dl::At<void*>(af, E.fAfHome) != fs) continue;
            if (!(dl::Call(E.fighterRole, dl::At<uint8_t>(dl::At<void*>(af, E.fAfType), E.fTypeRole)) & 0xFF)) continue;
            bestRange = r;
            best = dv;
            break;
        }
    }
    return best;
}

// The routine's work for one side (no C++ objects: an exception is caught here). False: an exception (the request is
// cleared, as the game's handler does).
static bool LandCapPick(int side, void** own, void** any) {
    void** slot = LandCapSlot(side);
    __try {
        void* target = *slot;
        if (target && dl::ListCount(dl::At<void*>(target, E.fShips)) > 0) {
            *own = NearestCapAirfield(target, side);
            *any = NearestCapAirfield(target, -1);  // (the game's own choice: only logged)
            if (*own) dl::At<void*>(*own, E.fCapped) = target;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *slot = nullptr;
        return false;
    }
}

static void LandCapForSide(int side) {
    static void *lastTarget[2], *lastOwn[2], *lastAny[2];  // (compared only: the log names a change of the assignment)
    void* target = *LandCapSlot(side);
    void *own = nullptr, *any = nullptr;
    S.landCapCalls++;
    if (!LandCapPick(side, &own, &any)) {
        Log("lockstep: exception in the land-CAP check of side %d at t=%s: the request is cleared (as the game's own error "
            "handler does)", side, ClockText().c_str());
        return;
    }
    if (any && any != own) S.landCapOther++;
    if (target == lastTarget[side] && own == lastOwn[side] && any == lastAny[side]) return;
    lastTarget[side] = target;
    lastOwn[side] = own;
    lastAny[side] = any;
    if (!target) return;
    std::string game = any && any != own ? "; the game alone would have tasked " + W2U(ObjName(any)) + " of side " +
                                               std::to_string(dl::At<int>(any, E.fDivSide))
                                         : std::string();
    Log("lockstep: land CAP of side %d over %s: %s%s (t=%s)", side, W2U(ObjName(target)).c_str(),
        own ? (W2U(ObjName(own)) + " covers it").c_str() : "no airfield of this side with fighters", game.c_str(),
        ClockText().c_str());
}

extern "C" void __stdcall LS_LandCap(int side) {
    if (!LiveSimNow() || (side != 0 && side != 1) || !E.landCap) {
        dl::Call(g_lsOrigLandCap, (uint32_t)side);
        return;
    }
    LandCapForSide(side);
}

// TForce.CheckSearchAircraft (StartMinute's CheckObjectives, every force every minute): the force's search launches from
// its own settings (the routine air ops orders). Its carriers' launches (TFightingShip.ShipLaunchSearchAircraft) ask
// PlayerSide's player "Do you want <ship> to launch scout aircraft?" when the ship sees enemy ships and the battle
// preference Floatplane launch is not Automatic. Inside a live battle's minute that question gets the fixed answer No (and
// its text, with the ship's name, would go to side 0's chat), so with the canonical PlayerSide 0 side 0's carriers stopped
// scouting while they saw enemy ships and side 1's didn't. With 2 (no human side) neither is asked: both sides' carriers
// launch as their forces' settings say, as the computer's do. Nothing else in its call tree tests PlayerSide. The
// floatplane question (LaunchFloatCheck, for ships with ForceIsHumanControlled: both sides inside the simulation) stays,
// with the same fixed answer for both sides; its text names one side's ship, so it goes to the log only (g_searchBoxes).
static int g_searchBoxes = 0;  // inside the forces' air search of a live minute (its questions are not shown)

static void SearchAirLive(void* force) {
    g_searchBoxes++;
    __try {
        CallWithSimSide(2, g_lsOrigSearchAir, U(force));
    } __finally {
        g_searchBoxes--;
    }
}

extern "C" void __stdcall LS_SearchAir(void* force) {
    if (!LiveSimNow()) {
        dl::Call(g_lsOrigSearchAir, U(force));
        return;
    }
    if (S.scoutCalls++ == 0)
        Log("lockstep: the forces' air search with the simulation PlayerSide 2 (no scout question for either side) from "
            "t=%s", ClockText().c_str());
    SearchAirLive(force);
}

// Each side's own Fire at will (engine level 9). CheckMissileFiringForAllShips runs with the simulation PlayerSide 2
// (both sides under the computer's rules, as in 0.4.0). A side whose player turned Fire at will off has its divisions
// held for the call: HoldMissileFire := 1 on those not holding already, put back in __finally. The routine skips a held
// division exactly where it skips the player's divisions while the box is unchecked (the same jump target, read in
// ResolveMissiles), and the game's exception there, a computer-controlled force of a nation below the routine's
// threshold, fires anyway here too. HoldMissileFire is read nowhere else in the routine's call tree (static scan, depth
// 12), and the order guard takes the agreed orders before the step and again after it, so the held values never become
// orders. A side the computer commands (auto-run, a dropped player) is never held. The missile panel's box is not read
// for this: during the call it holds a canonical value (unchecked) on both PCs, and with PlayerSide 2 its value doesn't
// change the routine's path anyway.
static void* g_mslHeld[512];
static int g_mslHeldN = 0;

static void MissileHoldEnd() {
    for (int i = 0; i < g_mslHeldN; i++) dl::At<uint8_t>(g_mslHeld[i], E.fHoldMsl) = 0;
    g_mslHeldN = 0;
}

// The divisions to hold for this call (held[side] counts them); false: none, or an exception (nothing held).
static bool MissileHoldBegin(int held[2]) {
    g_mslHeldN = 0;
    if (g_faw[0] && g_faw[1]) return false;
    __try {
        void* divs = List(E.divisionsVar);
        int n = dl::ListCount(divs);
        for (int i = 0; i < n && g_mslHeldN < 512; i++) {
            void* dv = dl::ListItem(divs, i);
            if (!dv) continue;
            int side = dl::At<int>(dv, E.fDivSide);
            if ((side != 0 && side != 1) || g_faw[side] || !g_human[side] || dl::At<uint8_t>(dv, E.fHoldMsl)) continue;
            void* ships = dl::At<void*>(dv, E.fShips);
            void* force = dl::At<void*>(dv, E.fMyForce);
            if (dl::ListCount(ships) <= 0 || !force) continue;  // (the routine passes these by, or stops at them)
            if (dl::At<uint8_t>(force, E.fFAI)) {  // the routine's flagship (GetFlagship: Ships[0]) and its nation's value
                void* flag = dl::ListItem(ships, 0);
                int nat = flag ? dl::At<int>(flag, E.fShipNation) : -1;
                if (nat >= 0 && nat < 64 && *(int*)(E.nations + nat * E.nationStride + E.nationGate) < E.nationGateMin)
                    continue;
            }
            dl::At<uint8_t>(dv, E.fHoldMsl) = 1;
            g_mslHeld[g_mslHeldN++] = dv;
            held[side]++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        MissileHoldEnd();
        held[0] = held[1] = -1;
        return false;
    }
    return g_mslHeldN > 0;
}

// The routine with PlayerSide 2, the holds and the box's canonical value (no C++ objects here: __finally).
static void MissileFireLive(void* self, int held[2]) {
    void* m = BattleForm();
    void* cb = m && E.fCbFireAtWill > 0 && E.fCbState > 0 ? dl::At<void*>(m, E.fCbFireAtWill) : nullptr;
    uint8_t shown = cb ? dl::At<uint8_t>(cb, E.fCbState) : 0;
    if (cb) dl::At<uint8_t>(cb, E.fCbState) = 0;  // cbUnchecked
    bool hold = g_featFireAtWill && MissileHoldBegin(held);
    __try {
        CallWithSimSide(MissileSimSide(), g_lsOrigMissileFire, U(self));
    } __finally {
        if (hold) MissileHoldEnd();
        if (cb) dl::At<uint8_t>(cb, E.fCbState) = shown;
    }
}

extern "C" void __stdcall LS_MissileFire(void* self) {
    if (!LiveSimNow()) {
        dl::Call(g_lsOrigMissileFire, U(self));
        return;
    }
    if (S.missileCalls++ == 0)
        Log("lockstep: missile fire under the computer's rules for both sides (simulation PlayerSide %d)%s from t=%s",
            MissileSimSide(), g_featFireAtWill ? ", each side's own Fire at will" : "", ClockText().c_str());
    int held[2] = {0, 0};
    MissileFireLive(self, held);
    if (held[0] < 0) {
        Log("lockstep: exception while holding the missile fire of a side at t=%s (nothing held this minute)",
            ClockText().c_str());
        held[0] = held[1] = 0;
    }
    for (int k = 0; k < 2; k++) {
        if (held[k] > 0) S.mslHeldCalls[k]++;
        if (held[k] != S.mslHeld[k])
            Log("lockstep: automatic missile fire of side %d: %s (%d division(s) held) from t=%s", k,
                held[k] ? "held (its Fire at will is off)" : "at will", held[k], ClockText().c_str());
        S.mslHeld[k] = held[k];
    }
}

// UpdateMissilePanel (a display context) counts PlayerSide's missile ships and the enemy ships in their range, but the
// enemy is side 1, hard-coded (0xe03376 in 1.01.44): right for the PC that shows side 0. On the PC that shows side 1 it
// would count that side's own ships as targets, so it doesn't run there (the panel keeps what it shows).
extern "C" int __stdcall LS_MissilePanelAllowed() {
    if (g_viewSide != 1) return 1;
    static int lastBid = -1;
    if (lastBid != S.bid) {
        lastBid = S.bid;
        Log("lockstep: the missile panel's counts are not updated on this PC (side 1)");
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Battle preferences in a live battle (engine level 15). The game's preferences record (TdlgPrefs.LoadPrefs fills it
// from the Preferences file at the game's start) holds the battle options next to many display ones. Every read of it
// was classified by whether the battle load or the simulation reaches it and what the read decides (GAME-INTERNALS,
// "Two PCs: the preferences, ..."): these are the ones whose value changes the loaded battle, the simulation's state or
// its shared random numbers (some of the minute's messages and sounds draw shared random numbers only when they are
// on). B uses A's values of these while the live battle lasts; every other preference stays each PC's own (until
// level 14, B also took A's display options: sound volume, dark mode, window state, range circles, ...).
struct PrefSpec {
    int off, len;
    const char* key;
};
static const PrefSpec kPrefs[] = {
    {0x0c, 4, "AIControl"},             // every battle load: SetInitialDivAIControlFromUserPrefs (TDivision.AIControlled)
    {0x14, 4, "FloatWarning"},          // DoDamageControl writes a ship field by it
    {0x18, 1, "ReadyPopup"},            // TAirFormation.CheckStatus2 (the minute's air operations)
    {0x1c, 1, "BreakOnNewSighting"},    // the sighting routine draws a shared random number by it
    {0x25, 1, "AirApproachMessage"},    // EOTP draws a shared random number by it
    {0x2a, 4, "AirSurfaceMessageLvl"},  // TAirFormation.AttackShips draws shared random numbers by it (attack sounds)
    {0x2f, 1, "LetAIControlForce"},     // EOTP gives forces to the computer by it
    {0x3f, 1, "LOScalc"},               // the sighting routine's line of sight
    {0x42, 4, "SoundOption"},           // the minute's messages draw shared random numbers by it (sound variants)
    {0x46, 4, "Autosave"},              // EOTP saves the battle every N minutes
    {0x4a, 4, "FloatplaneLaunch"},      // the scout floatplane launches
    {0x56, 4, "RealismSetting"},        // many rules (DetachShip, the sighting routine, CheckObjectives, torpedo fire)
    {0x357, 1, "NoFriendlyTorpHits"},   // torpedo hits on own ships
    {0x37b, 4, "BSSMHC"},               // a value of a simulation routine (0xdf0d5d in 1.01.44; no dialog sets it)
};

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

static int PrefsHexLen() {
    int n = 0;
    for (auto& r : kPrefs) n += r.len * 2;
    return n;
}
static std::string PrefsHex() {
    std::string s;
    if (!E.prefs) return s;
    for (auto& r : kPrefs) s += Hex(E.prefs + r.off, r.len);
    return s;
}
static bool SetPrefsHex(const std::string& h) {
    if (!E.prefs || (int)h.size() != PrefsHexLen()) return false;
    size_t pos = 0;
    for (auto& r : kPrefs) {
        if (!Unhex(h.substr(pos, r.len * 2), E.prefs + r.off, r.len)) return false;
        pos += r.len * 2;
    }
    return true;
}
static int PrefValue(const std::string& h, size_t i) {  // the value of kPrefs[i] in a PrefsHex text (-1: none)
    size_t pos = 0;
    for (size_t k = 0; k < i && k < sizeof(kPrefs) / sizeof(kPrefs[0]); k++) pos += kPrefs[k].len * 2;
    uint8_t b[4] = {};
    if ((int)h.size() != PrefsHexLen() || !Unhex(h.substr(pos, kPrefs[i].len * 2), b, kPrefs[i].len)) return -1;
    return kPrefs[i].len == 1 ? b[0] : (int)(b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24);
}
// "Key a -> b, ..." for the preferences that differ between two PrefsHex texts ("" if none).
static std::string PrefsDiff(const std::string& from, const std::string& to) {
    std::string out;
    for (size_t i = 0; i < sizeof(kPrefs) / sizeof(kPrefs[0]); i++) {
        int a = PrefValue(from, i), b = PrefValue(to, i);
        if (a != b)
            out += std::string(out.empty() ? "" : ", ") + kPrefs[i].key + " " + std::to_string(a) + " -> " + std::to_string(b);
    }
    return out;
}

// The campaign's "ship types exist" flags (engine level 17). TfrmBuildCamp.CheckShipTypesExist sets three unit
// variables from every nation's ships in play: a battleship exists, a battlecruiser exists, a carrier (CV or CVL)
// exists. Only the month's processing calls it (ReduceTimeLimits, from DoTurn and DoStrategicTurn), so they hold what
// this PC's last processed month left: the host's game has the campaign's, a joined player's game (which never processes
// a month) 0 for all three. The battle's identification reads them (TFightingShip.SetMistakenType, IdentifyType: a
// battleship is mistaken for a battlecruiser only if one exists, else for a heavy cruiser; a carrier for a heavy cruiser
// if none exists; an extra Random(100) when no battleship exists), so the two PCs identified ships differently and then
// drew different random numbers (0.4.1 RC: the lockstep_strike DESYNCs at SetMistakenType). The start message carries
// A's (`st=`); B uses them for the load and the battle, and gets its own back when the battle ends.
static uint8_t* g_typeFlags[3] = {};
static bool g_typeFlagsSync = true;  // TEST ONLY (`ls shiptypes nosync`): B keeps its own flags (as 0.4.1 RC)
static bool ResolveTypeFlags() {
    static bool tried = false;
    if (!tried) {
        tried = true;
        // mov esi, eax; then three times: mov eax, [cell]; mov byte ptr [eax], 0
        uint8_t* h = Find(dl::Method("TfrmBuildCamp", "CheckShipTypesExist"), 0x20,
                          {0x8B, 0xF0, 0xA1, -1, -1, -1, -1, 0xC6, 0x00, 0x00, 0xA1, -1, -1, -1, -1, 0xC6, 0x00, 0x00,
                           0xA1, -1, -1, -1, -1, 0xC6, 0x00, 0x00});
        for (int i = 0; h && i < 3; i++) g_typeFlags[i] = (uint8_t*)VarFromCell(h + 3 + 8 * i);
        bool ok = g_typeFlags[0] && g_typeFlags[1] && g_typeFlags[2];
        if (!ok) g_typeFlags[0] = g_typeFlags[1] = g_typeFlags[2] = nullptr;
        Log("lockstep: the ship-type flags (CheckShipTypesExist): %s", ok ? "found" : "NOT FOUND (each PC keeps its own)");
    }
    return g_typeFlags[0] != nullptr;
}
static std::string TypeFlagsHex() {  // "" if not found
    if (!ResolveTypeFlags()) return "";
    uint8_t b[3] = {*g_typeFlags[0], *g_typeFlags[1], *g_typeFlags[2]};
    return Hex(b, 3);
}
static bool SetTypeFlagsHex(const std::string& h) {
    uint8_t b[3];
    if (h.size() != 6 || !ResolveTypeFlags() || !Unhex(h, b, 3)) return false;
    for (int i = 0; i < 3; i++) *g_typeFlags[i] = b[i];
    return true;
}
static bool TypeFlagsNow(uint8_t out[3]) {
    if (!ResolveTypeFlags()) return false;
    for (int i = 0; i < 3; i++) out[i] = *g_typeFlags[i];
    return true;
}

// TEST ONLY: two-PC conditions on one PC (docs/TESTING.md "Two-PC conditions"). Applied on the main thread when the mod
// starts and after every run of the game's Preferences reader (game.cpp hooks it when RTW3MP_TEST_PREFS is set, and its
// writer does nothing then: the copy never writes the shared file).
//   RTW3MP_TEST_PREFS=Key=Value;Key=Value   this copy's own preferences (in memory only)
//   RTW3MP_TEST_FPUCW=<hex>                 this copy's x87 control word (another PC's DLLs may change it)
//   RTW3MP_TEST_LOCALE=comma                this copy's number and date separators as a comma locale's (de-DE)
static const struct {
    const char* key;
    int off, len;
} kPrefKeys[] = {
    {"BreakOnSighting", 0x0, 4}, {"BreakOnId", 0x4, 4}, {"OpenFireMsg", 0x8, 4}, {"AIControl", 0xc, 4},
    {"BreakOnHit", 0x10, 4}, {"FloatWarning", 0x14, 4}, {"ReadyPopup", 0x18, 1}, {"LoseContactPopup", 0x19, 1},
    {"AutoSubMoves", 0x1a, 1}, {"AutoASWMoves", 0x1b, 1}, {"BreakOnNewSighting", 0x1c, 1}, {"BreakOnTorpedoHit", 0x1d, 1},
    {"BreakOnMissileHit", 0x1e, 1}, {"BreakOnSinking", 0x1f, 1}, {"BreakOnSunk", 0x20, 1}, {"BreakOnSubsighting", 0x21, 1},
    {"BreakOnAirAttackOnShips", 0x22, 1}, {"BreakOnAirAttackOnLand", 0x23, 1}, {"BreakOnAirCombat", 0x24, 1},
    {"AirApproachMessage", 0x25, 1}, {"AirCombatMessageLvl", 0x26, 4}, {"AirSurfaceMessageLvl", 0x2a, 4},
    {"LargeBattles", 0x2e, 1}, {"LetAIControlForce", 0x2f, 1}, {"FOW", 0x30, 4}, {"DamageMeter", 0x34, 1},
    {"CourseIndicator", 0x35, 1}, {"ShipFlags", 0x36, 1}, {"Compass", 0x37, 1}, {"BaseFlags", 0x38, 1},
    {"DarkMode", 0x39, 4}, {"GridLines", 0x3d, 1}, {"CourseClick", 0x3e, 1}, {"LOScalc", 0x3f, 1},
    {"ConvertCharacters", 0x40, 1}, {"SpaceBarRestarts", 0x41, 1}, {"SoundOption", 0x42, 4}, {"Autosave", 0x46, 4},
    {"FloatplaneLaunch", 0x4a, 4}, {"BackgroundSound", 0x4e, 4}, {"SoundVolume", 0x52, 4}, {"RealismSetting", 0x56, 4},
    {"HistoricalForces", 0x5a, 1}, {"MainFormState", 0x65, 4}, {"Scale", 0x34e, 1}, {"FFRLow", 0x356, 1},
    {"NoFriendlyTorpHits", 0x357, 1}, {"EnglishRanks", 0x358, 1}, {"AutoAssignCO", 0x364, 1}, {"SupportForce", 0x36e, 1},
    {"BSSMHC", 0x37b, 4},
};

static std::string EnvStr(const char* name) {
    char buf[512];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) ? std::string(buf, n) : std::string();
}
static unsigned FpuCw();
static void SetFpuCw(unsigned v);

bool TestPrefsWanted() { return !EnvStr("RTW3MP_TEST_PREFS").empty(); }

void ApplyTestSwitches(const char* when) {
    static bool fpuDone = false, localeDone = false;
    std::string prefs = EnvStr("RTW3MP_TEST_PREFS");
    if (!prefs.empty()) {
        if (!Resolve() || !E.prefs) {
            Log("TEST: RTW3MP_TEST_PREFS not applied (%s): the preferences record was not found", when);
        } else {
            std::string done;
            size_t pos = 0;
            while (pos < prefs.size()) {
                size_t e = prefs.find(';', pos);
                std::string kv = prefs.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
                pos = e == std::string::npos ? prefs.size() : e + 1;
                size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                std::string k = kv.substr(0, eq);
                int v = atoi(kv.c_str() + eq + 1);
                bool known = false;
                for (auto& p : kPrefKeys)
                    if (!_stricmp(p.key, k.c_str())) {
                        known = true;
                        int old = p.len == 1 ? E.prefs[p.off] : *(int*)(E.prefs + p.off);
                        if (p.len == 1)
                            E.prefs[p.off] = (uint8_t)v;
                        else
                            *(int*)(E.prefs + p.off) = v;
                        done += " " + k + " " + std::to_string(old) + "->" + std::to_string(v);
                    }
                if (!known) done += " " + k + "(unknown)";
            }
            Log("TEST: RTW3MP_TEST_PREFS (%s): this copy's preferences in memory:%s (never written to the file)", when,
                done.c_str());
        }
    }
    std::string cw = EnvStr("RTW3MP_TEST_FPUCW");
    if (!cw.empty() && !fpuDone && GetCurrentThreadId() == MainThreadId()) {
        fpuDone = true;
        unsigned before = FpuCw(), v = (unsigned)strtoul(cw.c_str(), nullptr, 16);
        SetFpuCw(v);
        Log("TEST: RTW3MP_TEST_FPUCW: the main thread's x87 control word %04x -> %04x", before, FpuCw());
    }
    std::string loc = EnvStr("RTW3MP_TEST_LOCALE");
    if (!loc.empty() && !localeDone) {
        localeDone = true;
        // TCustomIniFile.ReadFloat: mov edx, [<FormatSettings cell>]; mov eax, [ebp-4]; call StrToFloat
        uint8_t* h = Find(dl::Method("TCustomIniFile", "ReadFloat"), 0x80, {0x8B, 0x15, -1, -1, -1, -1, 0x8B, 0x45, 0xFC, 0xE8});
        uint8_t* fs = h && InImage(*(uint32_t*)(h + 2)) ? (uint8_t*)(uintptr_t) * (uint32_t*)(uintptr_t) * (uint32_t*)(h + 2) : nullptr;
        if (!fs || !InImage((uintptr_t)fs)) {
            Log("TEST: RTW3MP_TEST_LOCALE not applied: FormatSettings not found");
        } else {
            // TFormatSettings (this RTL): DateSeparator +6, TimeSeparator +8, ListSeparator +0xA, ThousandSeparator
            // +0xC0, DecimalSeparator +0xC2 (TextToFloat reads +0xC2)
            wchar_t* d = (wchar_t*)(fs + 0xC2);
            wchar_t before = *d;
            *(wchar_t*)(fs + 0xC0) = L'.';
            *d = L',';
            *(wchar_t*)(fs + 0x6) = L'.';
            *(wchar_t*)(fs + 0xA) = L';';
            Log("TEST: RTW3MP_TEST_LOCALE=%s: decimal separator '%lc' -> ',', thousands '.', date '.', list ';'",
                loc.c_str(), before ? before : L'?');
        }
    }
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
// The x87 control word must not change during a battle (rounding/precision decide hits and positions). Engine level 16:
// checked around every simulation step and every hash on both PCs (A's word: S.cw0); each difference is logged (the
// first 20, then every 100th) and counted for the READY diagnostics.
static void CheckFpu(const char* where) {
    unsigned cw = FpuCw();
    if (S.cw0 && cw != S.cw0) {
        if (++S.cwFixes <= 20 || S.cwFixes % 100 == 0)
            Log("lockstep: FPU control word %04x at %s, the battle's (A's) is %04x: put back (%d times in this battle)", cw,
                where, S.cw0, S.cwFixes);
        SetFpuCw(S.cw0);
    }
}

// ---------------------------------------------------------------------------------------------
// The order guard. The order fields of every division, ship and force are snapshot when a simulation step ends
// ("agreed"). Before the next step, any difference on this PC's own side is a player's order: it becomes a numbered
// batch for the other PC, and the agreed value is put back so the simulation never runs with an order the other PC
// doesn't have. Between steps the player keeps seeing his pending orders (overlay). A release applies the batches it
// names on both PCs in a fixed order (ApplyBatches): the other PC's immediate actions, then side 0's batches and then
// side 1's, each in the order its player gave them: values (V) and deferred actions (D).

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
    // engine level 7: the strike dialog's Stand down returns the formation's torpedoes and missiles to its carrier
    {"TFightingShip", "AirTorpedoes", 4},         {"TFightingShip", "AirToSurfaceMissiles", 4},
    // engine level 9: the missile dialog writes the heavy SSM target of a ship whose launcher can't bear (12 bytes, a
    // TPointLL; checked against the layout in ResolveOrderFields)
    {"TFightingShip", "PendingMissileTarget", 12},
};
static OField g_oForce[] = {
    {"TForce", "Course", 4},         {"TForce", "Speed", 4},          {"TForce", "ForceWithdraw", 4},
    {"TForce", "AIControlled", 1},   {"TForce", "Rallied", 1},        {"TForce", "FlotillaAttack", 1},
    {"TForce", "DoNotEnterPort", 1}, {"TForce", "ForceObjective", 12}, {"TForce", "TimeToTurnAway", 4},
    {"TForce", "HasChangedLead", 1},
    // engine level 6: the routine air ops dialog's commit (its nine fields and ProcessSearchDirectons' sectors; size 0:
    // the static array, sized from the layout). Not SearchPatternCurrent: the simulation's own count-downs per sector.
    {"TForce", "CapLevel", 4},       {"TForce", "NightCAP", 1},       {"TForce", "SearchLimitLeft", 4},
    {"TForce", "SearchLimitRight", 4}, {"TForce", "SearchDistance", 4}, {"TForce", "SearchInterval", 4},
    {"TForce", "TwoPhaseSearch", 1}, {"TForce", "RepeatSearch", 1},   {"TForce", "NightSearch", 1},
    {"TForce", "SearchPatternOrdered", 0},
};
// The land-CAP requests (engine level 6): two pseudo-objects "Llandcap#0" and "#1" whose address is the slot of
// LandCAPDivision for side 0 and side 1 in the game's layout (LandCapSlot) and whose one field is the division there.
static OField g_oLand[] = {{"", "LandCAPDivision", -2}};
// Air formations (engine level 7): key "A<AFId>#0" (AFIds are unique), side = TAirFormation.Side. The fields the strike
// dialog writes (its popups, Launch after LaunchSomeAircraft, Ready, Stand down, the list's number) and the ones the
// launch's post-processing sets on the new formation; size 12 = a TPointLL (iLongX, iLatY, iHeightZ).
static OField g_oAir[] = {
    {"TAirFormation", "Mission", 1},          {"TAirFormation", "Loadout", 4},
    {"TAirFormation", "LoadedNumber", 4},     {"TAirFormation", "OrderedState", 1},
    {"TAirFormation", "Status", 1},           {"TAirFormation", "StatusCount", 4},
    {"TAirFormation", "GroupIndex", 4},       {"TAirFormation", "LeadAirFormation", -2},
    {"TAirFormation", "CombinedStrikeSpeed", 4}, {"TAirFormation", "PriorityTarget", 1},
    {"TAirFormation", "ConfirmTargetLoc", 1}, {"TAirFormation", "TargetLoc", 12},
    {"TAirFormation", "Location", 12},        {"TAirFormation", "AircraftNumber", 4},
};
static const int kOAirConfirm = 10;  // g_oAir's ConfirmTargetLoc
// Each side's Fire at will (engine level 9): two pseudo-objects "Mfireatwill#0" and "#1" whose address is g_faw[side]
// and whose one field is that side's setting (only that side's player edits it: the Fire at will box of that PC).
static OField g_oMsl[] = {{"", "FireAtWill", 1, 0}};
static bool g_oResolved = false;

static void ResolveOrderFields() {
    if (g_oResolved) return;
    g_oResolved = true;
    for (auto& f : g_oDiv) f.off = dl::Field(f.cls, f.name);
    for (auto& f : g_oShip) {
        f.off = dl::Field(f.cls, f.name);
        // (a TPointLL only where the layout says 12 bytes: PendingMissileTarget .. Sunk)
        if (!strcmp(f.name, "PendingMissileTarget") && f.off >= 0 && dl::Field(f.cls, "Sunk") - f.off != f.size) f.off = -1;
    }
    for (auto& f : g_oForce) {
        f.off = dl::Field(f.cls, f.name);
        if (f.size == 0 && (f.size = ForceArraySize(f.name)) == 0) f.off = -1;  // a static array: sized from the layout
    }
    // (a TPointLL only where the layout says 12 bytes: Location .. Side, TargetLoc .. Status)
    for (auto& f : g_oAir) {
        f.off = dl::Field(f.cls, f.name);
        const char* next = !strcmp(f.name, "Location") ? "Side" : !strcmp(f.name, "TargetLoc") ? "Status" : nullptr;
        if (next && f.off >= 0 && dl::Field(f.cls, next) - f.off != f.size) f.off = -1;
    }
}

static void FieldsOf(char kind, OField*& fs, int& n) {
    if (kind == 'D') fs = g_oDiv, n = sizeof(g_oDiv) / sizeof(g_oDiv[0]);
    else if (kind == 'S') fs = g_oShip, n = sizeof(g_oShip) / sizeof(g_oShip[0]);
    else if (kind == 'A') fs = g_oAir, n = sizeof(g_oAir) / sizeof(g_oAir[0]);
    else if (kind == 'M') fs = g_oMsl, n = 1;
    else if (kind == 'L') {
        g_oLand[0].off = E.landCap ? 0 : -1;
        fs = g_oLand, n = 1;
    } else fs = g_oForce, n = sizeof(g_oForce) / sizeof(g_oForce[0]);
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
    // the land-CAP requests (side = slot), always in the game's layout (on side 1's PC the display swaps the slots)
    if (E.landCap)
        for (int s = 0; s < 2; s++) add('L', LandCapSlot(s), s, L"landcap");
    // each side's Fire at will (engine level 9; side = the side it is for)
    if (g_featFireAtWill)
        for (int s = 0; s < 2; s++) add('M', &g_faw[s], s, L"fireatwill");
    // the air formations (engine level 7): by AFId, which is unique and the same on both PCs (launches from the screen
    // get side-partitioned ids), whatever the list order
    void* air = E.fAfId >= 0 && E.fAfSide >= 0 ? List(E.airVar) : nullptr;
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* af = dl::ListItem(air, i))
            add('A', af, dl::At<int>(af, E.fAfSide), std::to_wstring(dl::At<int>(af, E.fAfId)));
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
// "Confirm target location when spotted" is not available in a live battle (the game would ask in the middle of a
// minute, and retarget only side 1's strikes by the computer): a formation of this side that got it outside the
// simulation (the strike dialog's Launch sets it from its check box after LaunchSomeAircraft) has it cleared here,
// before the edits are read, so it never becomes an order (the other PC's replay makes the formation without it).
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
        if (o.kind == 'A' && fs[kOAirConfirm].off >= 0 && dl::At<uint8_t>(o.p, fs[kOAirConfirm].off)) {
            auto ag = g_agreed.find(FK(o.key, kOAirConfirm));
            if (ag != g_agreed.end() && ag->second == "00") {
                dl::At<uint8_t>(o.p, fs[kOAirConfirm].off) = 0;
                Log("lockstep: \"confirm target location\" cleared on %s (not available in a live battle)", o.key.c_str());
            }
        }
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

// Leak check (tests): the simulation's state (everything hashed except the orders, plus the order of the division and
// ship lists, the forces' division lists and each division's force and Admin) at the end of a simulation step must be
// the same at the start of the next one; a difference was made by this PC's display code or the mod (it would desync the
// two PCs). Since engine level 17 that includes the simulation's set of the identification (IdentifiedAsType: the
// display's own set is swapped out between the steps, so a change there is a display write into the simulation's set).
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
        if (E.fMyForce >= 0) Fnv(h.g[G_FORCE], (uint8_t*)dv + E.fMyForce, 4);  // force orders: membership, Admin
        if (E.fAdmin >= 0) Fnv(h.g[G_FORCE], (uint8_t*)dv + E.fAdmin, 4);
        void* ships = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(ships); j++) {
            void* sp = dl::ListItem(ships, j);
            if (!sp) continue;
            Fnv(h.g[G_COUNT], &sp, 4);
            for (auto& f : g_shipFields)
                if (f.off >= 0 && f.size > 0 && f.group != G_ORD && (f.group != G_IDENT || g_leakIdent))
                    Fnv(h.g[f.group], (uint8_t*)sp + f.off, f.size);
            std::string mb = MountBytes(sp);  // the torpedo and missile mounts (engine level 5)
            Fnv(h.g[G_AMMO], mb.data(), mb.size());
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
                if (f.off >= 0 && f.size > 0 && f.group != G_ORD) Fnv(h.g[G_AIR], (uint8_t*)a + f.off, f.size);
    if (E.afIdCounter) Fnv(h.g[G_AIR], E.afIdCounter, 4);
    if (E.scoutCount) Fnv(h.g[G_AIR], E.scoutCount, 4);
    void* forces = List(E.forcesVar);  // the forces' division lists (membership and order), their search count-downs
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* fc = dl::ListItem(forces, i)) {
            Fnv(h.g[G_FORCE], &fc, 4);
            void* fd = E.fFDivs >= 0 ? dl::At<void*>(fc, E.fFDivs) : nullptr;
            for (int j = 0; j < dl::ListCount(fd); j++) {
                void* d = dl::ListItem(fd, j);
                Fnv(h.g[G_FORCE], &d, 4);
            }
            for (auto& f : g_forceFields)
                if (f.off >= 0 && f.size > 0 && f.group == G_AIR) Fnv(h.g[G_AIR], (uint8_t*)fc + f.off, f.size);
        }
    void* subs = List(E.subsVar);  // the submarines (engine level 9)
    for (int i = 0; i < dl::ListCount(subs); i++)
        if (void* sb = dl::ListItem(subs, i))
            for (auto& f : g_subFields)
                if (f.off >= 0 && f.size > 0) Fnv(h.g[G_SUB], (uint8_t*)sb + f.off, f.size);
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
    return b + MountBytes(sp);  // the mounts last (the leak report compares them as one block)
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
            if (f.group != G_ORD && (f.group != G_IDENT || g_leakIdent) && now.compare(pos, f.size, e.second, pos, f.size) != 0 &&
                nf++ < 4)
                fields += " " + W2U(ObjName(e.first)) + "." + f.name + " " + Hex((uint8_t*)e.second.data() + pos, f.size) +
                          "->" + Hex((uint8_t*)now.data() + pos, f.size);
            pos += f.size;
        }
        if (now.compare(pos, std::string::npos, e.second, pos, std::string::npos) != 0 && nf++ < 4) {
            size_t n0 = std::min<size_t>(e.second.size() - std::min(pos, e.second.size()), 30);
            size_t n1 = std::min<size_t>(now.size() - std::min(pos, now.size()), 30);
            fields += " " + W2U(ObjName(e.first)) + ".mounts " + Hex((uint8_t*)e.second.data() + pos, (int)n0) + "->" +
                      Hex((uint8_t*)now.data() + pos, (int)n1);
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
    CheckFpu("the step's end");  // (engine level 16: a word changed during the step is logged and put back)
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

// Launches from the strike dialog (engine level 7). LaunchSomeAircraft called outside the simulation (the dialog's
// Launch and Ready, `ls airlaunch`) runs at once on this PC as a simulation context (its frame's entry and exit are the
// guard's step: earlier edits become an order batch first and the routine runs on the agreed state; the agreed state
// is taken again at its exit) and is recorded with its arguments by key: `Alaunch <source> <number> <mission> <target
// hex> <name hex> <ordered state> <loadout> <spot now> <new AFId> <ScoutCount used> <side>`. The other PC replays it
// first thing in the release's shared section (ApplyBatches), on the same agreed state. Both give the new formation
// the same side-partitioned AFId (NextUiAfId; the game's counter steps back, so the simulation's own formations keep
// the ids they would have), the same name (the recorded one: no ScoutCount there, which moves by the recorded count)
// and no target confirmation. No Random and no PlayerSide in the routine's call tree (verify_p3b.py, depth 6).
struct LaunchRec {
    int state = 0;  // 0 nothing, 1 a local launch entered the routine, 2 its new formation has the final AFId
    std::string src;
    int n = 0, mission = 0, orderedState = 0, loadout = 0, spot = 0, side = -1, newId = -1, scoutUsed = 0;
    int ctr0 = 0, scout0 = 0;
    uint8_t target[12] = {};
    std::wstring nameOut;
};
static LaunchRec g_launch;

static std::string HexStr(const std::string& s) { return Hex((const uint8_t*)s.data(), (int)s.size()); }
static std::string UnhexStr(const std::string& h) {
    std::string out(h.size() / 2, '\0');
    return h.size() % 2 == 0 && Unhex(h, (uint8_t*)&out[0], (int)out.size()) ? out : std::string();
}

// The next AFId of a launch from the screen for `side`: above the side's count and every such formation in the battle
// (a resync or a checkpoint may have loaded some); -1 when the side's range is used up.
static int NextUiAfId(int side) {
    int base = kUiAfId + side * kUiAfIdSide, k = std::max(0, S.launchK[side]);
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* af = dl::ListItem(air, i)) {
            int id = dl::At<int>(af, E.fAfId);
            if (id >= base && id < base + kUiAfIdSide) k = std::max(k, id - base + 1);
        }
    if (k >= kUiAfIdSide) return -1;
    S.launchK[side] = k + 1;
    return base + k;
}
static void NoteUiAfId(int side, int id) {
    int base = kUiAfId + side * kUiAfIdSide;
    if (side >= 0 && side <= 1 && id >= base && id < base + kUiAfIdSide) S.launchK[side] = std::max(S.launchK[side], id - base + 1);
}

// The new formation of a launch (both PCs): its AFId, the counter stepped back when the routine took the next one.
static void SetLaunchedId(void* nf, int ctr0, int newId) {
    int id = dl::At<int>(nf, E.fAfId);
    bool stepBack = *E.afIdCounter == ctr0 + 1 && id == *E.afIdCounter;
    if (!stepBack)
        Log("lockstep: WARNING the launch took AFId %d, the counter went %d -> %d (not stepped back)", id, ctr0,
            *E.afIdCounter);
    dl::At<int>(nf, E.fAfId) = newId;
    if (stepBack) (*E.afIdCounter)--;
    if (E.fAfConfirm >= 0) dl::At<uint8_t>(nf, E.fAfConfirm) = 0;
}

// LS_CtxEnter: st = aSpotNow, aLoadout, aOrderedState, aName, @aTarget (the stack arguments above the return address).
static void OnLaunchEnter(uint32_t src, uint32_t number, uint32_t mission, const uint32_t* st) {
    g_launch = LaunchRec();
    if (!(S.on && S.st == St::Running && !g_loading && g_manual == 0 && E.afIdCounter && E.scoutCount)) return;
    void* s = (void*)(uintptr_t)src;
    World w;
    Walk(w);
    auto it = w.keyOf.find(s);
    g_launch.src = it != w.keyOf.end() ? it->second : "?";
    g_launch.n = (int)number;
    g_launch.mission = (int)(mission & 0xFF);
    g_launch.spot = (int)(st[0] & 0xFF);
    g_launch.loadout = (int)st[1];
    g_launch.orderedState = (int)(st[2] & 0xFF);
    if (st[4]) memcpy(g_launch.target, (const void*)(uintptr_t)st[4], 12);
    g_launch.ctr0 = *E.afIdCounter;
    g_launch.scout0 = *E.scoutCount;
    g_launch.state = 1;
    if (S.minuteRunning) Log("lockstep: WARNING a launch from the screen during a minute (desync likely)");
}

// LS_CtxExit, before the frame's exit takes the agreed state: the final AFId.
static void OnLaunchPreExit(void* nf) {
    if (g_launch.state != 1) return;
    g_launch.state = 0;
    if (!nf || !dl::IsInstanceOf(nf, "TAirFormation")) {  // (no undamaged aircraft: the routine changed nothing)
        Log("lockstep: launch from the screen from %s made no formation", g_launch.src.c_str());
        return;
    }
    int side = dl::At<int>(nf, E.fAfSide);
    int nid = side == 0 || side == 1 ? NextUiAfId(side) : -1;
    if (nid < 0)  // (the side's range used up: the game's id stays, here and in the replay, which then may resync)
        Log("lockstep: WARNING launch from %s: no AFId for side %d (kept %d)", g_launch.src.c_str(), side,
            dl::At<int>(nf, E.fAfId));
    else
        SetLaunchedId(nf, g_launch.ctr0, nid);
    if (side != S.mySide) Log("lockstep: WARNING launch from %s for side %d on side %d's PC", g_launch.src.c_str(), side, S.mySide);
    g_launch.side = side;
    g_launch.newId = nid;
    g_launch.scoutUsed = *E.scoutCount - g_launch.scout0;
    g_launch.nameOut = E.fAfName >= 0 ? dl::ReadUStr(dl::At<void*>(nf, E.fAfName)) : L"";
    g_launch.state = 2;
}

// LS_CtxExit, after the frame's exit: the action item.
static void OnLaunchExit() {
    if (g_launch.state != 2) {
        g_launch.state = 0;
        return;
    }
    g_launch.state = 0;
    const LaunchRec& l = g_launch;
    std::string item = "Alaunch\x1f" + l.src + "\x1f" + std::to_string(l.n) + "\x1f" + std::to_string(l.mission) + "\x1f" +
                       Hex(l.target, 12) + "\x1f" + HexStr(W2U(l.nameOut)) + "\x1f" + std::to_string(l.orderedState) +
                       "\x1f" + std::to_string(l.loadout) + "\x1f" + std::to_string(l.spot) + "\x1f" +
                       std::to_string(l.newId) + "\x1f" + std::to_string(l.scoutUsed) + "\x1f" + std::to_string(l.side);
    std::vector<std::string> items{item};
    QueueBatch(items, "action");
    S.launches++;
    Log("lockstep: launch from the screen: %s -> AF%d '%s' (%d aircraft, mission %d, ordered state %d, loadout %d, spot "
        "%d) at t=%s",
        l.src.c_str(), l.newId, W2U(l.nameOut).c_str(), l.n, l.mission, l.orderedState, l.loadout, l.spot,
        ClockText().c_str());
}

// The other PC's launch (inside the release's shared section): `launch <source> <number> <mission> <target> <name>
// <state> <loadout> <spot> <AFId> <ScoutCount used> <side>`.
// LaunchSomeAircraft for `actor`'s player: its only box ("Warning! Air formation ... has target 0,0!") is theirs.
static void* CallLaunch(void* src, int n, int mission, const uint32_t* st, int actor, bool* ok) {
    void* nf = nullptr;
    int prev = g_dfActor;
    __try {
        g_dfActor = actor;
        nf = (void*)(uintptr_t)SafeCall(E.launchFn, U(src), (uint32_t)n, (uint32_t)mission, 5, st, ok);
    } __finally {
        g_dfActor = prev;
    }
    return nf;
}

static bool ReplayLaunch(const std::vector<std::string>& a, const World& w) {
    auto it = w.objOf.find(a[1]);
    void* src = it == w.objOf.end() ? nullptr : it->second;
    int side = atoi(a[11].c_str()), newId = atoi(a[9].c_str()), scoutUsed = atoi(a[10].c_str());
    uint8_t target[12] = {};
    std::wstring name = U2W(UnhexStr(a[5]));
    const char* why = !E.launchFn || !E.afIdCounter || !E.scoutCount      ? "the routine is not available here"
                      : !src || !dl::IsInstanceOf(src, "TAirFormation") ? "the source formation is gone"
                      : dl::At<int>(src, E.fAfSide) != side            ? "the source formation is on the other side"
                      : !Unhex(a[4], target, 12)                        ? "a damaged record"
                                                                        : nullptr;
    if (why) {
        S.launchFailed++;
        Log("lockstep: replayed launch from %s NOT carried out: %s", a[1].c_str(), why);
        return false;
    }
    dl::ConstUStr nm(name);
    uint32_t st[5] = {U(target), name.empty() ? 0u : nm.ptr(), (uint32_t)atoi(a[6].c_str()), (uint32_t)atoi(a[7].c_str()),
                      (uint32_t)atoi(a[8].c_str())};
    int ctr0 = *E.afIdCounter;
    bool ok = false;
    void* nf = CallLaunch(src, atoi(a[2].c_str()), atoi(a[3].c_str()), st, side, &ok);
    if (!ok || !nf || !dl::IsInstanceOf(nf, "TAirFormation")) {
        S.launchFailed++;
        Log("lockstep: replayed launch from %s made no formation (%s), the other PC AF%d", a[1].c_str(),
            ok ? "nothing to launch" : "an exception", newId);
        return false;
    }
    if (newId >= 0) SetLaunchedId(nf, ctr0, newId);  // (-1: the launching PC kept the game's id)
    if (scoutUsed > 0 && !name.empty()) *E.scoutCount += scoutUsed;  // (an empty name: the routine numbered it here)
    NoteUiAfId(side, newId);
    S.launchReplays++;
    Log("lockstep: replayed launch from %s -> AF%d '%s'", a[1].c_str(), newId, W2U(name).c_str());
    return true;
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
    if (a[0] == "launch") return a.size() >= 12 && ReplayLaunch(a, w);
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

// ---------------------------------------------------------------------------------------------
// Deferred actions (engine level 3). A command of the battle screen that both PCs must carry out at the same point of
// the battle (change force, change lead, battle turn-away) is not carried out on the PC where it is given: it is recorded
// with the keys of its objects, the values its dialog chose and the boundary it was given for, and sent in this PC's
// next batch (after the player's earlier order edits, which go into an earlier batch). Both PCs carry it out when a
// release names that batch, inside the release's shared section (simulation context, shared dice), in the order the
// player gave it (ApplyBatches). The game's own conditions are checked again then: an order that can't be carried out
// any more is skipped on both PCs and only its player is told. The game's messages of the replay go to that player only
// (g_dfActor), so the other player doesn't learn the move. A resync or the end of the live battle drops pending ones
// (the player is told). Only immediate actions (A items) count for the "both players acted" resync.

static void ResolveBattleDialogs();
static void* DialogForm(const char* cls);
static bool TryRun(const std::function<void()>& f);

struct DfKind {
    const char* name;
    bool thisBoundaryOnly;  // released at another boundary than the one it was given for: dropped on both PCs
};
// The force orders apply at whichever release carries them; so does auto-run (`autorun <side> on|home|off`). A torpedo
// salvo (`torp`) and a missile salvo (`msl`, engine level 9) only at the release of the boundary they were given at:
// their firing solution is for that moment.
static const DfKind kDfKinds[] = {{"chforce", false}, {"lead", false}, {"turnaway", false}, {"autorun", false},
                                  {"torp", true},     {"msl", true}};

static const DfKind* DfFind(const std::string& kind) {
    for (auto& k : kDfKinds)
        if (kind == k.name) return &k;
    return nullptr;
}

// The object name in a key ("D1st Division#0" -> "1st Division").
static std::string KeyName(const std::string& key) {
    size_t h = key.rfind('#');
    if (key.size() < 2 || h == std::string::npos || h < 1) return key;
    return key.substr(1, h - 1);
}

static std::string DfText(const std::string& kind, const std::vector<std::string>& a) {
    if (a.size() >= 2) {
        if (kind == "chforce") return "Change of force (" + KeyName(a[0]) + " to " + KeyName(a[1]) + ")";
        if (kind == "lead") return "Change of lead division (" + KeyName(a[0]) + ", new lead " + KeyName(a[1]) + ")";
        if (kind == "turnaway") return "Battle turn-away (" + KeyName(a[0]) + ", new lead " + KeyName(a[1]) + ")";
        if (kind == "autorun")
            return a[1] == "off"    ? "Auto-run off"
                   : a[1] == "home" ? "Auto-run on, all forces to the closest friendly base"
                                    : "Auto-run on";
        if (kind == "torp") return "Torpedo salvo of " + KeyName(a[0]);  // (the target by its true name: logs only)
        if (kind == "msl") return "Missile salvo of " + KeyName(a[0]);   // (likewise)
    }
    return kind;
}

struct DfPending {
    int seq;
    std::string kind;
    int minute;
    std::vector<std::string> args;
};

// This PC's deferred actions that no release has carried yet, in the order they were given.
static std::vector<DfPending> PendingDeferred() {
    std::vector<DfPending> out;
    for (auto& b : S.myBatches)
        for (auto& it : b.items)
            if (!it.empty() && it[0] == 'D') {
                std::vector<std::string> p = Split(it.substr(1), '\x1f');
                if (p.size() >= 2)
                    out.push_back({b.seq, p[0], atoi(p[1].c_str()), std::vector<std::string>(p.begin() + 2, p.end())});
            }
    return out;
}

// The pending deferred actions are about to be dropped: tell this PC's player which. On side 1's PC its launches from
// the strike dialog that no release carried yet are lost too (a new snapshot comes from A, without them; side 0's are
// in it).
static void ReportDroppedDeferred(const char* why) {
    std::string list;
    for (auto& d : PendingDeferred()) list += (list.empty() ? "" : "; ") + DfText(d.kind, d.args);
    if (!S.isA)
        for (auto& b : S.myBatches)
            for (auto& it : b.items)
                if (it.compare(0, 8, "Alaunch\x1f") == 0) {
                    std::vector<std::string> p = Split(it.substr(1), '\x1f');
                    std::string nm = p.size() > 5 ? UnhexStr(p[5]) : std::string();
                    list += (list.empty() ? "" : "; ") + std::string(p.size() > 6 && p[6] == "2" ? "Ready " : "Launch ") +
                            (nm.empty() ? std::string("of aircraft") : nm);
                }
    if (!list.empty()) Chat(std::string("Not carried out (") + why + "): " + list + ". Give the order again.");
}

static bool IsDivision(void* p) { return p && dl::IsInstanceOf(p, "TDivision"); }
static bool IsForce(void* p) { return p && dl::IsInstanceOf(p, "TForce"); }
static int ShipCount(void* dv) { return dl::ListCount(dl::At<void*>(dv, E.fShips)); }

static int IndexIn(void* list, void* item) {
    int n = dl::ListCount(list);
    for (int i = 0; i < n; i++)
        if (dl::ListItem(list, i) == item) return i;
    return -1;
}

static bool HasShips(void* force) {
    void* fd = dl::At<void*>(force, E.fFDivs);
    for (int i = 0; i < dl::ListCount(fd); i++)
        if (void* d = dl::ListItem(fd, i))
            if (ShipCount(d) > 0) return true;
    return false;
}

// The division a ship belongs to (as the game's unnamed GetDivisionOfShip 0xd79a48: the first division of the battle's
// division list whose ships contain it).
static void* DivisionOfShip(void* ship) {
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++)
        if (void* d = dl::ListItem(divs, i))
            if (IndexIn(dl::At<void*>(d, E.fShips), ship) >= 0) return d;
    return nullptr;
}

// Deferred actions are given from the battle screen of a running live battle, outside the simulation and the mod's own
// shared sections.
static bool CanOrderNow(std::string& err) {
    if (!S.on || S.st != St::Running || g_loading)
        err = "The live battle is not running yet; give the order again when it runs.";
    else if (g_sim > 0 || g_manual > 0 || S.inAction)
        err = "The battle is busy; give the order again.";
    else
        return true;
    return false;
}

// The battle is paused between two minutes (no minute runs, A's clock doesn't run on): Detach and torpedo salvoes.
static bool BetweenMinutes() { return S.st == St::Running && !S.minuteRunning && !S.inAction && S.runLeft <= 0; }

// Records a deferred action of this PC's player; err says why not. said: the chat line (default: "<what> ordered; it
// takes effect when the next minute starts."); chat = false: none (the caller says it).
static bool QueueDeferred(const std::string& kind, const std::vector<std::string>& args, std::string& err,
                          const std::string& said = "", bool chat = true) {
    if (!CanOrderNow(err)) return false;
    DetectEdits("before a deferred action");  // the player's earlier orders go first
    // the boundary whose release is meant to carry it: this one, or the next one while a minute runs
    int minute = S.minuteRunning ? S.boundary + 1 : Minute();
    std::string item = "D" + kind + "\x1f" + std::to_string(minute);
    for (auto& a : args) item += "\x1f" + a;
    std::vector<std::string> items{item};
    QueueBatch(items, "deferred");
    if (chat) Chat(said.empty() ? DfText(kind, args) + " ordered; it takes effect when the next minute starts." : said);
    return true;
}

// Change force: this PC's division `div` into `force` (the ship tree's popup, `ls chforce`).
static bool CheckChangeForce(void* div, void* force, std::string& err) {
    if (!g_featChforce) err = "Change force is not available in this live battle.";
    else if (!IsDivision(div) || !IsForce(force)) err = "No such division or force.";
    else if (dl::At<int>(div, E.fDivSide) != S.mySide) err = "That division is not yours.";
    else if (!dl::At<void*>(div, E.fMyForce)) err = "That division is not in a force.";
    else if (dl::At<void*>(div, E.fMyForce) == force) err = "That division is in that force already.";
    else if (dl::At<int>(force, E.fForceSide) != dl::At<int>(div, E.fDivSide)) err = "That force is on the other side.";
    else return true;
    return false;
}

static bool QueueChangeForce(void* div, void* force, std::string& err) {
    if (!CheckChangeForce(div, force, err)) return false;
    World w;
    Walk(w);
    auto dk = w.keyOf.find(div), fk = w.keyOf.find(force);
    if (dk == w.keyOf.end() || fk == w.keyOf.end()) {
        err = "That division or force is not in the battle.";
        return false;
    }
    return QueueDeferred("chforce", {dk->second, fk->second}, err);
}

// Change lead / battle turn-away (Force orders, then the Select lead dialog; `ls lead`, `ls turnaway`): this PC's force
// `force` with the new lead division `div`. The checks are the dialog buttons' conditions (Status1Click).
static bool QueueForceLead(bool turnAway, void* force, void* div, std::string& err) {
    const char* kind = turnAway ? "turnaway" : "lead";
    err.clear();
    if (!g_featLead) err = "Change lead and battle turn-away are not available in this live battle.";
    else if (!IsForce(force)) err = "No force selected.";
    else if (!IsDivision(div)) err = "No division selected.";
    else if (dl::At<int>(force, E.fForceSide) != S.mySide) err = "That force is not yours.";
    else if (IndexIn(dl::At<void*>(force, E.fFDivs), div) < 0) err = "That division is not in the force.";
    else if (dl::At<uint8_t>(force, E.fFAI)) err = "The force is under computer control.";
    else if (!turnAway && dl::At<uint8_t>(force, E.fHasChangedLead)) err = "The lead of this force was changed once already.";
    else if (turnAway && dl::At<int>(force, E.fTimeToTurnAway) > 0)
        err = "A battle turn-away is possible again in " + std::to_string(dl::At<int>(force, E.fTimeToTurnAway)) +
              " minutes.";
    if (!err.empty()) return false;
    World w;
    Walk(w);
    auto fk = w.keyOf.find(force), dk = w.keyOf.find(div);
    if (fk == w.keyOf.end() || dk == w.keyOf.end()) {
        err = "That force or division is not in the battle.";
        return false;
    }
    for (auto& d : PendingDeferred())
        if (d.kind == kind && !d.args.empty() && d.args[0] == fk->second) {
            err = DfText(d.kind, d.args) + " is ordered already.";
            return false;
        }
    return QueueDeferred(kind, {fk->second, dk->second}, err);
}

// Display work after the release's shared section: the ship tree, battle dialogs a replay closed, chat lines.
static std::vector<std::function<void()>> g_afterRelease;
static std::vector<void*> g_reshow;

static void AfterRelease(std::function<void()> f) { g_afterRelease.push_back(std::move(f)); }

static void Reshow(void* form) {
    if (form && std::find(g_reshow.begin(), g_reshow.end(), form) == g_reshow.end()) g_reshow.push_back(form);
}

static void RunAfterRelease() {
    std::vector<void*> forms;
    forms.swap(g_reshow);
    std::vector<std::function<void()>> v;
    v.swap(g_afterRelease);
    for (void* f : forms)
        if (E.mShow && !game::FormVisible(f)) {
            bool ok = false;
            SafeCall(E.mShow, U(f), 0, 0, 0, nullptr, &ok);
            Log("lockstep: %s shown again after a replay closed it (%s)", dl::ClassNameOf(f), ok ? "ok" : "FAILED");
        }
    for (auto& f : v) TryRun(f);
}

// The ship tree as Changeforce2Click refills it: FillShipTree(tvShips, PlayerSide, False, nil).
static void RefillShipTree() {
    void* m = BattleForm();
    if (!m || !E.mFillShipTree || E.fTvShips < 0 || !E.playerSideVar) return;
    uint32_t st[2] = {0, 0};
    bool ok = false;
    SafeCall(E.mFillShipTree, U(m), U(dl::At<void*>(m, E.fTvShips)), (uint32_t)*E.playerSideVar, 2, st, &ok);
}

// SetForceLeadDiv / DoBattleTurnAway (Self = the battle screen, EDX = the division) for `force`: they read the force from
// SelectedForce and write the Selecting flags and close the Force orders and Select lead dialogs. That UI state is put
// back afterwards (also when an exception unwinds the call); a dialog the routine closed is shown again after the
// release's shared section. Only `actor`'s player sees the routine's messages.
static bool CallForceRoutine(void* fn, void* m, void* force, void* div, int actor, void* dlgOrders, void* dlgLead) {
    void* selForce = dl::At<void*>(m, E.fSelForce);
    void* selDiv = dl::At<void*>(m, E.fSelDiv);
    uint8_t newLead = dl::At<uint8_t>(m, E.fSelNewLead), turnAway = dl::At<uint8_t>(m, E.fSelTurnAway);
    bool ordersVis = dlgOrders && game::FormVisible(dlgOrders), leadVis = dlgLead && game::FormVisible(dlgLead);
    int prevActor = g_dfActor;
    bool ok = false;
    __try {
        dl::At<void*>(m, E.fSelForce) = force;
        g_dfActor = actor;
        SafeCall(fn, U(m), U(div), 0, 0, nullptr, &ok);
    } __finally {
        g_dfActor = prevActor;
        dl::At<void*>(m, E.fSelForce) = selForce;
        dl::At<void*>(m, E.fSelDiv) = selDiv;
        dl::At<uint8_t>(m, E.fSelNewLead) = newLead;
        dl::At<uint8_t>(m, E.fSelTurnAway) = turnAway;
    }
    if (ordersVis && !game::FormVisible(dlgOrders)) Reshow(dlgOrders);
    if (leadVis && !game::FormVisible(dlgLead)) Reshow(dlgLead);
    return ok;
}

// ---- Auto-run per player (engine level 4) ----
// The battle screen's auto-run button hands this PC's side to the computer for the rest of the battle, or gives it back:
// a deferred action `autorun <side> on|home|off` that both PCs carry out at the release. "on" is the game's own handler
// (sbAutoRunBattleClick) for that side only: GetForceFlagDivision of each of its forces and AIControlled of its flag
// divisions and of all its divisions; "home" first gives the handler's "move to closest friendly base" orders
// (ForceWithdraw 6, ForceMission tfmWithdraw) to its forces that are not computer-controlled and to those named 'Support
// Force'. Beyond the handler, which relies on the battle-wide AutoRunBattle flag and keeps PlayerSide, the side is
// commanded like the computer's own side (investigation 2026-10-17): its forces are AIControlled as well (flotilla
// attacks, survivors, port arrival and the sighting messages read TForce.AIControlled), its ships are not human-
// controlled inside the simulation (search floatplanes, damage control), and while side 0 is on auto-run the
// simulation's PlayerSide is 2 (the computer's side in every PlayerSide test: the tactical AI's "force withdraw" stances,
// carrier roles, scout launches). "off" gives every force and division of the side back to its player (AIControlled 0);
// the force orders stay. The game's AutoRunBattle flag stays off: these flags cover what it gates in the simulation, and
// with it on one PC's keys (FormKeyUp: Alt, space) would stop the game's own run timer in the middle of a minute.

static bool BothAuto() { return S.on && S.autoRun[0] && S.autoRun[1]; }
static std::string AutoText() { return std::string(S.autoRun[0] ? "1" : "0") + (S.autoRun[1] ? "1" : "0"); }

// The simulation's PlayerSide: 2 (no human side) while side 0 is on auto-run, else the canonical 0. (Side 1 is never the
// simulation's PlayerSide: the game treats it as the computer's side there anyway.)
static void SetSimSideFromAuto() {
    int s = S.on && S.autoRun[0] ? 2 : 0;
    if (s != g_simPlayerSide) Log("lockstep: the simulation's PlayerSide is %d (auto-run %s)", s, AutoText().c_str());
    g_simPlayerSide = s;
    if (E.playerSideVar && !(g_viewSide >= 0 && !g_loading && ViewInPlace())) *E.playerSideVar = s;
}

// A new live battle, or the end of one: no side on auto-run, both human, the canonical PlayerSide.
static void ResetSimSides() {
    g_simPlayerSide = 0;
    g_human[0] = g_human[1] = true;
}

// Auto-run of `side` once this PC's deferred auto-run actions that no release has carried yet are carried out.
static bool EffectiveAuto(int side) {
    bool on = S.autoRun[side];
    if (side != S.mySide) return on;
    for (auto& d : PendingDeferred())
        if (d.kind == "autorun" && d.args.size() >= 2 && d.args[0] == std::to_string(side)) on = d.args[1] != "off";
    return on;
}

// The one notice for torpedo and missile orders while the player's own ships are on auto-run (both are refused).
static const char* kAutoRunFire = "Your ships are on auto-run: the computer fires their torpedoes and missiles.";

static void* FlagDivision(void* force) {
    bool ok = false;
    void* fd = (void*)(uintptr_t)SafeCall(E.getFlagDiv, U(force), 0, 0, 0, nullptr, &ok);
    return ok && IsDivision(fd) ? fd : nullptr;
}

// This PC's auto-run button: down while its side is on auto-run (display; the game's FormShow puts it up).
static void SetAutoButton(bool down) {
    void* m = BattleForm();
    void* b = m && E.fAutoBtn > 0 ? dl::At<void*>(m, E.fAutoBtn) : nullptr;
    if (!b || !E.setDown || E.fDownOff <= 0 || !dl::IsInstanceOf(b, "TSpeedButton")) return;
    if ((dl::At<uint8_t>(b, E.fDownOff) != 0) == down) return;
    bool ok = false;
    SafeCall(E.setDown, U(b), down ? 1 : 0, 0, 0, nullptr, &ok);
}

// The computer-control flags of a side's forces in list order ("0"/"1").
static std::string ForceAiFlags(int side) {
    std::string f;
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* fc = dl::ListItem(forces, i))
            if (dl::At<int>(fc, E.fForceSide) == side) f += dl::At<uint8_t>(fc, E.fFAI) ? '1' : '0';
    return f;
}

// Both PCs, inside the release's shared section: `side` to the computer (see above). Returns the number of forces given
// the "home" orders.
static int AutoOn(int side, bool home) {
    S.preAutoF[side] = ForceAiFlags(side);  // (Auto-run off gives these back)
    int orders = 0;
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++) {
        void* f = dl::ListItem(forces, i);
        if (!f || dl::At<int>(f, E.fForceSide) != side || !FlagDivision(f)) continue;
        if (home) {
            bool give = !dl::At<uint8_t>(f, E.fFAI);
            if (dl::ReadUStr(dl::At<void*>(f, E.fForceName)).find(E.supportForce) != std::wstring::npos) give = true;
            if (give) {
                dl::At<int>(f, E.fWithdraw) = E.homeWithdraw;
                dl::At<uint8_t>(f, E.fMission) = (uint8_t)E.homeMission;
                orders++;
            }
        }
        if (void* fd = FlagDivision(f)) dl::At<uint8_t>(fd, E.fDAI) = 1;
    }
    SetSideControl(side, true, false);  // every force and division of the side, its ships (and g_human[side])
    return orders;
}

// `side` back to its player as single player's Auto-run off does it (sbAutoRunBattleClick 0xe02824: the flag division
// of every force that is not computer-controlled gets AIControlled := 0; every other division stays with the computer;
// the player takes one back with its division report's "AI controlled"). The live auto-run also gave the side's forces
// to the computer (AutoOn: the simulation's computer paths of a side); they get the control they had before it
// (S.preAutoF; unknown: all the player's). Its ships are the player's again (ForceIsHumanControlled), each with its
// division's flag. Both PCs inside the release's shared section; A also when the live battle ends with side 0 on
// auto-run (Cleanup). Returns the number of lead divisions given back.
static int AutoOff(int side) {
    const std::string pre = S.preAutoF[side];
    std::vector<void*> fl;
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* f = dl::ListItem(forces, i))
            if (dl::At<int>(f, E.fForceSide) == side) fl.push_back(f);
    bool known = pre.size() == fl.size();
    int leads = 0, kept = 0;
    for (size_t i = 0; i < fl.size(); i++) {
        uint8_t ai = known && pre[i] == '1' ? 1 : 0;
        dl::At<uint8_t>(fl[i], E.fFAI) = ai;
        if (ai) {
            kept++;
            continue;
        }
        if (void* fd = FlagDivision(fl[i])) {
            dl::At<uint8_t>(fd, E.fDAI) = 0;
            leads++;
        }
    }
    int divs = 0, ships = 0;
    void* dl0 = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(dl0); i++) {
        void* dv = dl::ListItem(dl0, i);
        if (!dv || dl::At<int>(dv, E.fDivSide) != side) continue;
        uint8_t ai = dl::At<uint8_t>(dv, E.fDAI);
        divs += ai;
        void* sh = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(sh); j++)
            if (void* s = dl::ListItem(sh, j)) {
                dl::At<uint8_t>(s, E.fHuman) = 1;
                dl::At<uint8_t>(s, E.fSDAI) = ai;
                ships++;
            }
    }
    if (side == 0 || side == 1) g_human[side] = true;
    S.preAutoF[side].clear();
    Log("lockstep: side %d back to its player as in single player: the lead divisions of %d forces; %d forces stay with "
        "the computer as before auto-run%s; %d divisions stay with the computer (%d ships) at t=%s",
        side, leads, kept, known ? "" : " (not known: none)", divs, ships, ClockText().c_str());
    return leads;
}

// The player's chat line after their Auto-run off.
static std::string AutoOffText(int leads) {
    return "You command your ships again (auto-run off). As in single player, you get each force's lead division back (" +
           std::to_string(leads) + "); the computer keeps its other divisions until you clear \"AI controlled\" in "
           "their division reports.";
}

// Carries out a deferred `autorun <side> on|home|off` of `actor`'s player (from RunDeferred); skip reports a refusal.
static void RunAutoRun(int actor, const std::vector<std::string>& a, int relN,
                       const std::function<void(const std::string&)>& skip) {
    int side = a[0] == "0" ? 0 : a[0] == "1" ? 1 : -1;
    const std::string& mode = a[1];
    bool mine = actor == S.mySide;
    if (side != actor) return skip("those are not that player's ships");
    if (!g_featAutoRun) return skip("auto-run is not available in this live battle");
    if (mode != "on" && mode != "home" && mode != "off") return skip("an order this PC doesn't know");
    if (mode == "off" && !S.autoRun[side]) return skip("the ships are not on auto-run");
    if (mode != "off" && S.autoRun[side]) return skip("the ships are on auto-run already");
    bool was = S.autoRun[side];
    int orders = 0, leads = 0;
    bool ok = TryRun([&] {
        if (mode == "off")
            leads = AutoOff(side);
        else
            orders = AutoOn(side, mode == "home");
    });
    S.autoRun[side] = mode != "off";
    SetSimSideFromAuto();
    Log("lockstep: auto-run %s for side %d at minute %d%s (%d forces ordered to the closest friendly base; auto-run "
        "%s)",
        mode.c_str(), side, relN, ok ? "" : " FAILED (an exception; the side's flags may be partly set)", orders,
        AutoText().c_str());
    std::string peer = S.peerName.empty() ? "The other player" : S.peerName;
    bool on = S.autoRun[side];
    if (mine)
        AfterRelease([on, orders, leads] {
            SetAutoButton(on);
            Chat(on ? "The computer commands your ships now (auto-run)" +
                          std::string(orders ? ", and all forces head for the closest friendly base" : "") +
                          ". Press Auto-run again to take them back."
                    : AutoOffText(leads));
        });
    else if (on != was)
        AfterRelease([peer, on] {
            Chat(on ? peer + " handed their ships to the computer (auto-run)."
                    : peer + " commands their ships again (auto-run off).");
        });
}

// Records this PC's player's auto-run order (the button after its question, `ls autorun`); err says why not.
static bool QueueAutoRun(const std::string& mode, std::string& err) {
    err.clear();
    if (!g_featAutoRun)
        err = "Auto-run is not available in this live battle.";
    else if (mode != "on" && mode != "home" && mode != "off")
        err = "Auto-run: on, home or off.";
    else if (!CanOrderNow(err))
        return false;
    else if (mode == "off" && !EffectiveAuto(S.mySide))
        err = "Your ships are not on auto-run.";
    else if (mode != "off" && EffectiveAuto(S.mySide))
        err = "Your ships are on auto-run already.";
    if (!err.empty()) return false;
    return QueueDeferred("autorun", {std::to_string(S.mySide), mode}, err);
}

// TfrmMain.sbAutoRunBattleClick in a live battle: the game's own question dialog (TdlgEventAnswer.ExecuteGeneral, shown
// as usual between minutes: the dialog policy answers it only inside the simulation), then the deferred auto-run order.
static void AutoRunClick() {
    void* dlg = E.autoDlgVar ? *E.autoDlgVar : nullptr;
    if (!dlg || !dl::IsInstanceOf(dlg, "TdlgEventAnswer")) {
        Chat("Auto-run is not available in this live battle (its question dialog was not found).");
        return;
    }
    int me = S.mySide;
    bool on = EffectiveAuto(me);
    std::string peer = S.peerName.empty() ? "The other player" : S.peerName;
    std::wstring prompt =
        on ? L"Take back command of your ships?"
           : L"Hand your ships to the computer for the rest of the battle? " + U2W(peer) +
                 (S.autoRun[1 - me] ? L"'s ships are on auto-run too: the battle then runs to its end at the fastest speed."
                                    : L" keeps commanding theirs.");
    dl::ConstUStr p(prompt), yes(L"Yes"), no(L"No"), home(L"Yes, and order all forces to move to the closest friendly base");
    bool ok = false;
    if (E.autoClearHints) SafeCall(E.autoClearHints, U(dlg), 0, 0, 0, nullptr, &ok);
    // ExecuteGeneral(Self, aPrompt, answer1; answer2, answer3, aPreselect): an empty answer hides its choice; the result
    // is the chosen answer's number (1..3), -1 without a choice
    uint32_t st[3] = {on ? no.ptr() : home.ptr(), on ? 0u : no.ptr(), (uint32_t)-1};
    int r = (int)SafeCall(E.mExecGeneral, U(dlg), p.ptr(), yes.ptr(), 3, st, &ok);
    std::string mode = !ok ? "" : on ? (r == 1 ? "off" : "") : (r == 1 ? "on" : r == 2 ? "home" : "");
    Log("lockstep: auto-run question (%s) answered %d%s", on ? "take back" : "hand over", r, ok ? "" : " (FAILED)");
    if (mode.empty()) return;
    std::string err;
    if (!QueueAutoRun(mode, err)) Chat(err);
}

// ---- Manual torpedo salvoes (engine level 5) ----
// The division report's Fire torpedoes opens the torpedo dialog (TdlgTorpTargets); its Fire computes a firing solution (the
// course at the end of the chosen line of its mount list, jittered with this PC's own dice; the HIGH or LOW speed and range
// of the ship's torpedoes) and calls the game's fire routine TfrmMain.FireMount(mount, ship, target, angle, speed, range,
// Manual = 1). Both buttons work between minutes only (B_TORPS). In a live battle FireMount's hook doesn't run the routine
// on the PC where Fire is pressed: it records a deferred `torp <ship> <mount index in Tship.TorpedomountList> <target>
// <the angle's Single bits> <speed> <range> <manual>` for this boundary only. At the release both PCs check that the ship
// (afloat, the giving player's), the mount (torpedoes left) and the target (afloat) are still there and call FireMount
// with exactly these arguments inside the release's shared section (its 10 Random sites and SetReloadTime's 7 draw the
// shared dice), then set the ship's FiredMountThisTurn as the dialog does after the routine. The routine's early returns
// change nothing (no torpedoes left; submerged tubes above 25 knots unless the ship is an MTB, with Manual after its own
// message box): there the hook lets the routine run, so the player sees the game's own message. The AI's fire (inside the
// simulation), the release's own call and every call outside a live battle pass through.

static std::string g_torpLast;  // the outcome of this PC's last Fire (`ls torp`)

static bool IsFightingShip(void* p) { return p && dl::IsInstanceOf(p, "TFightingShip"); }
static bool IsTorpMount(void* p) { return p && dl::IsInstanceOf(p, "TFightingTorpedoMount"); }
static void* MountList(void* ship) { return ship && E.fMountList > 0 ? dl::At<void*>(ship, E.fMountList) : nullptr; }

static float AngleOf(uint32_t bits) {
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

// FireMount returns without a change, in its own order: no torpedoes left in the mount; submerged tubes (IsSubmerged) on
// a ship faster than the limit that is not an MTB (with Manual after its message box).
static bool TorpEarlyReturn(void* mount, void* ship) {
    if (dl::At<int>(mount, E.fTorpsLeft) == 0) return true;
    bool ok = false;
    bool sub = (SafeCall(E.isSubmerged, U(mount), 0, 0, 0, nullptr, &ok) & 0xFF) != 0;
    return ok && sub && dl::At<int>(ship, E.fCurSpeed) > E.subSpeed &&
           dl::At<uint8_t>(ship, E.fShipType) != (uint8_t)E.subMtb;
}

// FireMount called outside the simulation in a live battle (the torpedo dialog's Fire): 1 = handled here (recorded, or
// refused with a chat line), 0 = the routine runs (one of its early returns).
static int TorpOrder(void* mount, void* ship, void* target, uint32_t angle, int speed, int range, int manual) {
    std::string err;
    if (!g_featTorp) {
        err = "Manual torpedo salvoes are not available in this live battle.";
    } else if (!IsFightingShip(ship) || !IsFightingShip(target) || !IsTorpMount(mount)) {
        err = "No ship, mount or target selected.";
    } else if (TorpEarlyReturn(mount, ship)) {
        g_torpLast = "the game's own refusal (no torpedoes left, or submerged tubes above " + std::to_string(E.subSpeed) +
                     " knots)";
        Log("lockstep: torpedo salvo of %s: %s", W2U(ObjName(ship)).c_str(), g_torpLast.c_str());
        return 0;
    } else if (CanOrderNow(err) && dl::At<int>(ship, E.fShipSide) != S.mySide) {
        err = "That ship is not yours.";
    } else if (err.empty() && EffectiveAuto(S.mySide)) {
        err = kAutoRunFire;
    }
    if (err.empty()) {
        World w;
        Walk(w);
        auto sk = w.keyOf.find(ship), tk = w.keyOf.find(target);
        int mi = IndexIn(MountList(ship), mount);
        if (sk == w.keyOf.end() || tk == w.keyOf.end() || mi < 0) {
            err = "That ship, mount or target is not in the battle.";
        } else {
            std::string ms = std::to_string(mi);
            for (auto& d : PendingDeferred())
                if (d.kind == "torp" && d.args.size() >= 2 && d.args[0] == sk->second && d.args[1] == ms)
                    err = "A torpedo salvo from that mount is ordered already; it fires when the next minute starts.";
            char ab[16];
            snprintf(ab, sizeof(ab), "%08x", angle);
            if (err.empty() &&
                QueueDeferred("torp",
                              {sk->second, ms, tk->second, ab, std::to_string(speed), std::to_string(range),
                               manual ? "1" : "0"},
                              err, "Torpedo salvo ordered; it fires when the next minute starts.")) {
                g_torpLast = "queued #" + std::to_string(S.mySeq);
                Log("lockstep: torpedo salvo ordered (#%d): %s mount %d at %s, course %.1f, speed %d, range %d",
                    S.mySeq, W2U(ObjName(ship)).c_str(), mi, W2U(ObjName(target)).c_str(), AngleOf(angle), speed, range);
                return 1;
            }
        }
    }
    g_torpLast = "refused: " + err;
    Log("lockstep: torpedo salvo refused: %s", err.c_str());
    Chat(err);
    return 1;
}

extern "C" int __stdcall LS_FireMount(void* self, void* mount, void* ship, void* target, uint32_t angle, int speed,
                                      int range, int manual) {
    (void)self;
    // inside the simulation (the AI's fire, a release), during a load or outside a live battle: the game's routine
    if (!S.on || S.st == St::Ended || g_sim > 0 || g_loading) return 0;
    int r = 1;
    __try {
        r = TorpOrder(mount, ship, target, angle, speed, range, manual & 0xFF);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception %08lx while ordering a torpedo salvo (not ordered)", GetExceptionCode());
        r = 1;
    }
    return r;
}

// FireMount at the release (both PCs, inside its shared section); only the giving player sees the routine's messages.
static bool CallFireMount(void* m, void* mount, void* ship, void* target, uint32_t angle, int speed, int range, int manual,
                          int actor) {
    uint32_t st[5] = {U(target), angle, (uint32_t)speed, (uint32_t)range, (uint32_t)manual};
    void* fn = g_lsOrigFireMount ? g_lsOrigFireMount : E.fireMount;
    int prev = g_dfActor;
    bool ok = false;
    __try {
        g_dfActor = actor;
        SafeCall(fn, U(m), U(mount), U(ship), 5, st, &ok);
    } __finally {
        g_dfActor = prev;
    }
    return ok;
}

// The torpedo dialog's firing solutions are for the boundary they were computed at: when a minute starts the dialog is
// hidden (as before a reload), so no solution is fired a minute late.
static void CloseTorpDialog() {
    void* d = DialogForm("TdlgTorpTargets");
    if (!d || !E.mHide || !game::FormVisible(d)) return;
    bool ok = false;
    SafeCall(E.mHide, U(d), 0, 0, 0, nullptr, &ok);
    Log("lockstep: the torpedo dialog hidden as the minute from %d starts (%s)", S.boundary, ok ? "ok" : "FAILED");
    Chat("The torpedo dialog closed as the minute started: its firing solutions hold only between minutes.");
}

// ---- Missiles (engine level 9) ----
// Manual salvoes. The missile panel's Manual fire (sbFireSalvoClick) opens the modal missile dialog (TdlgMissileFire)
// between minutes only (B_SALVO). Its Fire one / half / all (FireMissiles(1|2|4), B_SALVOFIRE) go through each checked
// shooter; per launcher a nested procedure counts the "too close" ships, writes the ship's PendingMissileTarget when the
// launcher can't bear (an order of the guard: both PCs set it at the release, and the simulation's FireAllGuns fires
// it), counts the ships with friendly ships in the line of fire, and calls TfrmMain.FireMissileMount(mount, ship, target
// ship | nil, @location, bearing, N). In a live battle FireMissileMount's hook doesn't run the routine on the PC where
// Fire is pressed: it records a deferred `msl` for this boundary only and returns the count the routine would fire (its
// own preconditions, read from its code: electrics, the launcher not destroyed, missiles left (less this player's
// earlier orders on that launcher), it bears or the ship fires at any bearing, beyond the minimum and within the
// maximum range; then min(Tubes, N)), so the dialog's "one" and "half" stop rules and its summary behave as in single
// player. At the release both PCs check the same conditions again and call FireMissileMount with the recorded
// arguments inside the shared section (SetReloadTime's draws are shared dice). Missile submarines can't fire salvoes in
// a live battle: the game's branch fires them for side 0 with nation 0 and sets the submarine's Downtime on the giving
// PC alone, so the fire buttons uncheck them first (UncheckMissileSubs), and FireSSM from the screen is refused.

static std::string g_mslLast;  // the outcome of this PC's last salvo order (`ls missiles`)

// A missile launcher: TTorpedomount.MountType 1..6 (mtHeavySAM, mtMediumSAM, mtLightSam, mtHeavySSM, mtMediumSSM,
// mtLightSSM; 0 = torpedoes).
static bool IsMissileMount(void* mt) {
    if (!IsTorpMount(mt) || E.fMountType < 0) return false;
    uint8_t t = dl::At<uint8_t>(mt, E.fMountType);
    return t >= 1 && t <= 6;
}

static bool MissileShipsInBattle() {
    void* ships = List(E.allShipsVar);
    for (int i = 0; i < dl::ListCount(ships); i++) {
        void* ml = MountList(dl::ListItem(ships, i));
        for (int k = 0; k < dl::ListCount(ml); k++)
            if (IsMissileMount(dl::ListItem(ml, k))) return true;
    }
    return false;
}

// Missile orders (the dialog's fire buttons, FireMissileMount from the screen): the battle at a processed boundary with
// no minute running, and on B no release waiting (A releases nothing while its own dialog is open; B's order must
// reach A before A's next release, so B's clock must be paused too).
static bool MissileOrdersOpen(std::string& err) {
    if (!CanOrderNow(err)) return false;
    if (S.minuteRunning || !S.atBoundary || Minute() != S.boundary || S.hasPendingRel || (!S.isA && S.runLeft > 0)) {
        err = "Pause the battle first: missiles are fired between minutes.";
        return false;
    }
    if (EffectiveAuto(S.mySide)) {
        err = kAutoRunFire;
        return false;
    }
    return true;
}

// TorpsLeft less the predicted counts of this PC's pending `msl` orders on the same launcher (the replays run in order).
static int MslLeftAfterPending(const std::string& shipKey, int mi, int left) {
    std::string ms = std::to_string(mi);
    for (auto& d : PendingDeferred())
        if (d.kind == "msl" && d.args.size() >= 7 && d.args[0] == shipKey && d.args[1] == ms)
            left = std::max(left - atoi(d.args[6].c_str()), 0);
    return left;
}

// FireMissileMount's preconditions with `left` missiles in the launcher: the number it fires, or 0 and why not. Pure
// game reads (MountCanFire, GetDistanceToPoint, GetMissileMinimumRange and the maximum range draw no Random).
static int MslPredict(void* m, void* mount, void* ship, const uint8_t* pt, int bearing, int n, int left, std::string& why) {
    if (dl::At<uint8_t>(ship, E.fElectric)) return why = "its electrics are out", 0;
    if (dl::At<uint8_t>(mount, E.fMountDestroyed)) return why = "the launcher is destroyed", 0;
    if (left <= 0) return why = "no missiles are left in that launcher", 0;
    bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
    bool bears = (SafeCall(E.mountCanFire, U(mount), (uint32_t)bearing, 0, 0, nullptr, &ok1) & 0xFF) != 0;
    if (!bears && dl::At<uint8_t>(ship, E.fShipType) != (uint8_t)E.mslAnyBearing) return why = "the launcher can't bear", 0;
    uint8_t loc[12];
    memcpy(loc, pt, 12);
    int d = (int)SafeCall(E.distToPoint, U(ship), U(loc), 0, 0, nullptr, &ok2);
    uint8_t type = dl::At<uint8_t>(mount, E.fMountType);
    int mn = (int)SafeCall(E.minRange, U(m), type, 0, 0, nullptr, &ok3);
    int mx = (int)SafeCall(E.maxRange, (uint32_t)dl::At<int>(ship, E.fShipNation), type, 0, 0, nullptr, &ok4);
    if (!ok1 || !ok2 || !ok3 || !ok4) return why = "the game's range check failed", 0;
    if (d <= mn) return why = "the target is inside the minimum range", 0;
    if (d >= mx) return why = "the target is out of range", 0;
    int c = std::min(dl::At<int>(mount, E.fTubes), n);
    if (c <= 0) return why = "nothing to fire", 0;
    return c;
}

// This PC's orders of one dialog action, said once in the chat (posted: the dialog's own summary box comes first).
static struct MslNote {
    bool posted = false;
    int orders = 0, missiles = 0;
    std::set<std::string> ships;
    std::string refused;
} g_mslNote;

static void MslNotePost() {
    if (g_mslNote.posted) return;
    g_mslNote.posted = true;
    RunOnMainThread([] {
        MslNote n = g_mslNote;
        g_mslNote = MslNote();
        if (n.orders > 0) {
            std::string ships;
            for (auto& s : n.ships) ships += (ships.empty() ? "" : ", ") + s;
            Chat("Missile salvo ordered: " + std::to_string(n.missiles) + (n.missiles == 1 ? " missile" : " missiles") +
                 " from " + ships + "; they fire when the next minute starts.");
        }
        if (!n.refused.empty()) Chat(n.refused);
    });
}

// FireMissileMount called outside the simulation in a live battle (the missile dialog): records the salvo, or refuses
// it; returns the count the dialog gets (0: nothing fires, as the routine would answer).
static int MslOrder(void* m, void* mount, void* ship, void* target, void* loc, int bearing, int n) {
    std::string err;
    if (!g_featMissile)
        err = "Manual missile fire is not available in this live battle.";
    else if (!IsFightingShip(ship) || !IsTorpMount(mount) || !loc || (target && !IsFightingShip(target)))
        err = "No ship, launcher or target.";
    else if (MissileOrdersOpen(err) && dl::At<int>(ship, E.fShipSide) != S.mySide)
        err = "That ship is not yours.";
    if (err.empty()) {
        uint8_t pt[12];
        memcpy(pt, loc, 12);
        World w;
        Walk(w);
        auto sk = w.keyOf.find(ship);
        auto tk = target ? w.keyOf.find(target) : w.keyOf.end();
        int mi = IndexIn(MountList(ship), mount);
        if (sk == w.keyOf.end() || (target && tk == w.keyOf.end()) || mi < 0) {
            err = "That ship, launcher or target is not in the battle.";
        } else {
            std::string why;
            int left = MslLeftAfterPending(sk->second, mi, dl::At<int>(mount, E.fTorpsLeft));
            int count = MslPredict(m, mount, ship, pt, bearing, n, left, why);
            if (count <= 0) {  // the routine would fire nothing and change nothing: no order
                g_mslLast = "nothing to fire (" + why + ")";
                Log("lockstep: missile salvo of %s mount %d: %s", W2U(ObjName(ship)).c_str(), mi, g_mslLast.c_str());
                return 0;
            }
            if (QueueDeferred("msl",
                              {sk->second, std::to_string(mi), target ? tk->second : std::string("-"), Hex(pt, 12),
                               std::to_string(bearing), std::to_string(n), std::to_string(count)},
                              err, "", false)) {
                S.mslOrders++;
                g_mslLast = "queued #" + std::to_string(S.mySeq) + " (" + std::to_string(count) + ")";
                Log("lockstep: missile salvo ordered (#%d): %s mount %d (type %d) at %s, bearing %d, %d of %d to fire",
                    S.mySeq, W2U(ObjName(ship)).c_str(), mi, (int)dl::At<uint8_t>(mount, E.fMountType),
                    target ? W2U(ObjName(target)).c_str() : "a location", bearing, count, n);
                g_mslNote.orders++;
                g_mslNote.missiles += count;
                g_mslNote.ships.insert(W2U(ObjName(ship)));
                MslNotePost();
                return count;
            }
        }
    }
    g_mslLast = "refused: " + err;
    Log("lockstep: missile salvo refused: %s", err.c_str());
    if (g_mslNote.refused.empty()) g_mslNote.refused = err;
    MslNotePost();
    return 0;
}

extern "C" int __stdcall LS_FireMissileMount(void* self, void* mount, void* ship, void* target, void* loc, int bearing,
                                             int n) {
    // inside the simulation (the computer's fire, the pending heavy SSM, a release), during a load or outside a live
    // battle: the game's routine
    if (!S.on || S.st == St::Ended || g_sim > 0 || g_loading) return 0;
    g_lsMslCount = 0;
    __try {
        g_lsMslCount = MslOrder(self, mount, ship, target, loc, bearing, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception %08lx while ordering a missile salvo (not ordered)", GetExceptionCode());
        g_lsMslCount = 0;
    }
    return 1;
}

// FireMissileMount at the release (both PCs, inside its shared section); only the giving player sees the routine's
// messages. Returns the number fired.
static int CallFireMissileMount(void* m, void* mount, void* ship, void* target, const uint8_t* pt, int bearing, int n,
                                int actor, bool* ok) {
    uint8_t loc[12];
    memcpy(loc, pt, 12);
    uint32_t st[4] = {U(target), U(loc), (uint32_t)bearing, (uint32_t)n};
    void* fn = g_lsOrigFireMissileMount ? g_lsOrigFireMissileMount : E.fireMissileMount;
    int prev = g_dfActor;
    int r = 0;
    __try {
        g_dfActor = actor;
        r = (int)SafeCall(fn, U(m), U(mount), U(ship), 4, st, ok);
    } __finally {
        g_dfActor = prev;
    }
    return r;
}

// The giving player's chat after a release: the salvoes fired, per ship, and the ones not carried out (once each).
static struct MslReleased {
    bool posted = false;
    std::map<std::string, int> fired;
    std::vector<std::string> notes;
} g_mslReleased;

static void MslReleasedNote(const std::string& ship, int fired, const std::string& note) {
    if (!note.empty()) {
        if (std::find(g_mslReleased.notes.begin(), g_mslReleased.notes.end(), note) == g_mslReleased.notes.end())
            g_mslReleased.notes.push_back(note);
    } else {
        g_mslReleased.fired[ship] += fired;
    }
    if (g_mslReleased.posted) return;
    g_mslReleased.posted = true;
    AfterRelease([] {
        MslReleased r = g_mslReleased;
        g_mslReleased = MslReleased();
        if (!r.fired.empty()) {
            std::string list;
            int total = 0;
            for (auto& f : r.fired) {
                list += (list.empty() ? "" : ", ") + f.first + " " + std::to_string(f.second);
                total += f.second;
            }
            Chat("Missile salvo fired: " + list + " (" + std::to_string(total) + (total == 1 ? " missile)." : " missiles)."));
        }
        for (auto& s : r.notes) Chat(s);
    });
}

// The missile dialog's shooter list (TListItems) and its item helpers, as FireMissiles uses them.
static void* MslItems(void* dlg) {
    void* lv = dlg && E.fMslShooters > 0 ? dl::At<void*>(dlg, E.fMslShooters) : nullptr;
    return lv && E.fLvItems > 0 ? dl::At<void*>(lv, E.fLvItems) : nullptr;
}
static int MslItemCount(void* items) {
    bool ok = false;
    int n = items && E.lvCount ? (int)SafeCall(E.lvCount, U(items), 0, 0, 0, nullptr, &ok) : 0;
    return ok ? n : 0;
}
static void* MslItem(void* items, int i) {
    bool ok = false;
    void* it = items && E.lvGetItem ? (void*)(uintptr_t)SafeCall(E.lvGetItem, U(items), (uint32_t)i, 0, 0, nullptr, &ok)
                                    : nullptr;
    return ok ? it : nullptr;
}
static bool MslItemChecked(void* it) {
    bool ok = false;
    return it && E.lvItemChecked && (SafeCall(E.lvItemChecked, U(it), 0, 0, 0, nullptr, &ok) & 0xFF) != 0 && ok;
}
static bool MslItemCheck(void* it, bool on) {
    bool ok = false;
    if (it && E.lvItemSetChecked) SafeCall(E.lvItemSetChecked, U(it), on ? 1 : 0, 0, 0, nullptr, &ok);
    return ok;
}
static void* MslItemData(void* it) { return it && E.fItemData > 0 ? dl::At<void*>(it, E.fItemData) : nullptr; }

// Before the dialog's Fire: its checked missile submarines are unchecked (their salvo would be fired for side 0 with
// nation 0, and their Downtime set on this PC alone). Returns how many.
static int UncheckMissileSubs(void* dlg) {
    void* items = MslItems(dlg);
    int n = MslItemCount(items), un = 0;
    for (int i = 0; i < n && i < 1000; i++) {
        void* it = MslItem(items, i);
        void* data = MslItemData(it);
        if (data && dl::IsInstanceOf(data, "TSubmarine") && MslItemChecked(it) && MslItemCheck(it, false)) un++;
    }
    return un;
}

// FireSSM from the screen in a live battle: the missile dialog's submarine branch (side 0 and nation 0 hard-coded) or a
// handler the battle blocks. A submarine's location tells which (for the chat).
static bool IsSubLocation(void* loc) {
    __try {
        void* subs = List(E.subsVar);
        for (int i = 0, n = dl::ListCount(subs); i < n && E.fSubLocation > 0; i++)
            if (void* sb = dl::ListItem(subs, i))
                if ((uint8_t*)sb + E.fSubLocation == (uint8_t*)loc) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

static bool g_ssmNoted = false;

static void SsmRefused(int side, bool sub) {
    S.ssmRefused++;
    Log("lockstep: FireSSM from the screen refused (side %d, %s)", side, sub ? "a missile submarine" : "not a submarine");
    if (g_ssmNoted) return;
    g_ssmNoted = true;
    RunOnMainThread([sub] {
        g_ssmNoted = false;
        Chat(sub ? "Missile submarines can't fire salvoes in a live battle (the game fires them for the first side only)."
                 : "That missile launch is not available in a live battle.");
    });
}

extern "C" int __stdcall LS_FireSSM(void* self, int side, void* loc) {
    (void)self;
    // inside the simulation (the computer's fire, FireMissileMount at a release) or outside a live battle: the routine
    if (!S.on || S.st == St::Ended || g_sim > 0 || g_loading) return 0;
    SsmRefused(side, IsSubLocation(loc));
    return 1;
}

// Carries out one deferred action of `actor`'s player (both PCs, inside the release's shared section; relN = the
// boundary the release starts from). The same state and order on both PCs give the same result.
static void RunDeferred(int actor, const std::string& item, int relN) {
    std::vector<std::string> p = Split(item.substr(1), '\x1f');
    if (p.size() < 2) return;
    std::string kind = p[0];
    int minute = atoi(p[1].c_str());
    std::vector<std::string> a(p.begin() + 2, p.end());
    bool mine = actor == S.mySide;
    std::string what = DfText(kind, a);
    auto skip = [&](const std::string& why) {
        Log("lockstep: deferred %s of side %d (given for minute %d) not carried out at minute %d: %s", what.c_str(), actor,
            minute, relN, why.c_str());
        if (kind == "msl") {  // (a salvo is one order per launcher: the giver's chat says it once per ship and reason)
            S.mslSkipped++;
            if (mine) MslReleasedNote("", 0, "Not carried out: " + what + " (" + why + ").");
            return;
        }
        if (mine) AfterRelease([what, why] { Chat("Not carried out: " + what + " (" + why + ")."); });
    };
    const DfKind* k = DfFind(kind);
    if (!k || a.size() < 2) return skip("an order this PC doesn't know");
    if (k->thisBoundaryOnly && minute != relN)
        return skip("it was meant for minute " + std::to_string(minute) + " and arrived too late");
    void* m = BattleForm();
    if (!m) return skip("no battle");
    if (kind == "autorun") return RunAutoRun(actor, a, relN, skip);
    World w;
    Walk(w);
    auto obj = [&](const std::string& key) -> void* {
        auto it = w.objOf.find(key);
        return it == w.objOf.end() ? nullptr : it->second;
    };
    if (kind == "torp") {  // torp <ship> <mount index> <target> <angle bits> <speed> <range> <manual>
        if (!g_featTorp) return skip("manual torpedo salvoes are not available on this PC");
        if (a.size() < 7) return skip("an order this PC doesn't know");
        void *ship = obj(a[0]), *target = obj(a[2]);
        if (!IsFightingShip(ship) || !IsFightingShip(target)) return skip("the ship or its target is gone");
        if (dl::At<int>(ship, E.fShipSide) != actor) return skip("the ship is not that player's");
        if (dl::At<uint8_t>(ship, E.fSunk)) return skip("the ship was sunk");
        if (dl::At<uint8_t>(target, E.fSunk)) return skip("the target was sunk");
        // Issue order: a salvo given before its player's auto-run order (same pause or earlier) fires here, ahead of the
        // auto-run item; from that order on the buttons refuse, and one that still arrives later is skipped.
        if (actor >= 0 && actor <= 1 && S.autoRun[actor]) return skip("the side is on auto-run");
        int mi = atoi(a[1].c_str());
        void* mount = dl::ListItem(MountList(ship), mi);
        if (!IsTorpMount(mount)) return skip("the mount is gone");
        int left = dl::At<int>(mount, E.fTorpsLeft);
        if (left <= 0) return skip("no torpedoes are left in that mount");
        uint32_t angle = (uint32_t)strtoul(a[3].c_str(), nullptr, 16);
        int speed = atoi(a[4].c_str()), range = atoi(a[5].c_str()), manual = atoi(a[6].c_str()) ? 1 : 0;
        int before = dl::ListCount(List(E.torpVar));
        bool ok = CallFireMount(m, mount, ship, target, angle, speed, range, manual, actor);
        dl::At<uint8_t>(ship, E.fFiredMount) = 1;  // as the torpedo dialog's Fire does after the routine
        int fired = dl::ListCount(List(E.torpVar)) - before;
        std::string sn = W2U(ObjName(ship));
        Log("lockstep: deferred torpedo salvo of side %d %s at minute %d (given for minute %d): %s mount %d at %s, course "
            "%.1f, speed %d, range %d, manual %d: %d torpedoes in the water (%d -> %d left, reload %d)",
            actor, ok ? "carried out" : "FAILED (an exception in the game's routine)", relN, minute, sn.c_str(), mi,
            W2U(ObjName(target)).c_str(), AngleOf(angle), speed, range, manual, fired, left,
            dl::At<int>(mount, E.fTorpsLeft), E.fReloadTime > 0 ? dl::At<int>(mount, E.fReloadTime) : -1);
        if (mine)
            AfterRelease([sn, fired, ok] {
                Chat(!ok         ? "Torpedo salvo of " + sn + ": the game's fire routine failed."
                     : fired > 0 ? "Torpedo salvo fired: " + sn + " launched " + std::to_string(fired) +
                                       (fired == 1 ? " torpedo." : " torpedoes.")
                                 : "Torpedo salvo of " + sn + ": no torpedo left the tubes.");
            });
        return;
    }
    if (kind == "msl") {  // msl <ship> <mount index> <target|-> <location hex> <bearing> <number> <predicted>
        std::string sn = KeyName(a[0]);
        if (!g_featMissile) return skip("manual missile fire is not available on this PC");
        if (a.size() < 6) return skip("an order this PC doesn't know");
        void* ship = obj(a[0]);
        void* target = a[2] == "-" ? nullptr : obj(a[2]);
        if (!IsFightingShip(ship)) return skip("the ship is gone");
        if (a[2] != "-" && !IsFightingShip(target)) return skip("the target is gone");
        if (dl::At<int>(ship, E.fShipSide) != actor) return skip("the ship is not that player's");
        if (dl::At<uint8_t>(ship, E.fSunk)) return skip("the ship was sunk");
        if (target && dl::At<uint8_t>(target, E.fSunk)) return skip("the target was sunk");
        if (actor >= 0 && actor <= 1 && S.autoRun[actor]) return skip("the side is on auto-run");  // (issue order, as torp)
        int mi = atoi(a[1].c_str());
        void* mount = dl::ListItem(MountList(ship), mi);
        if (!IsTorpMount(mount)) return skip("the launcher is gone");
        uint8_t pt[12];
        if (!Unhex(a[3], pt, 12)) return skip("an order this PC doesn't know");
        int bearing = atoi(a[4].c_str()), n = atoi(a[5].c_str());
        int left = dl::At<int>(mount, E.fTorpsLeft);
        std::string why;
        int can = MslPredict(m, mount, ship, pt, bearing, n, left, why);
        if (can <= 0) return skip(why);
        int before = dl::ListCount(List(E.torpVar));
        bool ok = false;
        int fired = CallFireMissileMount(m, mount, ship, target, pt, bearing, n, actor, &ok);
        int added = dl::ListCount(List(E.torpVar)) - before;
        S.mslReplays++;
        S.mslFired += added;
        Log("lockstep: deferred missile salvo of side %d %s at minute %d (given for minute %d): %s mount %d (type %d) at "
            "%s, bearing %d, %d of %d: %d fired, %d missiles in the air (%d -> %d left, reload %d)",
            actor, ok ? "carried out" : "FAILED (an exception in the game's routine)", relN, minute,
            W2U(ObjName(ship)).c_str(), mi, (int)dl::At<uint8_t>(mount, E.fMountType),
            target ? W2U(ObjName(target)).c_str() : "a location", bearing, can, n, fired, added, left,
            dl::At<int>(mount, E.fTorpsLeft), E.fReloadTime > 0 ? dl::At<int>(mount, E.fReloadTime) : -1);
        if (mine) {
            if (!ok)
                MslReleasedNote(sn, 0, "Missile salvo of " + sn + ": the game's fire routine failed.");
            else if (added <= 0)
                MslReleasedNote(sn, 0, "Missile salvo of " + sn + ": no missile left the launcher.");
            else
                MslReleasedNote(sn, added, "");
        }
        return;
    }
    if (kind == "chforce") {
        void *dv = obj(a[0]), *fc = obj(a[1]);
        if (!dv || !fc) return skip("the division or the force is gone");
        void* old = dl::At<void*>(dv, E.fMyForce);
        if (!old) return skip("the division is not in a force any more");
        if (old == fc) return skip("the division is in that force already");
        if (dl::At<int>(fc, E.fForceSide) != dl::At<int>(dv, E.fDivSide)) return skip("that force is on the other side");
        if (!HasShips(fc)) return skip("that force has no ships left");
        void *oldList = dl::At<void*>(old, E.fFDivs), *newList = dl::At<void*>(fc, E.fFDivs);
        bool ok = false;
        int idx = (int)SafeCall(E.listIndexOf, U(oldList), U(dv), 0, 0, nullptr, &ok);
        if (!ok || idx < 0) return skip("the division is not in its force's list");
        // as Changeforce2Click: old.Divisions.Delete(old.Divisions.IndexOf(div)); new.Divisions.Add(div);
        // div.myForce := new; dec div.Admin
        SafeCall(E.listDelete, U(oldList), (uint32_t)idx, 0, 0, nullptr, &ok);
        if (ok) SafeCall(E.listAdd, U(newList), U(dv), 0, 0, nullptr, &ok);
        if (!ok) {
            Log("lockstep: deferred %s of side %d FAILED in the force lists", what.c_str(), actor);
            return;
        }
        dl::At<void*>(dv, E.fMyForce) = fc;
        dl::At<int>(dv, E.fAdmin) -= 1;
        Log("lockstep: deferred %s of side %d carried out at minute %d (given for minute %d)", what.c_str(), actor, relN,
            minute);
        if (dl::At<int>(dv, E.fDivSide) == g_viewSide) AfterRelease(RefillShipTree);  // the tree of the PC whose side it is
        return;
    }
    bool turn = kind == "turnaway";
    void *fc = obj(a[0]), *dv = obj(a[1]);
    if (!fc || !dv) return skip("the force or the division is gone");
    if (IndexIn(dl::At<void*>(fc, E.fFDivs), dv) < 0) return skip("the division is not in that force any more");
    if (dl::At<uint8_t>(fc, E.fFAI)) return skip("the force is under computer control");
    if (!turn && dl::At<uint8_t>(fc, E.fHasChangedLead)) return skip("the lead of this force was changed once already");
    if (turn && dl::At<int>(fc, E.fTimeToTurnAway) > 0) return skip("the force turned away less than 15 minutes ago");
    if (ShipCount(dv) == 0) return skip("the division has no ships left");
    // The routines start from the force's lead division (GetDivisionOfShip(GetForceFlagship)) and fail without one.
    bool ok = false;
    void* flag = (void*)(uintptr_t)SafeCall(E.getForceFlagship, U(fc), 0, 0, 0, nullptr, &ok);
    void* oldLead = ok && flag ? DivisionOfShip(flag) : nullptr;
    if (!oldLead) return skip("the force has no lead division");
    if (oldLead == dv) return skip("that division leads the force already");
    if (turn) {  // DoBattleTurnAway reverses and turns every division ahead of the new lead (their LeadDivision chain)
        int n = dl::ListCount(List(E.divisionsVar)), steps = 0;
        for (void* d = dl::At<void*>(dv, E.fLeadDiv); d; d = dl::At<void*>(d, E.fLeadDiv)) {
            if (++steps > n) return skip("the divisions' lead chain loops");
            if (!IsDivision(d) || ShipCount(d) == 0) return skip("a division ahead of it has no ships left");
        }
    }
    ResolveBattleDialogs();
    ok = CallForceRoutine(turn ? E.doTurnAway : E.setForceLead, m, fc, dv, actor, DialogForm("TdlgForceOrders"),
                          DialogForm("TdlgSelectLead"));
    Log("lockstep: deferred %s of side %d %s at minute %d (given for minute %d)", what.c_str(), actor,
        ok ? "carried out" : "FAILED (an exception in the game's routine)", relN, minute);
}

// The formations launched from the screen since the last minute's end are the tail of the air list on both PCs, each
// PC's own launches first (they ran at once) and the other PC's after (replayed at the release). At the end of the
// release (its shared section, both PCs) that tail is put into one order, by AFId: side 0's launches first, each side's
// in the order they were given. So both players launching at the same boundary need no resync.
static int g_airMark = -1;  // the air list's count at the end of the last minute, load or release (equal on both PCs)

static void SortLaunched() {
    void* air = List(E.airVar);
    int n = dl::ListCount(air);
    if (g_airMark >= 0 && g_airMark <= n && n - g_airMark >= 2 && E.fAfId >= 0) {
        void** items = dl::At<void**>(air, 4);  // TList.FList
        std::vector<void*> tail(items + g_airMark, items + n), sorted;
        bool ui = true;
        for (void* af : tail) ui = ui && af && IsUiAfId(dl::At<int>(af, E.fAfId));
        if (!ui) {
            Log("lockstep: the formations made since minute %d are not all launches from the screen: not sorted", S.boundary);
        } else {
            sorted = tail;
            std::stable_sort(sorted.begin(), sorted.end(),
                             [](void* x, void* y) { return dl::At<int>(x, E.fAfId) < dl::At<int>(y, E.fAfId); });
            if (sorted != tail) {
                std::copy(sorted.begin(), sorted.end(), items + g_airMark);
                S.launchSorts++;
                Log("lockstep: %d formations launched at this boundary put into one order (AF%d first)", n - g_airMark,
                    dl::At<int>(sorted[0], E.fAfId));
            }
        }
    }
    g_airMark = n;
}

// Applies the released batches inside the release's shared section, in an order that is the same on both PCs:
//   1. the other PC's immediate actions (this PC carried out its own when they were given);
//   2. side 0's batches in sequence order, each item in the order its player gave it: values (V), deferred actions (D);
//   3. side 1's batches likewise;
//   4. the formations launched at this boundary into one order (SortLaunched).
// relN = the boundary the release starts from. Returns the number of sides with immediate actions that change the order
// of battle (detach; both: a resync); launches from the strike dialog don't count (side-partitioned AFIds, SortLaunched).
static int ApplyBatches(int seq0, int seq1, int relN) {
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
                    if (it.compare(1, 7, "launch\x1f") != 0) any = true;
                    if (side != mySide) ReplayAction(Split(it.substr(1), '\x1f'));
                }
        if (any) withActions++;
    }
    World w;
    Walk(w);
    int nv = 0, bad = 0, nd = 0;
    for (int side = 0; side < 2; side++)
        for (auto& b : bySide[side])
            for (auto& it : b.items) {
                if (it.empty()) continue;
                if (it[0] == 'D') {
                    RunDeferred(side, it, relN);
                    nd++;
                    Walk(w);  // (today's kinds keep every key; a later kind may not)
                    continue;
                }
                if (it[0] != 'V') continue;
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
    SortLaunched();
    // released own orders leave the overlay (unless a newer order of the same field is still pending)
    for (auto it = g_overlay.begin(); it != g_overlay.end();)
        it = it->second.seq <= mySeqTo ? g_overlay.erase(it) : std::next(it);
    S.myBatches.erase(std::remove_if(S.myBatches.begin(), S.myBatches.end(),
                                     [&](const Batch& b) { return b.seq <= mySeqTo; }),
                      S.myBatches.end());
    for (auto it = S.peerBatches.begin(); it != S.peerBatches.end();)
        it = it->first <= peerSeqTo ? S.peerBatches.erase(it) : std::next(it);
    if (nv || bad || withActions || nd)
        Log("lockstep: released orders side0<=#%d side1<=#%d: %d fields%s, %d deferred, %d with actions", seq0, seq1, nv,
            bad ? (" (" + std::to_string(bad) + " not found)").c_str() : "", nd, withActions);
    return withActions;
}

// Pending orders are dropped (a new snapshot, the end of the live battle); dropWhy: tell the player which deferred
// actions were not carried out.
static void ClearOrders(const char* dropWhy = nullptr) {
    if (dropWhy) ReportDroppedDeferred(dropWhy);
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

// Every live load goes through here (LoadSnapshot on both PCs: the start, a resync, a rejoin; A's RecoverAlone). After
// the load the battle runs with the campaign's own date, not the battle save's day and month (engine level 10).
static bool LoadCampaignGuarded(int slot) {
    __try {
        if (!game::LoadCampaign(slot, 0)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception %08lx while loading slot %d", GetExceptionCode(), slot);
        return false;
    }
    KeepCampaignDate("the load");
    return true;
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

static void WriteMarker(int slot);
static void SwapCampaignDate();

// The battle log's two lists: TfrmMain.TotalLogLines (each message) and TotalTimeStamps (its "dd hh:nn ") grow together
// in the EOTP message routine (0xd8fd5c), which compares their counts after every batch and, when they differ, shows
// "Log lines out of sync! n/m" and trims the longer one. The battle save writes them as TLog.log and TTime.log
// (TStrings.SaveToFile, one line per entry) and a load reads them back (LoadFromFile), where a line break inside an
// entry splits it: every message with one (a battery's "... is out of sight of the\nflagship ...") came back as two
// entries, and the first minute after every live load (start, resync, rejoin) showed the box on both PCs. The mod's
// snapshot writes such entries as one line (JoinLogLineBreaks), and every live load aligns the two lists in case the
// save is older (AlignLogLists: the time stamps get the last one again or lose their extra ones; display data only).
static void* LogStrings(void* m, const char* field) {
    int f = dl::Field("TfrmMain", field);
    void* l = m && f > 0 ? dl::At<void*>(m, f) : nullptr;
    return l && dl::IsInstanceOf(l, "TStringList") ? l : nullptr;
}

static int StringsCount(void* sl) {
    static int fCount = dl::Field("TStringList", "FCount");
    return sl && fCount > 0 ? dl::At<int>(sl, fCount) : 0;
}

static uint32_t* StringsSlot(void* sl, int i) {  // &FList[i].FString
    static int fList = dl::Field("TStringList", "FList");
    void** items = sl && fList > 0 ? dl::At<void**>(sl, fList) : nullptr;
    return items && i >= 0 && i < StringsCount(sl) ? (uint32_t*)&items[i * 2] : nullptr;
}

static int JoinLogLineBreaks(void* m) {
    void* lines = LogStrings(m, "TotalLogLines");
    if (!lines || !g_ustrAsg) return 0;
    int joined = 0;
    for (int i = 0, n = StringsCount(lines); i < n; i++) {
        uint32_t* slot = StringsSlot(lines, i);
        if (!slot || !*slot) continue;
        std::wstring s = dl::ReadUStr((void*)(uintptr_t)*slot);
        if (s.find_first_of(L"\r\n") == std::wstring::npos) continue;
        std::wstring t;
        for (size_t k = 0; k < s.size(); k++) {
            if (s[k] != L'\r' && s[k] != L'\n') {
                t += s[k];
            } else if (t.empty() || t.back() != L' ') {
                t += L' ';
            }
        }
        dl::ConstUStr u(t);
        StrAsg(slot, u.ptr());  // (_UStrAsg copies a literal)
        joined++;
    }
    return joined;
}

static void AlignLogLists(void* m, const char* when) {
    static void* mAdd = dl::Method("TStringList", "Add");
    static void* mDelete = dl::Method("TStringList", "Delete");
    void *lines = LogStrings(m, "TotalLogLines"), *stamps = LogStrings(m, "TotalTimeStamps");
    if (!lines || !stamps || !mAdd || !mDelete) return;
    int n1 = StringsCount(lines), n2 = StringsCount(stamps);
    if (n1 == n2) return;
    bool ok = true;
    if (n2 > n1) {
        for (int i = n2 - 1; i >= n1 && ok; i--) SafeCall(mDelete, U(stamps), (uint32_t)i, 0, 0, nullptr, &ok);
    } else {
        uint32_t* last = n2 > 0 ? StringsSlot(stamps, n2 - 1) : nullptr;
        std::wstring text = last && *last ? dl::ReadUStr((void*)(uintptr_t)*last) : std::wstring();
        dl::ConstUStr stamp(text);
        uint32_t sp = text.empty() ? 0u : stamp.ptr();  // (an empty Delphi string is nil)
        for (int i = n2; i < n1 && ok; i++) SafeCall(mAdd, U(stamps), sp, 0, 0, nullptr, &ok);
    }
    Log("lockstep: the battle log after %s had %d lines and %d time stamps: aligned (%s; the game would show \"Log lines "
        "out of sync! %d/%d\")",
        when, n1, n2, ok && StringsCount(stamps) == n1 ? "ok" : "FAILED", n1, n2);
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
    // A side on auto-run goes into the file under its player's command, with the canonical PlayerSide: the snapshot is
    // also the host's crash checkpoint (a load outside a live battle gives the host their ships back; the game's resume
    // gives side 1 to the computer itself), and every lockstep load hands the side to the computer again (LoadSnapshot).
    // Its forces keep the computer control they had before auto-run (S.preAutoF; unknown: as they are, which is that for
    // the game's own auto-run), its divisions are its player's.
    std::vector<std::pair<uint8_t*, uint8_t>> keep;
    std::vector<uint8_t> want;
    for (int side = 0; side < 2 && S.on; side++) {
        if (!S.autoRun[side]) continue;
        const std::string& pre = S.preAutoF[side];
        std::vector<void*> fl;
        void* forces = List(E.forcesVar);
        for (int i = 0; i < dl::ListCount(forces); i++)
            if (void* f = dl::ListItem(forces, i))
                if (dl::At<int>(f, E.fForceSide) == side) fl.push_back(f);
        for (size_t i = 0; i < fl.size(); i++) {
            uint8_t v = dl::At<uint8_t>(fl[i], E.fFAI);
            keep.push_back({(uint8_t*)fl[i] + E.fFAI, v});
            want.push_back(pre.size() == fl.size() ? (uint8_t)(pre[i] == '1') : v);
        }
        void* divs = List(E.divisionsVar);
        for (int i = 0; i < dl::ListCount(divs); i++)
            if (void* d = dl::ListItem(divs, i))
                if (dl::At<int>(d, E.fDivSide) == side) {
                    keep.push_back({(uint8_t*)d + E.fDAI, dl::At<uint8_t>(d, E.fDAI)});
                    want.push_back(0);
                }
    }
    for (size_t i = 0; i < keep.size(); i++) *keep[i].first = want[i];
    int simSide = g_simPlayerSide;
    g_simPlayerSide = 0;
    int joined = JoinLogLineBreaks(m);  // (TLog.log: one line per battle log entry, see AlignLogLists)
    CanonBegin();  // the battle is saved with the simulation's state, not this PC's display set
    g_quiet++;
    SafeCall(E.saveScenario, U(m), p.ptr(), 0, 0, nullptr, &ok1);
    if (ok1) {
        SwapCampaignDate();  // the campaign's own date into [General], not the battle's day and month
        SafeCall(E.mSaveCampaign, U(bc), (uint32_t)*E.inProgVar, 0, 1, st, &ok2);
        SwapCampaignDate();
    }
    g_quiet--;
    CanonEnd();
    g_simPlayerSide = simSide;
    for (auto& k : keep) *k.first = k.second;
    Log("lockstep: battle saved into slot %d at t=%s (%s) in %lu ms%s%s", slot, ClockText().c_str(),
        ok1 && ok2 ? "ok" : "FAILED", GetTickCount() - t0, keep.empty() ? "" : " (the sides on auto-run under their players)",
        joined ? (" (" + std::to_string(joined) + " battle log entries with line breaks written as one line)").c_str() : "");
    if (!ok1 || !ok2) err = "the game could not save the battle";
    // The host's own battle: the marker that lets a restarted host resume this battle save as a live battle (phase 5b).
    if (ok1 && ok2 && S.on && S.isA && S.campaignSlot) WriteMarker(slot);
    return ok1 && ok2;
}

// The unit variable of a form the program creates at startup: code reads it with `mov eax, [cell]; mov eax, [eax]`, and
// the variable holds an instance of exactly this class.
static void** FormVarOf(const char* cls) {
    uint32_t vmt = U(dl::ClassVmt(cls));
    if (!vmt) return nullptr;
    uint8_t* p = (uint8_t*)dl::g_base + 0x1000;
    uint8_t* end = (uint8_t*)dl::g_base + dl::g_size - 8;
    for (; p < end; p++) {
        if (p[0] != 0xA1 || p[5] != 0x8B || p[6] != 0x00) continue;
        void* var = VarFromCell(p + 1);
        if (!var) continue;
        __try {
            void* obj = *(void**)var;
            if (obj && *(uint32_t*)obj == vmt) return (void**)var;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return nullptr;
}

// The battle's modeless dialogs keep pointers to its ships, divisions and air formations. Before the battle is loaded
// again (start, resync, leaving) they are hidden, so none of them is used after a load freed those objects.
static const char* kBattleDialogs[] = {"TdlgShowAirFormations", "TdlgDivisionReport", "TdlgForceOrders",
                                       "TdlgSelectLead",        "TdlgTorpTargets",    "TdlgStrike",
                                       "TdlgMissileFire"};
static const int kBattleDialogCount = sizeof(kBattleDialogs) / sizeof(kBattleDialogs[0]);
static void** g_dialogVars[kBattleDialogCount] = {};
static bool g_dialogsResolved = false;

static void ResolveBattleDialogs() {
    if (g_dialogsResolved) return;
    g_dialogsResolved = true;
    std::string missing;
    for (int i = 0; i < kBattleDialogCount; i++) {
        // the strike dialog's variable is read from sbStrikeClick's code (ResolveAirStrikes), the others found by instance
        if (!strcmp(kBattleDialogs[i], "TdlgStrike") && E.strikeDlgVar) g_dialogVars[i] = E.strikeDlgVar;
        else g_dialogVars[i] = FormVarOf(kBattleDialogs[i]);
        if (!g_dialogVars[i]) missing += std::string(" ") + kBattleDialogs[i];
    }
    Log("lockstep: battle dialogs resolved%s", missing.empty() ? "" : (", not found:" + missing).c_str());
}

// One of the battle's dialogs (kBattleDialogs) or nullptr.
static void* DialogForm(const char* cls) {
    ResolveBattleDialogs();
    for (int i = 0; i < kBattleDialogCount; i++)
        if (!strcmp(kBattleDialogs[i], cls)) return g_dialogVars[i] ? *g_dialogVars[i] : nullptr;
    return nullptr;
}

static void HideBattleDialogs() {
    ResolveBattleDialogs();
    if (!E.mHide) return;
    for (int i = 0; i < kBattleDialogCount; i++) {
        void* f = g_dialogVars[i] ? *g_dialogVars[i] : nullptr;
        if (!f || !game::FormVisible(f)) continue;
        bool ok = false;
        SafeCall(E.mHide, U(f), 0, 0, 0, nullptr, &ok);
        Log("lockstep: %s hidden before the battle is loaded again (%s)", kBattleDialogs[i], ok ? "ok" : "FAILED");
    }
}

// ---- The strike dialog in a live battle (engine level 7) ----
// TdlgStrike is modeless: it stays open while minutes run, and its list items (Data), CurrentAirFormation and
// TheOriginatingDivision are raw pointers. The end of a minute may free formations (MoveAircraft merges a child that
// landed into its squadron and frees it), so right after every minute (OnBoundary, outside the simulation) its list is
// filled again from the game's lists by its own FillSquadronList, and Launch, Ready and Stand down wait for that
// (StrikeReady). Before every load the dialog is hidden (HideBattleDialogs); opening it again (Execute) refills it.

static void* StrikeDialog() {
    void* d = E.strikeDlgVar ? *E.strikeDlgVar : nullptr;
    return d && dl::IsInstanceOf(d, "TdlgStrike") ? d : nullptr;
}

// After a minute: the open dialog's list from the game's lists again (true: done, or nothing to do).
static bool RefreshStrikeList() {
    void* dlg = g_featStrike ? StrikeDialog() : nullptr;
    if (!dlg || !game::FormVisible(dlg) || !E.strikeFill) return true;  // (closed: Execute fills it when it opens)
    bool ok = false;
    void* div = E.fStrikeDiv > 0 ? dl::At<void*>(dlg, E.fStrikeDiv) : nullptr;
    if (!div || !IsDivision(div) || IndexIn(List(E.divisionsVar), div) < 0) {  // (no division to list: close it)
        SafeCall(E.mHide, U(dlg), 0, 0, 0, nullptr, &ok);
        Log("lockstep: the strike dialog closed after the minute (no division to list)");
        return true;
    }
    SafeCall(E.strikeFill, U(dlg), 0, 0, 0, nullptr, &ok);
    if (!ok) Log("lockstep: the strike dialog's list could not be refreshed after minute %d", S.boundary);
    return ok;
}

// The strike dialog's Launch, Ready and Stand down (and `ls airlaunch`): between minutes (the clock paused, the boundary
// processed, no received release waiting) and with its list filled after the last minute (refresh: fill it now when it
// is not, for the next click).
static bool StrikeReady(std::string& err, bool refresh = true) {
    if (!g_featStrike) {
        err = "Air strikes are not available in this live battle.";
        return false;
    }
    if (!CanOrderNow(err)) return false;
    if (!BetweenMinutes() || !S.atBoundary || Minute() != S.boundary || S.hasPendingRel) {
        err = "Pause the battle first: aircraft are launched, readied and stood down between minutes.";
        return false;
    }
    void* dlg = StrikeDialog();
    if (dlg && game::FormVisible(dlg) && !g_strikeListFresh) {
        if (refresh) RefreshStrikeList();
        err = "The strike list was out of date and has been refreshed: select the squadrons again.";
        return false;
    }
    return true;
}

// sbStrikeClick's work in a live battle: a pause request instead of PauseGame (which would stop only this PC's run), then
// the handler's own two calls (the list views' styles, Execute(nil, nil)). report: Execute(report, nil) as the reports
// list's "Launch strike" (`ls strikedlg <report>`).
static void OpenStrikeDialog(void* report) {
    void* dlg = StrikeDialog();
    if (!dlg) return;
    bool ok = false;
    SafeCall(E.strikeStyles, U(dlg), 0, 0, 0, nullptr, &ok);
    SafeCall(E.strikeExecute, U(dlg), U(report), 0, 0, nullptr, &ok);
    Log("lockstep: strike dialog opened%s (%s, %s)", report ? " for a report" : "", ok ? "ok" : "FAILED",
        game::FormVisible(dlg) ? "shown" : "not shown");
}

// The strike dialog opened with the host's option "Computer flies carrier strikes" on: it still launches this player's
// strikes, besides the computer's (said once per battle).
static void StrikeAutoNote() {
    if (!S.autoStrike || S.autoStrikeNoted) return;
    S.autoStrikeNoted = true;
    RunOnMainThread([] {
        Chat("The computer flies both sides' carrier strikes in this battle (the host's option \"Computer flies carrier "
             "strikes\"); the strikes you launch here fly as well, besides the computer's.");
    });
}

// The battle form as DoBattle makes it: free the old one, create a fresh (hidden) one into the unit variable.
static bool ResetBattleForm() {
    void* app = E.appVar ? *E.appVar : nullptr;
    if (!app || !E.vmtMain || !E.frmMainVar) return false;
    HideBattleDialogs();
    bool ok = true;
    void* f = *E.frmMainVar;
    if (f) {
        // The game's own teardown, as the battle screen's close runs it: freeing the form alone leaves the battle's
        // global lists behind, and the next load appends to them (the air formations doubled with every load).
        if (E.mClearData) {
            bool okClear = false, okAir = false;
            SafeCall(E.mClearData, U(f), 2, 1, 0, nullptr, &okClear);
            if (E.airCleanup) SafeCall(E.airCleanup, 0, 0, 0, 0, nullptr, &okAir);
            Log("lockstep: battle data cleared (%s), air formations left %d", okClear && okAir ? "ok" : "FAILED",
                dl::ListCount(List(E.airVar)));
        }
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

// The computer-control flags of a side's forces and divisions, in list order: "<forces>/<divisions>" as 0/1. The game's
// resume of a battle save gives side 1 to the computer (SetupGlobeRTWLayers sets side 1's AI flag in its side table and
// SetDivisionAIControl(resumed) sets every side-1 force and division to AIControlled), so a resync puts back what A's
// snapshot had (each PC applies it after its load, in the setup's shared section).
static std::string SideAiFlags(int side) {
    std::string f, d;
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* fc = dl::ListItem(forces, i))
            if (dl::At<int>(fc, E.fForceSide) == side) f += dl::At<uint8_t>(fc, E.fFAI) ? '1' : '0';
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++)
        if (void* dv = dl::ListItem(divs, i))
            if (dl::At<int>(dv, E.fDivSide) == side) d += dl::At<uint8_t>(dv, E.fDAI) ? '1' : '0';
    return f + "/" + d;
}

static bool ApplySideAiFlags(int side, const std::string& flags) {
    size_t slash = flags.find('/');
    if (slash == std::string::npos) return false;
    std::vector<void*> fl, dv;
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* fc = dl::ListItem(forces, i))
            if (dl::At<int>(fc, E.fForceSide) == side) fl.push_back(fc);
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++)
        if (void* d = dl::ListItem(divs, i))
            if (dl::At<int>(d, E.fDivSide) == side) dv.push_back(d);
    if (fl.size() != slash || dv.size() != flags.size() - slash - 1) return false;
    int nf = 0, nd = 0;
    for (size_t i = 0; i < fl.size(); i++) {
        uint8_t v = flags[i] == '1' ? 1 : 0;
        dl::At<uint8_t>(fl[i], E.fFAI) = v;
        nf += v;
    }
    for (size_t i = 0; i < dv.size(); i++) {
        uint8_t v = flags[slash + 1 + i] == '1' ? 1 : 0;
        dl::At<uint8_t>(dv[i], E.fDAI) = v;
        nd += v;
    }
    Log("lockstep: side %d's computer control as before the load: %d of %d forces, %d of %d divisions", side, nf,
        (int)fl.size(), nd, (int)dv.size());
    return true;
}

// Night CAP per force in list order ("0"/"1"). The battle save doesn't keep TForce.NightCAP (a load gives every force
// TForce.Create's default: on after 1930), so the start and resync messages carry the agreed values (`ncap=`) and both
// PCs put them back after the load.
static std::string NightCapFlags() {
    std::string s;
    if (E.fNightCap < 0) return s;
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* fc = dl::ListItem(forces, i)) s += dl::At<uint8_t>(fc, E.fNightCap) ? '1' : '0';
    return s;
}

static bool ApplyNightCap(const std::string& flags) {
    if (E.fNightCap < 0) return false;
    std::vector<void*> fl;
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* fc = dl::ListItem(forces, i)) fl.push_back(fc);
    if (fl.size() != flags.size()) return false;
    int on = 0, put = 0;
    for (size_t i = 0; i < fl.size(); i++) {
        uint8_t v = flags[i] == '1' ? 1 : 0;
        uint8_t& f = dl::At<uint8_t>(fl[i], E.fNightCap);
        if (f != v) put++;
        f = v;
        on += v;
    }
    Log("lockstep: night CAP as before the load: on for %d of %d forces (%d put back)", on, (int)fl.size(), put);
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

// ---------------------------------------------------------------------------------------------
// The map of this PC's side (engine level 15). The game decides what its map shows inside the simulation and in every
// battle load, for its PlayerSide (0 on both PCs there) and its side table's computer side (1 after a resume): the
// sighting routine's SetHiddenDiv shows the flag of every division whose force is not the computer's and its ships, then
// hides a computer side's ship unless it is sighted (GetBestSightingLevel > 0 and SeenByHumanControlledShips), shows the
// human side's spotted ships (moving their division's flag to the flagship) and labels every ship (names for the human
// side, the identification for the other). In a live battle both sides' forces are human: B's map showed side 0's
// picture (its own ships hidden until side 0 saw them, side 0's ships and names shown) and both maps showed every
// division's flag (A saw where B's fleet was). After every simulation step and every live load each PC applies the
// same rules for its own side, outside the simulation (the ship's Hidden is a display field there, the flags, wakes and
// labels are display objects). The simulation never reads what this writes.
static struct MapApi {
    bool tried = false, ok = false;
    void *setVisible = nullptr, *setHidden = nullptr, *relabel = nullptr;
    int fFlag = -1, fDivForce = -1, fRole = -1, fForceAI = -1, fFlagPoint = -1, fBothComputer = -1;
    int fOwnSpotted = -1, fOwnRadar = -1, fSeen = -1, fSight = -1, fRadar = -1, fLoc = -1, fType = -1, fSunk = -1;
    int fGeoState = 0x28;  // TGlobeObject's state word (bit 4: hidden), read only for the probe
} g_mapApi;
static bool g_mapOn = true;  // tests (`ls mapview off`): the game's own picture (as 0.4.0)
static bool g_cwBeforeLoad = true;
static int g_softWanted = -1;  // TEST ONLY (`ls x87 soft 0|1`): A's choice of the software maths (-1: RTW3MP_TEST_X87SOFT)  // tests (`ls testcwload 0`): B loads with its own x87 control word (as 0.4.0)
static int g_mapPasses = 0;
static std::string g_mapLast;  // the last pass's counts (logged when they change)

static bool ResolveMapView() {
    MapApi& m = g_mapApi;
    if (m.tried) return m.ok;
    m.tried = true;
    m.setVisible = dl::Method("TFightingShip", "SetVisible");
    // TfrmMain.SetHiddenDiv starts: cmp [esi+Side], 3; jne; mov eax, [esi+myFlag]; mov dl, 1; call <TGlobeObject hidden>
    if (uint8_t* h = Find(dl::Method("TfrmMain", "SetHiddenDiv"), 0x30, {0x8B, 0x46, -1, 0xB2, 0x01, 0xE8}))
        m.setHidden = CallTarget(h + 5);
    // CbShipNameDisplayChange: mov [edx], eax (the names setting); mov eax, ebx; call <label every ship>
    if (uint8_t* h = Find(dl::Method("TfrmMain", "CbShipNameDisplayChange"), 0x30, {0x89, 0x02, 0x8B, 0xC3, 0xE8}))
        m.relabel = CallTarget(h + 4);
    m.fFlag = dl::Field("TDivision", "myFlag");
    m.fDivForce = dl::Field("TDivision", "myForce");
    m.fRole = dl::Field("TDivision", "Role");
    m.fForceAI = dl::Field("TForce", "AIControlled");
    m.fFlagPoint = dl::Field("TDivLocation", "FlagPoint");
    m.fBothComputer = dl::Field("TfrmMain", "BothComputer");
    m.fOwnSpotted = dl::Field("TFightingShip", "OwnShipSpotted");
    m.fOwnRadar = dl::Field("TFightingShip", "OwnShipRadarSpotted");
    m.fSeen = dl::Field("TFightingShip", "SeenByHumanControlledShips");
    m.fSight = dl::Field("TFightingShip", "SightingLevel");
    m.fRadar = dl::Field("TFightingShip", "RadarSightingLevel");
    m.fLoc = dl::Field("TFightingShip", "ShipLocation");
    m.fType = dl::Field("TFightingShip", "ShipType");
    m.fSunk = dl::Field("TFightingShip", "Sunk");
    m.ok = m.setVisible && m.setHidden && m.relabel && m.fFlag >= 0 && m.fDivForce >= 0 && m.fRole >= 0 &&
           m.fForceAI >= 0 && m.fFlagPoint >= 0 && m.fBothComputer >= 0 && m.fOwnSpotted >= 0 && m.fOwnRadar >= 0 &&
           m.fSeen >= 0 && m.fSight >= 0 && m.fRadar >= 0 && m.fLoc >= 0 && m.fType >= 0 && m.fSunk >= 0 &&
           E.fShips >= 0 && E.fDivSide >= 0 && E.fShipSide >= 0;
    Log("lockstep: map of this PC's side: %s (SetVisible=%p hidden=%p labels=%p)", m.ok ? "ok" : "NOT AVAILABLE (the map "
        "shows the first side's picture)", m.setVisible, m.setHidden, m.relabel);
    return m.ok;
}

// The map's picture for this PC's side (see MapApi). Runs outside the simulation with this PC's view in place.
static void ApplyMapView(const char* why) {
    void* frm = BattleForm();
    int view = g_viewSide;
    if (!g_mapOn || !frm || view < 0 || view > 1 || g_loading || g_sim > 0 || !ResolveMapView()) return;
    MapApi& m = g_mapApi;
    if (dl::At<uint8_t>(frm, m.fBothComputer)) return;  // (the game's computer-vs-computer battle shows everything)
    // Right after a load SeenByHumanControlledShips holds only the first side's sightings (the load's sighting routine ran
    // while the second side's forces were still the computer's): the sighting levels alone decide until the next minute.
    bool afterLoad = !strcmp(why, "load");
    int shown[2] = {}, ships[2] = {}, flags[2] = {};
    bool ok = TryRun([&] {
        g_ui++;  // (display work: random draws of the labels' identification go to this PC's own stream)
        void* divs = List(E.divisionsVar);
        for (int i = 0; i < dl::ListCount(divs); i++) {
            void* dv = dl::ListItem(divs, i);
            if (!dv) continue;
            int side = dl::At<int>(dv, E.fDivSide);
            if (side != 0 && side != 1) continue;  // (side 3: the game's own rule)
            bool own = side == view;
            void* flag = dl::At<void*>(dv, m.fFlag);
            void* force = dl::At<void*>(dv, m.fDivForce);
            bool forceAI = force && dl::At<uint8_t>(force, m.fForceAI);
            // SetHiddenDiv for the human side: a computer force's division hides its flag and its ships
            bool flagHidden = !own || forceAI;
            void* list = dl::At<void*>(dv, E.fShips);
            for (int j = 0; j < dl::ListCount(list); j++) {
                void* s = dl::ListItem(list, j);
                if (!s || dl::At<uint8_t>(s, m.fSunk)) continue;
                bool vis;
                if (own)
                    vis = !flagHidden || dl::At<int>(s, m.fOwnSpotted) > 0 || dl::At<int>(s, m.fOwnRadar) > 0 ||
                          dl::At<uint8_t>(s, m.fType) == 12;
                else
                    vis = std::max(dl::At<int>(s, m.fSight), dl::At<int>(s, m.fRadar)) > 0 &&
                          (afterLoad || dl::At<uint8_t>(s, m.fSeen));
                dl::Call(m.setVisible, U(s), vis ? 1 : 0);
                ships[side]++;
                if (vis) shown[side]++;
            }
            // the human side's main division of a computer force keeps its flag (SetHiddenDiv's last rule)
            if (own && forceAI && dl::At<uint8_t>(dv, m.fRole) == 0) flagHidden = false;
            if (!flag) continue;
            dl::Call(m.setHidden, U(flag), flagHidden ? 1 : 0);
            if (!flagHidden) flags[side]++;
        }
        dl::Call(m.relabel, U(frm));  // every ship's map label for this PC's side
        g_ui--;
    });
    if (!ok && g_ui > 0) g_ui--;
    g_mapPasses++;
    char b[160];
    snprintf(b, sizeof(b), "own ships shown %d of %d, the other side's %d of %d, flags shown: own %d, other %d",
             shown[view], ships[view], shown[1 - view], ships[1 - view], flags[view], flags[1 - view]);
    if (!ok || g_mapLast != b) {
        g_mapLast = b;
        Log("lockstep: map of side %d (%s): %s%s", view, why, b, ok ? "" : " (FAILED: an exception)");
    }
    InvalidateRect(game::FormHwnd(frm), nullptr, FALSE);
}

// The ensign at the battle screen's top left (TfrmMain.ImgFlag): the resume copies the campaign screen's flag into it
// (LoadScenarioInProgress: frmMain.ImgFlag.Picture := <the campaign screen's flag image>.Picture), and the battle slot's
// campaign is the first player's, so B showed A's ensign. On the PC that shows side 1 it gets that side's flag, which
// the load read from the battle file (TfrmMain.LoadSideFlags: SideFlags[side] from the side's FlagFile). Display only.
static void ApplyOwnFlag() {
    static const int fImg = dl::Field("TfrmMain", "ImgFlag"), fSideFlags = dl::Field("TfrmMain", "SideFlags"),
                     fPicture = dl::Field("TImage", "FPicture");
    static void* const assign = dl::Method("TPicture", "Assign");
    void* frm = BattleForm();
    int view = g_viewSide;
    if (!g_mapOn || !frm || view != 1 || fImg < 0 || fSideFlags < 0 || fPicture < 0 || !assign) return;
    void* img = dl::At<void*>(frm, fImg);
    void* flag = dl::At<void*>(frm, fSideFlags + 4 * view);
    void* pic = img ? dl::At<void*>(img, fPicture) : nullptr;
    if (!pic || !flag || !dl::IsInstanceOf(flag, "TGraphic")) {
        Log("lockstep: the ensign of side %d not shown (no flag picture)", view);
        return;
    }
    bool ok = false;
    SafeCall(assign, U(pic), U(flag), 0, 0, nullptr, &ok);
    static int lastBid = -1;
    if (!ok || lastBid != S.bid) {
        lastBid = S.bid;
        Log("lockstep: the battle screen shows side %d's ensign%s", view, ok ? "" : " (FAILED: an exception)");
    }
}

// After the outermost simulation step that can set the map's visibility (the minute's start and end, a detach).
static void MapAfterStep(int outerIdx) {
    static bool busy = false;
    if (busy || !S.on || S.st != St::Running || g_loading || g_viewSide < 0) return;
    if (outerIdx != H_STARTMINUTE && outerIdx != H_EOTP && outerIdx != H_DETACH && outerIdx != H_CSDD) return;
    busy = true;
    ApplyMapView(outerIdx == H_EOTP ? "minute" : outerIdx == H_STARTMINUTE ? "minute start" : "detach");
    busy = false;
}

// B's battle screen while it loads: the game shows it and draws the first side's picture (its flags, the map centred on
// its fleet) before this PC's side is in place. It stays transparent until the load is done and this PC's map is drawn.
static HWND g_coverHwnd = nullptr;
static void CoverBattleForm(bool on) {
    static void* handleNeeded = dl::Method("TWinControl", "HandleNeeded");
    if (on) {
        void* f = BattleForm();
        if (!f || !handleNeeded) return;
        bool ok = false;
        SafeCall(handleNeeded, U(f), 0, 0, 0, nullptr, &ok);
        HWND h = game::FormHwnd(f);
        if (!ok || !h) return;
        SetWindowLongW(h, GWL_EXSTYLE, GetWindowLongW(h, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
        g_coverHwnd = h;
        return;
    }
    HWND h = g_coverHwnd;
    g_coverHwnd = nullptr;
    if (!h || !IsWindow(h)) return;
    SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
    SetWindowLongW(h, GWL_EXSTYLE, GetWindowLongW(h, GWL_EXSTYLE) & ~WS_EX_LAYERED);
    RedrawWindow(h, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

// The probe (`ls mapview`): per side the ships the map shows (Hidden off) and the flags shown.
static std::string MapViewProbe() {
    if (!ResolveMapView()) return "error the map pass is not available";
    MapApi& m = g_mapApi;
    int shown[2] = {}, ships[2] = {}, flags[2] = {}, divs2[2] = {}, wake[2] = {};
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* dv = dl::ListItem(divs, i);
        int side = dv ? dl::At<int>(dv, E.fDivSide) : -1;
        if (side != 0 && side != 1) continue;
        divs2[side]++;
        void* flag = dl::At<void*>(dv, m.fFlag);
        if (flag && !(dl::At<uint16_t>(flag, m.fGeoState) & 4)) flags[side]++;
        void* list = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(list); j++) {
            void* s = dl::ListItem(list, j);
            if (!s || dl::At<uint8_t>(s, m.fSunk)) continue;
            ships[side]++;
            if (g_fHidden >= 0 && !dl::At<uint8_t>(s, g_fHidden)) shown[side]++;
            static int fWake = dl::Field("TFightingShip", "Wake");
            void* w = fWake >= 0 ? dl::At<void*>(s, fWake) : nullptr;
            if (w && !(dl::At<uint16_t>(w, m.fGeoState) & 4)) wake[side]++;
        }
    }
    char b[256];
    snprintf(b, sizeof(b),
             "ok view=%d on=%d passes=%d side0=%d/%d wake0=%d flags0=%d/%d side1=%d/%d wake1=%d flags1=%d/%d "
             "playerSide=%d",
             g_viewSide, g_mapOn ? 1 : 0, g_mapPasses, shown[0], ships[0], wake[0], flags[0], divs2[0], shown[1],
             ships[1], wake[1], flags[1], divs2[1], E.playerSideVar ? *E.playerSideVar : -1);
    return b;
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

// Fastest available is clock state: the game's speed governor reads the button at the minute boundaries (with ships in
// contact a speed above Fast drops to 2 when it is down, else to 1; without contact it sets 5 when it is down), so both
// PCs keep it as A's releases and starts set it. A click on it asks A's clock instead (B_FASTEST).
static bool FastestShared() { return E.setDown && E.setImageIndex && E.fFastestBtn > 0 && E.fDownOff > 0; }

static void* FastestButton(void* m) { return m && E.fFastestBtn > 0 ? dl::At<void*>(m, E.fFastestBtn) : nullptr; }

static bool FastestDown(void* m) {
    void* b = FastestButton(m);
    return b && E.fDownOff > 0 && dl::At<uint8_t>(b, E.fDownOff) != 0;
}

// The button down with its pressed glyph, or up, as sbFastestAvailableClick leaves it.
static void SetFastestButton(void* m, bool down) {
    void* b = FastestButton(m);
    if (!b || !FastestShared()) return;
    bool ok = false;
    SafeCall(E.setDown, U(b), down ? 1 : 0, 0, 0, nullptr, &ok);
    SafeCall(E.setImageIndex, U(b), down ? 1 : 0, 0, 0, nullptr, &ok);
}

// Both PCs, inside a shared section: Fastest available as A released it (the release's speed follows: 5 or 2 for the
// button, as its click sets them, or the speed box's).
static void ApplyFastest(bool on) {
    bool was = S.fastest != 0;
    S.fastest = on ? 1 : 0;
    SetFastestButton(BattleForm(), on);
    if (was != on) Log("lockstep: Fastest available %s", on ? "on" : "off");
}

static void LockMissilePanel();
static void ShowFireAtWill();

// Each side's agreed Fire at will (engine level 9) as two digits ("11"), and back (both PCs, after the load).
static std::string FireAtWillText() { return std::string(1, g_faw[0] ? '1' : '0') + (g_faw[1] ? '1' : '0'); }

static void ApplyFireAtWill(const std::string& faw) {
    if (!g_featFireAtWill) return;
    for (int s = 0; s < 2; s++) g_faw[s] = faw.size() > (size_t)s ? (faw[s] == '0' ? 0 : 1) : kFireAtWillDefault;
    Log("lockstep: Fire at will per side %s", FireAtWillText().c_str());
}

// The ships' pending heavy SSM targets (engine level 9): the battle save doesn't keep PendingMissileTarget and the load
// clears it, so the start and resync messages carry the agreed ones: "<index in the all-ships list>:<24 hex digits>;..."
// for the ships whose target is set (IsEmptyPoint: the first two dwords zero).
static std::string PendingTargetsText() {
    std::string s;
    if (E.fPendingMsl <= 0) return s;
    void* ships = List(E.allShipsVar);
    for (int i = 0; i < dl::ListCount(ships); i++)
        if (void* sp = dl::ListItem(ships, i)) {
            uint8_t* p = (uint8_t*)sp + E.fPendingMsl;
            if (*(uint32_t*)p || *(uint32_t*)(p + 4)) s += std::to_string(i) + ":" + Hex(p, 12) + ";";
        }
    return s;
}

static bool ApplyPendingTargets(const std::string& pmt) {
    if (E.fPendingMsl <= 0) return pmt.empty();
    void* ships = List(E.allShipsVar);
    int n = dl::ListCount(ships), put = 0;
    for (auto& e : Split(pmt, ';')) {
        size_t c = e.find(':');
        if (e.empty()) continue;
        if (c == std::string::npos) return false;
        int i = atoi(e.substr(0, c).c_str());
        void* sp = i >= 0 && i < n ? dl::ListItem(ships, i) : nullptr;
        if (!sp || !Unhex(e.substr(c + 1), (uint8_t*)sp + E.fPendingMsl, 12)) return false;
        put++;
    }
    if (put) Log("lockstep: %d pending missile target(s) as before the load", put);
    return true;
}

// ---- The marker of a resumable live battle (phase 5b) ----
// Every snapshot A writes into the host's campaign slot (its own battle: the start, a resync, Save, a rejoin) also
// writes rtw3mp_livebattle.ini next to it: what a rejoin needs (the battle id and epoch, the players, the side names,
// the carrier-strike rule), the agreed state the battle save drops (Fire at will per side, night CAP per force, the
// pending missile targets) with the minute they belong to, whether the battle was a test battle outside a month, and a
// hash of the battle save. Only a campaign load that resumes exactly that battle save uses it (ResumeFromMarker), so a
// marker left behind never applies to another battle. The session's file transfers leave rtw3mp* files out.
static const wchar_t kMarkerName[] = L"rtw3mp_livebattle.ini";

static std::wstring MarkerPath(int slot) { return game::SaveDir(slot) + kMarkerName; }

// The battle save of the slot (RTWGame<slot>.sac) as "<FNV-1a 64>-<size>", or "" without one.
static std::string SacHash(int slot) {
    std::string data;
    if (!ReadFileBytes(game::SaveDir(slot) + L"RTWGame" + std::to_wstring(slot) + L".sac", data) || data.empty())
        return "";
    uint64_t h = 14695981039346656037ull;
    Fnv(h, data.data(), data.size());
    char b[48];
    snprintf(b, sizeof(b), "%016llx-%u", (unsigned long long)h, (unsigned)data.size());
    return b;
}

static void WriteMarker(int slot) {
    if (slot < 1 || slot > 49) return;  // (the host's campaign slots)
    std::string sac = SacHash(slot);
    if (sac.empty()) {
        Log("lockstep: resume: no marker for slot %d (the battle save is missing)", slot);
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    char when[32];
    snprintf(when, sizeof(when), "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    KV mk;
    mk["v"] = "1";
    mk["bid"] = std::to_string(S.bid);
    mk["epoch"] = std::to_string(S.epoch);
    mk["opp"] = std::to_string(S.opp);
    mk["host"] = S.myName;
    mk["peer"] = S.peerName;
    mk["s0"] = S.sideNames[0];
    mk["s1"] = S.sideNames[1];
    mk["autostrike"] = S.autoStrike ? "1" : "0";
    mk["faw"] = FireAtWillText();
    mk["ncap"] = NightCapFlags();
    mk["pmt"] = PendingTargetsText();
    mk["minute"] = std::to_string(Minute());
    mk["slotmode"] = game::BattleSlotMode() ? "1" : "0";  // (the host's test battle outside a month)
    mk["sac"] = sac;
    mk["saved"] = when;
    if (WriteFileBytes(MarkerPath(slot), KVEncode(mk)))
        Log("lockstep: resume: marker of slot %d written (battle %d epoch %d, minute %d, %s, sac %s)", slot, S.bid,
            S.epoch, Minute(), S.peerName.c_str(), sac.c_str());
    else
        Log("lockstep: resume: the marker of slot %d could NOT be written", slot);
}

static void DeleteMarker(int slot, const char* why) {
    if (slot < 1 || slot > 49) return;
    std::wstring p = MarkerPath(slot);
    if (!FileExists(p)) return;
    if (DeleteFileW(p.c_str()))
        Log("lockstep: resume: marker of slot %d deleted (%s)", slot, why);
    else
        Log("lockstep: resume: the marker of slot %d could not be deleted (%s, error %lu)", slot, why, GetLastError());
}

// The game's own Save (sbSaveClick outside a live battle) of a battle that can be rejoined: a single-player battle
// after a drop, or a resumed battle after Run. The marker follows the new battle save (its hash, minute and the state
// the save drops), so a restart after this Save resumes it as a live battle too. Runs posted, after the handler's save.
static void RefreshMarkerAfterSave() {
    if (S.on || !g_rj.on || g_rj.slot <= 0 || g_rj.slot != game::CurrentSlot() || RejoinInvalid()) return;
    std::string text;
    if (!ReadFileBytes(MarkerPath(g_rj.slot), text)) return;
    KV mk = KVDecode(text);
    std::string sac = SacHash(g_rj.slot);
    if (sac.empty() || KVStr(mk, "sac") == sac) return;  // (nothing saved: "The scenario is over", a quick battle)
    mk["epoch"] = std::to_string(g_rj.epoch);
    mk["ncap"] = NightCapFlags();
    mk["pmt"] = PendingTargetsText();
    mk["minute"] = std::to_string(Minute());
    mk["sac"] = sac;
    if (WriteFileBytes(MarkerPath(g_rj.slot), KVEncode(mk)))
        Log("lockstep: resume: marker of slot %d follows the game's own save (battle %d, minute %d, sac %s)", g_rj.slot,
            g_rj.bid, Minute(), sac.c_str());
}

// DESYNC forensics (defined with the state dump below).
static void FlightBoundary();
static void FlightReset();
static void DesyncReport(int minute, const std::string& peerHash, const char* why = nullptr);
static void CorruptForTest();
static bool g_testReadyDiff = false;  // tests (`ls testreadydiff`): this PC's next live load differs

// Loads the slot's battle with the shared seed (both PCs do exactly this). first = the first start of this battle;
// ai0/ai1 = side 0's and side 1's computer-control flags in A's snapshot (SideAiFlags): side 0's go back after every
// load (engine level 13), side 1's after all but the first; autoState = auto-run
// per side as A's start message says ("01": side 1 on auto-run), applied after them; ncap = night CAP per force
// (NightCapFlags: the battle save drops it); faw = Fire at will per side and pmt = the pending missile targets (session
// state and a field the battle save drops); pa = the forces' computer control before auto-run per side (PreAutoText).
static bool LoadSnapshot(int slot, uint32_t seed, int speed, int fastest, bool first, const std::string& ai0,
                         const std::string& ai1, const std::string& autoState, const std::string& ncap,
                         const std::string& faw, const std::string& pmt, const std::string& pa, std::string& err) {
    DWORD t0 = GetTickCount();
    SetView(-1);
    ClearOrders();
    g_exitHashOk = false;  // (leak check) a new state
    g_loading = true;
    S.minuteRunning = false;
    S.tokens = 0;
    bool ok = ResetBattleForm();
    if (ok && !S.isA) CoverBattleForm(true);  // (B: the first side's picture of the load is never shown)
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
        CoverBattleForm(false);
        err = ok ? "the battle did not open" : "the game could not load the battle";
        Log("lockstep: load of slot %d FAILED (%s)", slot, err.c_str());
        return false;
    }
    if (S.cw0) {
        unsigned cw = FpuCw();
        if (cw != S.cw0) Log("lockstep: the x87 control word changed during the load: %04x (the battle's: %04x)", cw, S.cw0);
        SetFpuCw(S.cw0);
    }
    AlignLogLists(BattleForm(), "the load");  // (display data: else the next minute shows "Log lines out of sync!")
    SharedBegin();
    bool setup = TryRun([&] {
        // Auto-run per side is session state: the battle save keeps the sides on auto-run under their players
        // (SaveSnapshot), and each one goes to the computer again below.
        S.autoRun[0] = autoState.size() >= 2 && autoState[0] == '1';
        S.autoRun[1] = autoState.size() >= 2 && autoState[1] == '1' && !S.peerAI;
        // The game's resume (SetDivisionAIControl at 0xda9f75) gives every PlayerSide division of a computer force to
        // the computer, also one its player took back: side 0's forces and divisions as A's snapshot had them (engine
        // level 13; on a first load they are the battle save's already).
        if (!ApplySideAiFlags(0, ai0))
            Log("lockstep: side 0's computer-control flags could not be put back (%s): the battle save's stay",
                ai0.empty() ? "none were sent" : "the forces or divisions differ");
        if (first) {
            g_human[0] = true;
            SetSideControl(1, false, true);  // the other player commands side 1
        } else {
            g_human[0] = !S.autoRun[0];
            g_human[1] = !S.peerAI && !S.autoRun[1];
            void* ships = List(E.allShipsVar);
            for (int i = 0; i < dl::ListCount(ships); i++)
                if (void* s = dl::ListItem(ships, i)) {
                    int side = dl::At<int>(s, E.fShipSide);
                    if (side == 0 || side == 1) dl::At<uint8_t>(s, E.fHuman) = (uint8_t)g_human[side];
                }
            // The game's resume gave side 1's forces and divisions to the computer: put back what the snapshot had.
            if (!S.peerAI && !ApplySideAiFlags(1, ai1)) {
                Log("lockstep: side 1's computer-control flags could not be put back (%s): side 1 is human",
                    ai1.empty() ? "none were sent" : "the forces or divisions differ");
                SetSideControl(1, false, true);
            }
        }
        if (S.peerAI) SetSideControl(1, true, false);
        // The forces' computer control before auto-run (A's record: the same on both PCs from here); a side on auto-run
        // without one (the game's own auto-run of side 0 at a rejoin) takes the loaded flags, which the snapshot wrote
        // as they were (SaveSnapshot).
        size_t slash = pa.find('/');
        S.preAutoF[0] = slash == std::string::npos ? std::string() : pa.substr(0, slash);
        S.preAutoF[1] = slash == std::string::npos ? std::string() : pa.substr(slash + 1);
        for (int side = 0; side < 2; side++)
            if (S.autoRun[side]) {
                if (S.preAutoF[side].empty()) S.preAutoF[side] = ForceAiFlags(side);
                Log("lockstep: side %d is on auto-run: the computer commands it again after the load (its forces' "
                    "control before auto-run: %s)",
                    side, S.preAutoF[side].c_str());
                SetSideControl(side, true, false);
            } else {
                S.preAutoF[side].clear();
            }
        if (!ncap.empty() && !ApplyNightCap(ncap))
            Log("lockstep: night CAP could not be put back (the forces differ): the load's defaults stay");
        ApplyFireAtWill(faw);  // (session state: the same on both PCs from here)
        if (!ApplyPendingTargets(pmt))
            Log("lockstep: the pending missile targets could not be put back (the ships differ): none is pending");
        SetSimSideFromAuto();
        ApplyFastest(fastest != 0);  // the battle save doesn't keep it (a new battle screen has it up)
        ApplySpeed(speed);
        if (g_testReadyDiff) {  // TEST ONLY (`ls testreadydiff`): this PC's load differs (the loaded battles differ)
            g_testReadyDiff = false;
            CorruptForTest();
        }
    });
    SharedEnd();
    if (!setup) {
        CoverBattleForm(false);
        err = "the loaded battle could not be set up";
        return false;
    }
    S.boundary = Minute();
    S.atBoundary = true;
    S.minuteBroken = false;
    S.stalledSince = 0;
    g_airMark = dl::ListCount(List(E.airVar));
    g_strikeListFresh = true;  // (the strike dialog was hidden before the load; opening it fills its list)
    TakeAgreed();
    SetView(g_noView ? -1 : S.mySide);
    g_traceSeq.clear();
    FlightReset();  // (DESYNC forensics: the loaded objects as the boundary before the next minute)
    RefreshView();
    ApplyMapView("load");  // (engine level 15: this PC's map; the load drew the first side's picture)
    ApplyOwnFlag();
    CoverBattleForm(false);
    LockMissilePanel();
    ShowFireAtWill();  // (a new battle screen: the box as the agreed setting of this player's side)
    SetAutoButton(S.autoRun[S.mySide]);  // (the game's FormShow put it up)
    Log("lockstep: battle loaded from slot %d (seed %u, minute %d, t=%s) in %lu ms%s", slot, seed, S.boundary,
        ClockText().c_str(), GetTickCount() - t0, S.autoRun[0] || S.autoRun[1] ? (", auto-run " + AutoText()).c_str() : "");
    return true;
}

static std::string HashNow() {
    CheckFpu("the hash");
    char b[48];
    snprintf(b, sizeof(b), "%016llx-%08x", (unsigned long long)StateHash().all, g_simState);
    return b;
}

// Engine level 16: the READY hash in parts, for the diagnostics of a mismatch: each hash group (32 bits, kGroupNames'
// order), the battle clock with the campaign date, and the random-number state, as hex.
static const char* const kPartWords[G_COUNT + 2] = {"positions", "damage", "ammunition", "sightings", "identification",
                                                    "orders", "aircraft", "torpedoes", "forces", "submarines", "clock",
                                                    "random numbers"};
static std::string ReadyParts() {
    HashResult h = StateHash();
    std::string s;
    char b[24];
    for (int g = 0; g < G_COUNT; g++) {
        snprintf(b, sizeof(b), "%08x", (unsigned)(h.group[g] ^ (h.group[g] >> 32)));
        s += b;
    }
    uint64_t ck = 1469598103934665603ull;
    if (E.clockVar) Fnv(ck, E.clockVar, 8);
    int32_t date[3];
    if (CampaignDateNow(date)) Fnv(ck, date, sizeof(date));
    snprintf(b, sizeof(b), "%08x%08x", (unsigned)(ck ^ (ck >> 32)), g_simState);
    return s + b;
}

// A: what differed between the two loads, in a few words a player can read out: the differing parts, both CPU vendors
// if they differ, both x87 control words if B's own differs from the battle's ("" when nothing is known).
static std::string ReadyDiag() {
    std::string out;
    const std::string &a = S.myReadyParts, &b = S.peerReadyParts;
    if (a.size() == b.size() && a.size() == (G_COUNT + 2) * 8)
        for (int i = 0; i < G_COUNT + 2; i++)
            if (a.compare(i * 8, 8, b, i * 8, 8)) out += std::string(out.empty() ? "" : ", ") + kPartWords[i];
    std::string me = x87::CpuVendor();
    auto nm = [](const std::string& n, const char* d) { return n.empty() ? std::string(d) : n; };
    if (!S.peerCpu.empty() && S.peerCpu != me)
        out += (out.empty() ? "CPUs " : "; CPUs ") + me + " (" + nm(S.myName, "A") + "), " + S.peerCpu + " (" +
               nm(S.peerName, "B") + ")";
    if (S.peerOwnCw && S.peerOwnCw != S.cw0) {
        char cw[96];
        snprintf(cw, sizeof(cw), "%scontrol words %04X (%s), %04X (%s)", out.empty() ? "" : "; ", S.cw0,
                 nm(S.myName, "A").c_str(), S.peerOwnCw, nm(S.peerName, "B").c_str());
        out += cw;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Dialog policy. Inside a minute (the simulation) the game's questions get a fixed answer on both PCs: keep the battle
// going (while both players' ships are on auto-run: "end the scenario?" Yes, the game's own end). Their texts go to the
// chat of the player each line is about (GameTextFor: fog of war), and a break names the last text of its own minute.

static std::string g_lastBox;
static std::string g_lastQuietBox;  // the last box of the mod's own quiet calls (never a pause's reason)
static std::string g_minuteText[2];  // A: the running minute's last game text each player may read (the break's reason)
static bool g_pocGate = false, g_pocGateHeld = false, g_pocHash = false;
static int g_pocTokens = 0, g_pocHashCount = 0;
static double g_pocLastHashClock = -1;

static bool Contains(const std::wstring& s, const wchar_t* sub) {
    std::wstring a = s, b = sub;
    for (auto& c : a) c = towlower(c);
    for (auto& c : b) c = towlower(c);
    return a.find(b) != std::wstring::npos;
}

static void OnGameText(const std::wstring& text, bool proposal, int siteSide);

// A box's text for the chat: file paths shortened to the file name (the battle's "Game saved! <path>" names the
// player's own folders, and a pause line goes to both players).
static std::string ChatSafe(const std::wstring& text) {
    std::wstring s = text;
    for (size_t i = 0; i + 2 < s.size(); i++) {
        bool drive = iswalpha(s[i]) && s[i + 1] == L':' && (s[i + 2] == L'\\' || s[i + 2] == L'/') &&
                     (i == 0 || !iswalnum(s[i - 1]));
        if (!drive) continue;
        size_t eol = s.find_first_of(L"\r\n", i);
        if (eol == std::wstring::npos) eol = s.size();
        size_t sep = s.find_last_of(L"\\/", eol - 1);
        if (sep != std::wstring::npos && sep >= i) s.erase(i, sep + 1 - i);
    }
    return W2U(s);
}

// ---- Whose game text is it (fog of war) ----
// The shared simulation writes its messages from the first side's point of view ("Enemy ship <identification> is hit"),
// and, both sides being human there, also for the second side's own units ("<division> is out of sight of the
// flagship ...", "<ship> reports a sonar contact", its divisions' questions, "Our ship <name> is hit" while it runs with
// PlayerSide 2) and for both sides' sightings ("Unknown ship sighted!" whichever side's division sighted). Each line goes
// only to the player it is about: a sighting box to the side of the division that sighted (BoxSiteSide); "Enemy ship
// ..." and "The enemy ship ..." lines are the first side's view; other lines by the units they name with their real
// names (the divisions', else the ships' and air formations' of one side only; whole words, not "<name>-class", which
// is an identification); the battle as a whole ("scenario", "no ships left at sea") to both; the rest stays the first
// player's, as before, and the second player reads neutral wording. (The game's own battle log panel still lists every
// message on both PCs: a known limit.)
struct UnitName {
    std::wstring name;
    int side;
    bool division;
};

static void UnitNames(std::vector<UnitName>& out) {
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* d = dl::ListItem(divs, i);
        int side = d ? dl::At<int>(d, E.fDivSide) : -1;
        if ((side == 0 || side == 1) && E.fDivName > 0) out.push_back({dl::ReadUStr(dl::At<void*>(d, E.fDivName)), side, true});
    }
    void* ships = List(E.allShipsVar);
    for (int i = 0; i < dl::ListCount(ships); i++) {
        void* s = dl::ListItem(ships, i);
        int side = s && E.fShipSide > 0 ? dl::At<int>(s, E.fShipSide) : -1;
        if ((side == 0 || side == 1) && E.fShipName > 0)
            out.push_back({dl::ReadUStr(dl::At<void*>(s, E.fShipName)), side, false});
    }
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air) && E.fAfName > 0 && E.fAfSide > 0; i++) {
        void* a = dl::ListItem(air, i);
        int side = a ? dl::At<int>(a, E.fAfSide) : -1;
        if (side == 0 || side == 1) out.push_back({dl::ReadUStr(dl::At<void*>(a, E.fAfName)), side, false});
    }
}

static bool NamesUnit(const std::wstring& line, const std::wstring& name) {
    if (name.size() < 2) return false;
    for (size_t pos = line.find(name); pos != std::wstring::npos; pos = line.find(name, pos + 1)) {
        size_t end = pos + name.size();
        if (pos > 0 && iswalnum(line[pos - 1])) continue;
        if (end < line.size() && iswalnum(line[end])) continue;
        if (line.compare(end, 6, L"-class") == 0) continue;
        return true;
    }
    return false;
}

// 0 or 1: that side's; -1: both (the battle as a whole); -2: not known (the first player's, as before).
static int LineSide(const std::wstring& line, const std::vector<UnitName>& units) {
    if (line.rfind(L"Enemy ship ", 0) == 0 || line.rfind(L"The enemy ship ", 0) == 0) return 0;
    bool div[2] = {false, false}, other[2] = {false, false};
    for (auto& u : units)
        if (NamesUnit(line, u.name)) (u.division ? div : other)[u.side] = true;
    if (div[0] != div[1]) return div[0] ? 0 : 1;
    if (other[0] != other[1]) return other[0] ? 0 : 1;
    if (Contains(line, L"scenario") || Contains(line, L"no ships left at sea")) return -1;
    return -2;
}

// The lines of a game text for each player (out[0]: the first player's, out[1]: the second's; ChatSafe, one line).
// siteSide 0 or 1: the whole text is that side's (a sighting box).
static void GameTextFor(const std::wstring& text, int siteSide, std::string out[2]) {
    std::vector<UnitName> units;
    if (siteSide != 0 && siteSide != 1) UnitNames(units);
    std::wstring part[2];
    size_t i = 0;
    while (i <= text.size()) {
        size_t j = text.find_first_of(L"\r\n", i);
        if (j == std::wstring::npos) j = text.size();
        std::wstring line = text.substr(i, j - i);
        size_t a = line.find_first_not_of(L' '), b = line.find_last_not_of(L' ');
        if (a != std::wstring::npos) {
            line = line.substr(a, b - a + 1);
            int side = siteSide == 0 || siteSide == 1 ? siteSide : LineSide(line, units);
            for (int p = 0; p < 2; p++)
                if (side == p || side == -1 || (p == 0 && side == -2)) part[p] += (part[p].empty() ? L"" : L" ") + line;
        }
        i = j + 1;
    }
    for (int p = 0; p < 2; p++) out[p] = ChatSafe(part[p]);
}

// A sighting box ("Unknown ship sighted!", "Land installation sighted!", "Unknown radar contact!" in the sighting
// reports routine 0xdc960c): right after its ShowMessagePos the caller reads the side of the division that sighted (mov
// eax, [ebp+d8]; mov eax, [eax+TDivision.Side]), so the box is that side's. -1 for any other box.
static int BoxSiteSide(const uint32_t* entryEsp, uint32_t ebp) {
    if (!entryEsp || !E.smpRet || E.fDivSide <= 0 || E.fDivSide > 0x7f || entryEsp[0] != E.smpRet) return -1;
    int side = -1;
    __try {
        uint32_t ret = entryEsp[5];  // (ShowMessagePos pushed four arguments)
        const uint8_t* c = (const uint8_t*)(uintptr_t)ret;
        if (InImage(ret) && c[0] == 0x8B && c[1] == 0x45 && c[3] == 0x8B && c[4] == 0x40 && c[5] == (uint8_t)E.fDivSide) {
            void* div = *(void**)(uintptr_t)(ebp + (int32_t)(int8_t)c[2]);
            if (IsDivision(div)) side = dl::At<int>(div, E.fDivSide);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        side = -1;
    }
    return side == 0 || side == 1 ? side : -1;
}

extern "C" int __stdcall LS_MsgDlg(const wchar_t* msg, int type, int buttons, const uint32_t* entryEsp, uint32_t callerEbp) {
    (void)type;
    if (g_results > 0 && !g_quiet) return -1;  // the result screen's questions are each player's own
    bool sim = g_sim > 0 && S.on;
    if (!sim && !g_quiet && !g_answerYes) return -1;  // the player's own dialogs
    std::wstring text = msg ? dl::ReadUStr(msg) : L"";
    bool yes = buttons & 1, no = (buttons & 2) != 0, ok = (buttons & 4) != 0, cancel = (buttons & 8) != 0;
    int r = ok ? 1 : yes ? 6 : cancel ? 2 : 7;
    bool proposal = false, autoEnd = false;
    if (yes && no) {
        if (g_answerYes)
            r = 6;
        else if (Contains(text, L"continue")) {
            r = 6;  // "... Do you want to continue searching?"
            proposal = Contains(text, L"search");
        } else {
            r = 7;  // "... Do you want to end the scenario?", "Stand down strike?", detach questions
            proposal = Contains(text, L"end the scenario");
            // Both players' ships on auto-run (the same on both PCs: it changes only at releases): the game's own end
            // conditions end the battle, as in single player.
            if (proposal && sim && g_dfActor < 0 && BothAuto()) {
                r = 6;
                proposal = false;
                autoEnd = true;
            }
        }
    }
    if (g_dfActor >= 0) {  // a deferred action's replay: only the player who gave it sees the game's message
        std::string t = W2U(text);
        bool mine = g_dfActor == S.mySide;
        Log("lockstep: deferred action's box '%s' answered %d (%s)", t.c_str(), r, mine ? "chat" : "quiet");
        if (mine && !t.empty()) {
            std::string c = ChatSafe(text);
            RunOnMainThread([c] { Chat("Battle: " + c); });
        }
        return r;
    }
    if (sim && g_searchBoxes > 0) {  // the forces' air search: a question about one side's ship (its name) stays in the log
        Log("lockstep: air search box '%s' answered %d (not shown)", W2U(text).c_str(), r);
        return r;
    }
    if (sim && !g_quiet && Contains(text, L"Log lines out of sync")) {  // the battle log's lists (AlignLogLists)
        Log("lockstep: minute box '%s' answered %d (the battle log's two lists differ: display data, not shown)",
            W2U(text).c_str(), r);
        return r;
    }
    if (!g_quiet)
        g_lastBox = ChatSafe(text);  // the mod's own saves' boxes are never a reason for a pause
    else
        g_lastQuietBox = W2U(text);  // (the bridge's missile dialog probe reports it)
    int site = sim && !g_quiet && !g_answerYes ? BoxSiteSide(entryEsp, callerEbp) : -1;
    Log("lockstep: %s box '%s' answered %d%s%s", g_quiet ? "quiet" : g_answerYes ? "end" : "minute", W2U(text).c_str(), r,
        autoEnd ? " (both sides on auto-run)" : "", site >= 0 ? (" (side " + std::to_string(site) + "'s sighting)").c_str() : "");
    if (autoEnd) {  // (both PCs answer it at the same minute: each says it)
        std::string t = ChatSafe(text);
        RunOnMainThread([t] { Chat("The game asks: \"" + t + "\" Both players' ships are on auto-run: yes, the battle ends."); });
        return r;
    }
    if (sim) OnGameText(text, proposal, site);
    return r;
}

extern "C" int __stdcall LS_ShowPic(const wchar_t* text, const wchar_t* pic) {
    if (!(S.on && g_sim > 0 && g_results == 0) && !g_quiet) return 0;
    std::wstring t = text ? dl::ReadUStr(text) : L"";
    Log("lockstep: picture message '%s' (%s) skipped", W2U(t).c_str(), pic ? W2U(dl::ReadUStr(pic)).c_str() : "");
    if (g_dfActor >= 0) {  // a deferred action's replay: for its player only
        std::string u = ChatSafe(t);
        if (g_dfActor == S.mySide && !u.empty()) RunOnMainThread([u] { Chat("Battle: " + u); });
        return 1;
    }
    if (!g_quiet) OnGameText(t, false, -1);
    return 1;
}

extern "C" int __stdcall LS_ExecGeneral(const wchar_t* prompt, int preselect) {
    bool sim = S.on && g_sim > 0 && g_results == 0;
    if (!sim && !g_quiet && g_testAutostart > 0) {  // tests: the host's own load of a battle in progress (phase 5b)
        std::wstring t = prompt ? dl::ReadUStr(prompt) : L"";
        if (Contains(t, L"autostart")) {
            int r = g_testAutostart;
            g_testAutostart = 0;
            Log("lockstep: question '%s' answered %d (test: ls autostart)", W2U(t).c_str(), r);
            return r;
        }
    }
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

// Phase 5b: a battle the game resumed from the host's save waits for its player: no minute starts until the rejoin
// starts it (RejoinTick) or the host presses Run (the battle goes on against the computer and stays rejoinable).
static bool ResumeHeld() { return !S.on && g_rj.on && g_rj.held && !RejoinInvalid(); }

static void ReleaseResumeHold(const char* how) {
    if (!g_rj.held) return;
    g_rj.held = false;
    Log("lockstep: resume: %s: battle %d goes on against the computer (minute %d, t=%s); %s can still rejoin it", how,
        g_rj.bid, Minute(), ClockText().c_str(), g_rj.peerName.c_str());
    std::string who = g_rj.peerName, side1 = g_rj.sideNames[1].empty() ? std::string("side 1") : g_rj.sideNames[1];
    RunOnMainThread([who, side1] {
        Chat("The battle goes on against the computer, which commands " + side1 + " for now. If " + who +
             " joins the session during this battle, " + who + " gets " + side1 + " back at the next minute.");
    });
}

static int MinuteGate(void* self) {
    if (!S.on && g_rjTry.want) {  // a rejoin: the battle stops at this minute boundary (RejoinTick starts it again)
        if (!g_rjTry.gateLogged) {
            g_rjTry.gateLogged = true;
            Log("lockstep: rejoin: the next minute is held (minute %d, t=%s)", Minute(), ClockText().c_str());
        }
        dl::At<int>(self, E.fMinutesLeft) = 0;
        return 0;
    }
    if (ResumeHeld()) {
        // A key that runs one minute directly (FormKeyUp calls StartMinute without a run: MinutesLeft 0) is the host's
        // Run too; a run the Run hook didn't see (minutes left) waits.
        if (dl::At<int>(self, E.fMinutesLeft) <= 0) {
            ReleaseResumeHold("a key ran one minute");
            return 1;
        }
        if (!g_rj.gateLogged) {
            g_rj.gateLogged = true;
            Log("lockstep: resume: the next minute is held (minute %d, t=%s)", Minute(), ClockText().c_str());
        }
        dl::At<int>(self, E.fMinutesLeft) = 0;
        return 0;
    }
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
    // (every live state: a minute this PC started before a new start of the battle arrived (B) ends here too)
    if (S.on && S.st != St::Ended) {
        S.minuteRunning = false;
        S.minuteBroken = false;
        S.stalledSince = 0;
        g_airMark = dl::ListCount(List(E.airVar));  // launches from the screen are appended from here (SortLaunched)
        g_strikeListFresh = false;                  // the minute may have freed formations the strike dialog lists
        RunOnMainThread(OnBoundary);  // after the interval timer's handler has finished
    }
}

extern "C" void __stdcall LS_AfterEOTP(void* self) {
    try {
        AfterMinute();
    } catch (...) {
    }
}

static void RejoinRunNote();

extern "C" int __stdcall LS_StartRun(void* self, int n) {
    if (!S.on && g_rjTry.want) {  // a rejoin waits for this boundary: the battle runs again once both PCs have it
        RunOnMainThread(RejoinRunNote);
        return 1;
    }
    if (ResumeHeld()) {  // the host's Run (or auto-run): the resumed battle goes on against the computer
        ReleaseResumeHold("Run pressed");
        return 0;
    }
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

// A blocked click on a toggle speed button: VCL flipped its Down state before the handler ran; put it back, so the screen
// and the code that reads the button (the division report's OK reads the land-CAP button) keep what is in effect.
static void PutToggleBack(void* btn) {
    if (!btn || !E.setDown || E.fDownOff <= 0 || E.fGroupOff <= 0 || !dl::IsInstanceOf(btn, "TSpeedButton")) return;
    if (dl::At<int>(btn, E.fGroupOff) == 0) return;  // not a toggle button: nothing was flipped
    bool down = dl::At<uint8_t>(btn, E.fDownOff) != 0;
    bool ok = false;
    SafeCall(E.setDown, U(btn), down ? 0 : 1, 0, 0, nullptr, &ok);
}

// The missile panel in a live battle (engine level 9). "Fire mode" (rgMissileDoctrine) is disabled: the game applies it
// to PlayerSide's ships only (the salvo routine 0xda7ccc), and the live automatic fire runs with the simulation
// PlayerSide 2, the computer's rules for both sides; Manual fire's own buttons choose one, half or all. "Fire at will"
// (cbFireAtWill) is this player's order for their own side (FireAtWillClicked) and shows that side's setting
// (ShowFireAtWill); without the feature it is disabled too (0.4.0). Each locked control gets a hint once per battle
// screen; the end of the live battle puts both back (UnlockMissilePanel).
static const wchar_t* kHintFireMode = L"Fire mode: the game applies it to one side only, so in a live battle the "
                                      L"computer sets the salvo size of both sides' automatic fire alike. Manual fire "
                                      L"chooses one, half or all.";
static const wchar_t* kHintFireAtWill = L"Fire at will: your own missile ships fire on their own (the computer's rules), "
                                        L"from the next minute. The other player sets theirs.";

static void SetCtlEnabled(void* c, bool on) {
    if (!c || E.setEnabledSlot <= 0 || E.fCtlEnabled <= 0 || (dl::At<uint8_t>(c, E.fCtlEnabled) != 0) == on) return;
    void** vt = *(void***)c;
    bool ok = false;
    SafeCall(vt[E.setEnabledSlot / 4], U(c), on ? 1 : 0, 0, 0, nullptr, &ok);
}

// A control's hint (nullptr: the form's own state again: no hint, ShowHint from the parent).
static void SetCtlHint(void* c, const wchar_t* text) {
    if (!c || !g_ustrAsg || E.fCtlHint <= 0 || E.fCtlShowHint <= 0 || E.fCtlParentShowHint <= 0) return;
    if (text) {
        dl::ConstUStr s(text);
        StrAsg(&dl::At<uint32_t>(c, E.fCtlHint), s.ptr());  // (_UStrAsg copies a literal)
        dl::At<uint8_t>(c, E.fCtlShowHint) = 1;
        dl::At<uint8_t>(c, E.fCtlParentShowHint) = 0;
    } else {
        StrAsg(&dl::At<uint32_t>(c, E.fCtlHint), 0);
        void* parent = E.fCtlParent > 0 ? dl::At<void*>(c, E.fCtlParent) : nullptr;
        dl::At<uint8_t>(c, E.fCtlParentShowHint) = 1;
        dl::At<uint8_t>(c, E.fCtlShowHint) = parent ? dl::At<uint8_t>(parent, E.fCtlShowHint) : 0;
    }
}

// The radio group and its buttons (the hint shows over a disabled button too).
static void SetGroupHint(void* rg, const wchar_t* text) {
    SetCtlHint(rg, text);
    void* buttons = rg && E.fRgButtons > 0 ? dl::At<void*>(rg, E.fRgButtons) : nullptr;
    for (int i = 0; i < dl::ListCount(buttons) && i < 16; i++) SetCtlHint(dl::ListItem(buttons, i), text);
}

static void LockMissilePanel() {
    void* m = BattleForm();
    if (!m || E.setEnabledSlot <= 0) return;
    void* fire = E.fCbFireAtWill > 0 ? dl::At<void*>(m, E.fCbFireAtWill) : nullptr;
    void* mode = E.fRgDoctrine > 0 ? dl::At<void*>(m, E.fRgDoctrine) : nullptr;
    SetCtlEnabled(mode, false);
    SetCtlEnabled(fire, g_featFireAtWill);
    // (a new battle screen has no hints on them: set once per screen)
    if (mode && E.fCtlHint > 0 && !dl::At<void*>(mode, E.fCtlHint)) SetGroupHint(mode, kHintFireMode);
    if (fire && g_featFireAtWill && E.fCtlHint > 0 && !dl::At<void*>(fire, E.fCtlHint)) SetCtlHint(fire, kHintFireAtWill);
}

// The live battle is over on this PC (a single-player battle from here, if it goes on): the panel as the game has it.
static void UnlockMissilePanel() {
    void* m = BattleForm();
    if (!m || E.setEnabledSlot <= 0) return;
    void* fire = E.fCbFireAtWill > 0 ? dl::At<void*>(m, E.fCbFireAtWill) : nullptr;
    void* mode = E.fRgDoctrine > 0 ? dl::At<void*>(m, E.fRgDoctrine) : nullptr;
    __try {
        SetCtlEnabled(mode, true);
        SetCtlEnabled(fire, true);
        if (mode && E.fCtlHint > 0 && dl::At<void*>(mode, E.fCtlHint)) SetGroupHint(mode, nullptr);
        if (fire && E.fCtlHint > 0 && dl::At<void*>(fire, E.fCtlHint)) SetCtlHint(fire, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lockstep: exception while putting the missile panel back");
    }
}

// The Fire at will box shows this player's own side's setting: the pending order, else the agreed value (g_faw holds
// that outside the simulation). FState and the window are set directly: no OnClick.
static void ShowFireAtWill() {
    void* m = BattleForm();
    if (!m || !g_featFireAtWill || !S.on || (S.mySide != 0 && S.mySide != 1) || E.fCbFireAtWill <= 0) return;
    void* cb = dl::At<void*>(m, E.fCbFireAtWill);
    if (!cb) return;
    uint8_t want = g_faw[S.mySide] ? 1 : 0;  // cbChecked / cbUnchecked
    dl::At<uint8_t>(cb, E.fCbState) = want;
    HWND h = game::FormHwnd(cb);
    if (h) SendMessageW(h, BM_SETCHECK, want ? BST_CHECKED : BST_UNCHECKED, 0);
}

// A click on Fire at will (its OnClick, after VCL toggled the box): this player's order for their own side. The guard
// makes it an order batch at its next look (the next step, a deferred action or the release); the simulation reads the
// agreed value only (LS_MissileFire).
static void FireAtWillClicked(void* cb) {
    bool on = dl::At<uint8_t>(cb, E.fCbState) == 1;
    std::string err;
    if (!g_featFireAtWill)
        err = "Fire at will is not available in this live battle.";
    else if (!CanOrderNow(err)) {
    } else if (S.mySide != 0 && S.mySide != 1)
        err = "This PC has no side in the battle.";
    if (!err.empty()) {
        ShowFireAtWill();  // (the setting in effect)
        RunOnMainThread([err] { Chat(err); });
        return;
    }
    if ((g_faw[S.mySide] != 0) == on) return;
    g_faw[S.mySide] = on ? 1 : 0;
    Log("lockstep: Fire at will %s for side %d (this player's order)", on ? "on" : "off", S.mySide);
    RunOnMainThread([on] {
        Chat(on ? "Fire at will on: your missile ships fire on their own from the next minute."
                : "Fire at will off: from the next minute your missile ships fire only the salvoes you order (Manual "
                  "fire).");
    });
}

// The force orders' click handlers (live battle: deferred actions instead of the game's own routine).

static void CloseForm(void* f) {
    bool ok = false;
    if (f && E.mClose && game::FormVisible(f)) SafeCall(E.mClose, U(f), 0, 0, 0, nullptr, &ok);
}

// TfrmMain.Changeforce2Click (Self = the battle screen, Sender = the force's item of the ship tree's "Change force" menu,
// Tag = its index in the forces list): the game's own question about the selected division, then a deferred change of
// force (the handler itself doesn't run).
static void ChangeForceClick(void* m, void* sender) {
    void* div = m ? dl::At<void*>(m, E.fSelDiv) : nullptr;
    if (!div || !sender || !dl::IsInstanceOf(sender, "TMenuItem")) return;  // (the game: nothing without a division)
    void* force = dl::ListItem(List(E.forcesVar), dl::At<int>(sender, E.fTag));
    std::string err;
    if (!CheckChangeForce(div, force, err)) {
        Chat(err);
        return;
    }
    World w;
    Walk(w);
    std::string dk = w.keyOf.count(div) ? w.keyOf[div] : "", fk = w.keyOf.count(force) ? w.keyOf[force] : "";
    dl::ConstUStr text(E.chforceText + ObjName(force) + E.chforceTail);
    uint32_t help[1] = {0};
    int epoch = S.epoch;
    bool ok = false;
    int r = (int)SafeCall(E.messageDlg, text.ptr(), 3, (uint32_t)E.chforceButtons, 1, help, &ok);  // mtConfirmation
    if (!ok || r != 6) return;  // mrYes
    // A minute may have run while the question was open: find both again by their keys.
    if (!S.on || S.epoch != epoch) {
        Chat("The battle was loaded again meanwhile; give the order again.");
        return;
    }
    World w2;
    Walk(w2);
    void* div2 = w2.objOf.count(dk) ? w2.objOf[dk] : nullptr;
    void* force2 = w2.objOf.count(fk) ? w2.objOf[fk] : nullptr;
    if (!QueueChangeForce(div2, force2, err)) Chat(err);
}

// The division chosen in the Select lead dialog (lbLeadShips.Items.Objects[ItemIndex], as its OK button reads it).
static void* ChosenLead(void* dlg) {
    void* lb = dl::At<void*>(dlg, E.fLbLeadShips);
    if (!lb) return nullptr;
    bool ok = false;
    int idx = (int)SafeCall((*(void***)lb)[E.lbIndexSlot / 4], U(lb), 0, 0, 0, nullptr, &ok);
    void* items = ok && idx >= 0 ? dl::At<void*>(lb, E.fLbItems) : nullptr;
    if (!items) return nullptr;
    void* obj = (void*)(uintptr_t)SafeCall((*(void***)items)[E.getObjectSlot / 4], U(items), (uint32_t)idx, 0, 0, nullptr, &ok);
    return ok && IsDivision(obj) ? obj : nullptr;
}

// TdlgSelectLead.bnOKClick (Self = the dialog): Force orders' Change lead / Battle turn away become a deferred action for
// the force the dialogs are open for (SelectedForce) and the chosen division; then the dialogs close as after the game's
// routine. 0 = the game's handler runs (no division chosen: its own message; no selection mode: nothing).
static int SelectLeadOK(void* self) {
    void* m = BattleForm();
    if (!m || !self) return 0;
    bool newLead = dl::At<uint8_t>(m, E.fSelNewLead) != 0, turn = !newLead && dl::At<uint8_t>(m, E.fSelTurnAway) != 0;
    if (!newLead && !turn) return 0;
    void* div = ChosenLead(self);
    if (!div) return 0;
    std::string err;
    if (!QueueForceLead(turn, dl::At<void*>(m, E.fSelForce), div, err)) {
        Chat(err);  // the dialog stays open (Cancel closes it)
        return 1;
    }
    dl::At<uint8_t>(m, turn ? E.fSelTurnAway : E.fSelNewLead) = 0;
    CloseForm(self);
    CloseForm(DialogForm("TdlgForceOrders"));
    return 1;
}

static int Unavailable(const char* what) {
    std::string w = what;
    RunOnMainThread([w] { Chat(w + " is not available in a live battle (this version)."); });
    return 1;
}

static std::string Who(int side);

// TdlgSearch.Execute(Self, aForce, Editor) in a live battle: the routine air ops dialog opens for this player's own forces
// only. The battle screen opens it for the force of the selected map flag (or tree node), which can be the other
// side's: its settings are the other player's, and the guard would put an edit there back at the next step anyway.
static bool SearchDialogRefused(void* force, uint32_t editor) {
    if ((editor & 0xFF) != 0 || !IsForce(force) || E.fForceSide < 0) return false;  // (the scenario editor's call)
    int side = dl::At<int>(force, E.fForceSide);
    if (side == S.mySide || (side != 0 && side != 1)) return false;
    RunOnMainThread([side] {
        Chat("That force is " + Who(side) + "'s: you can set the routine air operations of your own forces only.");
    });
    return true;
}

// Click handlers: 1 = handled here (the game's handler doesn't run). sender and arg3 are the handler's EDX and ECX.
static int LS_BlockImpl(int idx, void* self, void* sender, uint32_t arg3) {
    // Outside a live battle the game's own Save runs; for a battle that can be rejoined its marker follows the new
    // battle save once the handler has saved (phase 5b).
    if (!S.on && idx >= 0 && idx < kBlockers && g_blockers[idx].policy == B_SAVE && g_rj.on && g_rj.slot > 0)
        RunOnMainThread(RefreshMarkerAfterSave);
    if (!S.on || idx < 0 || idx >= kBlockers) return 0;
    const Blocker& b = g_blockers[idx];
    void* m = BattleForm();
    switch (b.policy) {
        case B_PREFS: {  // (see g_blockers) the dialog runs here; afterwards the battle preferences are this battle's again
            static bool open = false;
            if (open || !b.orig || !E.prefs) return 0;
            std::string agreed = PrefsHex();  // (A: its own, as sent; B: A's)
            open = true;
            bool ok = false;
            SafeCall(b.orig, U(self), U(sender), 0, 0, nullptr, &ok);
            open = false;
            std::string now = PrefsHex();
            if (S.on && now != agreed) {
                // (not kept for after the battle: opened from the battle screen the dialog writes some of them back
                // other than it got them, e.g. RealismSetting -1 after a plain Ok in a test)
                std::string d = PrefsDiff(agreed, now);
                SetPrefsHex(agreed);
                Log("lockstep: the Preferences dialog changed battle preferences the simulation uses (%s): this battle's "
                    "values put back", d.c_str());
                RunOnMainThread([] {
                    Chat("Battle preferences that the live battle's simulation uses (Realism, AI control, floatplane "
                         "launch, ...) can't change until it ends: change them after the battle. Display preferences "
                         "change at once.");
                });
            }
            return 1;
        }
        case B_PAUSE:
            RunOnMainThread([] { ClockRequest("pause", 0); });
            return 1;
        case B_END:
            if (m && dl::At<uint8_t>(m, E.fEndGame)) return 0;
            RunOnMainThread([] { ClockRequest("end", 0); });
            return 1;
        case B_DETACH:
            if (BetweenMinutes()) return 0;
            RunOnMainThread([] { Chat("Pause the battle first: ships can be detached between minutes."); });
            return 1;
        case B_TORPS:  // the division report's Fire torpedoes, the torpedo dialog's Fire: between minutes (FireMount's hook)
            if (!g_featTorp) return Unavailable("Manual torpedo fire");
            if (!BetweenMinutes()) {
                RunOnMainThread([] { Chat("Pause the battle first: torpedoes are fired between minutes."); });
                return 1;
            }
            if (EffectiveAuto(S.mySide)) {  // (as Manual fire: the computer fires the ships of a side on auto-run)
                RunOnMainThread([] { Chat(kAutoRunFire); });
                return 1;
            }
            // The torpedo dialog in its missile mode (opened by the division report's Fire missiles, which stays blocked)
            // would fire missiles itself.
            if (self && E.fTorpMissiles > 0 && dl::IsInstanceOf(self, "TdlgTorpTargets") &&
                dl::At<uint8_t>(self, E.fTorpMissiles))
                return Unavailable("Manual missile fire");
            return 0;
        case B_SPEED: {
            int want = ReadSpeedCombo(self, m);
            if (want >= -3 && want <= 5) RunOnMainThread([want] { ClockRequest("speed", want); });
            return 1;
        }
        case B_ARROW: {  // UpDown1Click: one step from the speed in use, by the game's own rule, as a request
            if (!m) return 1;
            int base = S.isA ? (S.wantSpeed != INT_MIN ? S.wantSpeed : CurSpeed())
                             : (S.clockSpeed != INT_MIN ? S.clockSpeed : CurSpeed());
            bool engaged = E.fEngaged >= 0 && dl::At<uint8_t>(m, E.fEngaged) != 0;
            int want = base;
            if ((arg3 & 0xff) == 0) {  // btNext: up, at most Fast while ships are in contact
                if ((base < 5 && !engaged) || base < 2) want = base + 1;
            } else if ((arg3 & 0xff) == 1 && base > -3) {  // btPrev: down
                want = base - 1;
            }
            if (want != base) RunOnMainThread([want] { ClockRequest("speed", want); });
            return 1;
        }
        case B_FASTEST: {
            // The button flipped before this handler ran, and the game's speed governor reads it at every minute
            // boundary: put it back as both PCs have it (up, unless A's clock turned it on), or this PC's minutes could
            // run at another speed than the other PC's; then ask A's clock for the new state (the next release applies
            // it on both PCs).
            if (!FastestShared()) {  // the button's routines were not found: it stays up (as in 0.3.2)
                bool ok = false;
                if (m && E.releaseFastest) SafeCall(E.releaseFastest, U(m), 0, 0, 0, nullptr, &ok);
                RunOnMainThread([] { Chat("Fastest available is not available in this live battle; use the speed box."); });
                return 1;
            }
            if (!m) return 1;
            bool want = FastestDown(m);
            SetFastestButton(m, S.fastest != 0);
            if (want != (S.fastest != 0)) RunOnMainThread([want] { ClockRequest("fastest", want ? 1 : 0); });
            return 1;
        }
        case B_AUTORUN: {  // this player's ships to the computer and back: the game's question, then a deferred action
            if (!g_featAutoRun) {
                RunOnMainThread([] { Chat("Auto-run is not available in a live battle; use Run (until a break)."); });
                return 1;
            }
            std::string err;
            if (CanOrderNow(err))
                AutoRunClick();
            else
                RunOnMainThread([err] { Chat(err); });
            return 1;  // (LS_Block puts the button back: it shows the side's auto-run, which changes at the release)
        }
        case B_SAVE:  // a clock request: A saves the battle at a minute boundary (ATryCheckpoint)
            RunOnMainThread([] { ClockRequest("save", 0); });
            return 1;
        case B_CHFORCE: {  // the ship tree's "Change force" > <force>: a deferred action
            if (!g_featChforce) return Unavailable("Change force");
            std::string err;
            if (CanOrderNow(err)) ChangeForceClick(m, sender);
            else RunOnMainThread([err] { Chat(err); });
            return 1;
        }
        case B_LEADBTN:  // Force orders' Change lead / Battle turn away: only the Select lead dialog (its OK is deferred)
            return g_featLead ? 0 : Unavailable(b.name);
        case B_AIROPS:  // Routine air ops / Search pattern: the dialog's commit writes the force's air settings (orders)
            return g_featAirOps ? 0 : Unavailable("Routine air operations");
        case B_LANDCAP:  // the division report's land-based CAP button: this player's own request (the display swap)
            return g_featLandCap ? 0 : Unavailable("Land-based CAP");
        case B_SEARCHDLG:  // TdlgSearch.Execute: not for the other side's forces
            return SearchDialogRefused(sender, arg3) ? 1 : 0;
        case B_STRIKE:  // "Order an airstrike": a pause request instead of the handler's PauseGame, then its dialog
            if (!g_featStrike) return Unavailable("The strike dialog");
            if (S.runLeft > 0 || S.minuteRunning) RunOnMainThread([] { ClockRequest("pause", 0); });
            OpenStrikeDialog(nullptr);
            StrikeAutoNote();
            return 1;
        case B_STRIKEDLG:  // the reports list's "Launch strike": the game's handler (Execute for the report)
            if (!g_featStrike) return Unavailable("The strike dialog");
            StrikeAutoNote();
            return 0;
        case B_STRIKEBTN: {  // the strike dialog's Launch, Ready, Stand down: between minutes, its list up to date
            std::string err;
            if (StrikeReady(err)) return 0;
            RunOnMainThread([err] { Chat(err); });
            return 1;
        }
        case B_SELECTLEAD: {
            if (!g_featLead) return Unavailable("Change lead");
            std::string err;
            if (CanOrderNow(err)) return SelectLeadOK(self);
            RunOnMainThread([err] { Chat(err); });
            return 1;
        }
        case B_FIREATWILL:  // cbShowBaseFlagsClick (Base flags' and Fire at will's handler): the game's repaint runs
            if (m && sender && E.fCbFireAtWill > 0 && E.fCbState > 0 && sender == dl::At<void*>(m, E.fCbFireAtWill))
                FireAtWillClicked(sender);
            return 0;
        case B_SALVO: {  // Manual fire: the missile dialog (modal) between minutes; while the clock runs a pause request
            if (!g_featMissile) return Unavailable("Manual missile fire");
            std::string err;
            if (!CanOrderNow(err)) {
                RunOnMainThread([err] { Chat(err); });
                return 1;
            }
            bool timer = m && TimerOn(m, E.fRunTimer);  // (the handler's PauseGame := 1 needs it: it never runs here)
            if (!BetweenMinutes() || timer || !S.atBoundary || Minute() != S.boundary || S.hasPendingRel) {
                bool running = S.runLeft > 0 || S.minuteRunning || timer;
                RunOnMainThread([running] {
                    if (running) ClockRequest("pause", 0);
                    Chat(running ? "Pausing the battle: open Manual fire again when it is paused (missiles are fired "
                                   "between minutes)."
                                 : "The next minute is starting: open Manual fire again when the battle is paused.");
                });
                return 1;
            }
            if (EffectiveAuto(S.mySide)) {
                RunOnMainThread([] { Chat(kAutoRunFire); });
                return 1;
            }
            return 0;  // the game's handler: the dialog's lists, then ShowModal
        }
        case B_SALVOFIRE: {  // the missile dialog's Fire one / half / all
            if (!g_featMissile) return Unavailable("Manual missile fire");
            std::string err;
            if (!MissileOrdersOpen(err)) {
                RunOnMainThread([err] { Chat(err); });
                return 1;
            }
            int un = self ? UncheckMissileSubs(self) : 0;
            if (un > 0) {
                S.mslSubsUnchecked += un;
                Log("lockstep: %d missile submarine(s) unchecked in the missile dialog (no submarine salvoes in a live "
                    "battle)", un);
                RunOnMainThread([] {
                    Chat("Missile submarines can't fire salvoes in a live battle (the game fires them for the first side "
                         "only): they were left out.");
                });
            }
            return 0;  // FireMissiles: its FireMissileMount calls become this player's salvo orders
        }
        default:
            return Unavailable(b.name);
    }
}

extern "C" int __stdcall LS_Block(int idx, void* self, void* sender, uint32_t arg3) {
    int r = LS_BlockImpl(idx, self, sender, arg3);
    if (r && g_blockers[idx].policy != B_FASTEST) PutToggleBack(sender);  // B_FASTEST puts its button back itself
    return r;
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
    kv["fastest"] = std::to_string(S.wantFastest != INT_MIN ? S.wantFastest : S.fastest);
    kv["e0"] = S.endWant[0] ? "1" : "0";
    kv["e1"] = S.endWant[1] ? "1" : "0";
    kv["a0"] = S.autoRun[0] ? "1" : "0";  // auto-run per side as applied here (B shows it; it applies it at releases)
    kv["a1"] = S.autoRun[1] ? "1" : "0";
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

// A: a game text of the running minute (a box or a picture message): the first player's lines into this chat, each
// player's lines kept as the reason of a break at the minute's end (OnBoundary sends the second player theirs).
static void OnGameText(const std::wstring& text, bool proposal, int siteSide) {
    if (!S.isA || text.empty()) return;  // (B: A says the breaks and the game's questions)
    std::string per[2];
    GameTextFor(text, siteSide, per);
    for (int p = 0; p < 2; p++)
        if (!per[p].empty()) g_minuteText[p] = per[p];
    std::string t = ChatSafe(text);
    if (proposal) {
        S.proposal = true;
        RunOnMainThread([t] { ChatBoth("The game asks: \"" + t + "\" Press End on both sides to end the battle."); });
    } else if (!per[0].empty()) {
        std::string a = per[0];
        RunOnMainThread([a] { Chat("Battle: " + a); });
    } else {
        Log("lockstep: the game's text is the other player's (not shown here): %s", per[1].c_str());
    }
}

static uint32_t NewSeed() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    uint32_t s = (uint32_t)q.LowPart ^ (GetTickCount() << 7) ^ (uint32_t)(S.bid * 2654435761u);
    return s ? s : 1;
}

static void Cleanup() {
    // A: the live battle is over on this PC (a single-player battle from here, if it goes on): ships the computer
    // commanded for this player (auto-run) are theirs again; the game's own auto-run button works from now on.
    bool handBack = S.isA && S.autoRun[0] && !g_loading && BattleForm();
    SetRng(0, 0);
    FreezeView();  // keep this PC's view (as in a single-player battle)
    if (!g_loading) UnlockMissilePanel();  // (Fire at will keeps this player's setting: the game's own rule from here)
    // This PC's own battle preferences as before the battle (B ran with A's; on either PC the Preferences dialog's
    // changes of them during the battle were put back)
    if (!S.savedPrefs.empty()) {
        std::string d = PrefsDiff(PrefsHex(), S.savedPrefs);
        if (SetPrefsHex(S.savedPrefs) && !d.empty()) Log("lockstep: this PC's own battle preferences again (%s)", d.c_str());
    }
    S.savedPrefs.clear();
    if (!S.savedTypes.empty()) {  // B: this PC's own ship-type flags again (engine level 17)
        if (S.savedTypes != TypeFlagsHex() && SetTypeFlagsHex(S.savedTypes))
            Log("lockstep: this PC's own ship-type flags again (%s)", S.savedTypes.c_str());
        S.savedTypes.clear();
    }
    if (g_cwSaved) {  // B: back to this PC's own x87 control word
        SetFpuCw(g_cwSaved);
        g_cwSaved = 0;
    }
    if (x87::Soft() || x87::TotalCalls()) {  // engine level 16: this CPU's own transcendental routines again
        Log("lockstep: x87 routines since the load: %s (%s); control word put back %d times", x87::Counts().c_str(),
            x87::Soft() ? "software" : "this CPU's", S.cwFixes);
        x87::SetSoft(false);
        x87::ResetCounts();
    }
    datafiles::SetPhase("after the battle");
    ClearOrders("the live battle ended");
    S.tokens = 0;
    S.minuteRunning = false;
    S.minuteBroken = false;
    S.atBoundary = false;
    S.stalledSince = 0;
    S.hasPendingRel = false;
    S.loadQueued = false;
    S.pendingStart.clear();
    S.runLeft = 0;
    S.autoRun[0] = S.autoRun[1] = false;
    S.autoClock = false;
    ResetSimSides();
    int leads = 0;
    if (handBack && TryRun([&] { leads = AutoOff(0); })) {
        SetAutoButton(false);
        void* m = BattleForm();
        if (m && !dl::At<uint8_t>(m, E.fEndGame)) Chat(AutoOffText(leads));
    }
}

// B: the box the battle screen's close shows (TfrmMain.FormClose: ShowMessagePos(frmBuildCamp.tmpBattleString), which
// HandleBattleAftermath wrote at the end of the battle from the first side's point of view: "<its adjective> <level>
// victory" or "Enemy <level> victory", the first side's prestige and morale lines, "Own VP: <side 0>", "Enemy VP: <side
// 1>"). The second player reads their own side's result: the victory from their side, the points swapped, without the
// first side's campaign effects (the battle counts in the host's campaign). Display text only: nothing else reads it.
static void OwnSideResult() {
    static int fText = dl::Field("TfrmBuildCamp", "tmpBattleString"), fName2 = dl::Field("TBuilderNation", "Name2");
    void* bc = game::BuildCamp();
    if (!bc || fText <= 0 || !g_ustrAsg) return;
    uint32_t* slot = &dl::At<uint32_t>(bc, fText);
    std::wstring t = *slot ? dl::ReadUStr((void*)(uintptr_t)*slot) : L"";
    size_t own = t.find(L"Own VP: "), enemy = t.find(L"Enemy VP: ");
    if (own == std::wstring::npos || enemy == std::wstring::npos || enemy < own) {
        Log("lockstep: the result box is not the usual one: left as the game wrote it ('%s')", W2U(t).c_str());
        return;
    }
    auto value = [&](size_t at, size_t len) {
        size_t e = t.find_first_of(L"\r\n", at + len);
        return t.substr(at + len, (e == std::wstring::npos ? t.size() : e) - at - len);
    };
    std::wstring vp0 = value(own, 8), vp1 = value(enemy, 10), first = t.substr(0, t.find_first_of(L"\r\n"));
    auto adjective = [&](void* n) { return n && fName2 > 0 ? dl::ReadUStr(dl::At<void*>(n, fName2)) : std::wstring(); };
    std::wstring a0 = adjective(game::PlayerNation()), a1 = S.opp >= 0 ? adjective(game::Nation(S.opp)) : L"";
    std::wstring head = first;  // ("The battle was a draw.")
    if (first.rfind(L"Enemy ", 0) == 0)
        head = (a1.empty() ? std::wstring(L"Your") : a1) + first.substr(5);  // "Enemy major victory" -> "<own> major victory"
    else if (!a0.empty() && first.rfind(a0 + L" ", 0) == 0)
        head = L"Enemy" + first.substr(a0.size());  // "<first side's> major victory" -> "Enemy major victory"
    std::wstring mine = head + L"\r\rOwn VP: " + vp1 + L"\rEnemy VP: " + vp0;
    dl::ConstUStr u(mine);
    StrAsg(slot, u.ptr());  // (_UStrAsg copies a literal)
    Log("lockstep: the result box shows this player's side: '%s' (the game's: '%s')", W2U(mine).c_str(), W2U(t).c_str());
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
    // The battle is over: its last snapshot is no longer a battle to resume as a live battle (phase 5b).
    if (normal && S.isA && S.campaignSlot) DeleteMarker(game::CurrentSlot(), "the live battle is over");
    if (normal && !S.isA) OwnSideResult();
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

// A: the other player is gone; side 1 goes to the game's AI and this battle goes on as a single-player battle. The
// battle stays rejoinable while it goes on (RememberRejoin): the player gets side 1 back when they return.
static void DoDrop() {
    S.dropPending = false;
    std::string why = S.dropWhy;
    if (!S.on) return;
    bool loaded = S.st == St::Running || S.st == St::WaitReady;
    bool rejoinFailed = S.rejoin && !S.everRunning;  // a rejoin that did not get to run
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
    RememberRejoin(why);
    // What will actually happen: a rejoin needs the battle remembered here and the player's PC on this live-battle level.
    bool rejoinable = g_rj.on && g_rj.bid == S.bid;
    bool otherLevel = S.peerLevel > 0 && S.peerLevel != MP_LS_LEVEL;
    if (rejoinFailed)
        Chat(S.peerName + " could not rejoin the battle (" + why + "): the computer keeps commanding " + Who(1) +
             "; the battle goes on here.");
    else if (g_rjTry.want)  // (the player came back before this minute ended: Rejoin waited for the drop)
        Chat(S.peerName + " left the live battle (" + why + ") and is back: the battle starts again with " +
             g_rjTry.peerName + " commanding " + Who(1) + ".");
    else if (!rejoinable)
        Chat(S.peerName + " left the live battle (" + why + "). The AI commands " + Who(1) +
             " to the end of this battle; it goes on here.");
    else if (otherLevel)
        Chat(S.peerName + " left the live battle (" + why + "). The AI commands " + Who(1) +
             " now; the battle goes on here. " + S.peerName + " can take their ships back only with a mod that plays "
             "live battles at level " + std::to_string(MP_LS_LEVEL) + " (theirs: " + std::to_string(S.peerLevel) + ").");
    else
        Chat(S.peerName + " left the live battle (" + why + "). The AI commands " + Who(1) +
             " now; the battle goes on here. If " + S.peerName + " joins the session again during this battle, " +
             S.peerName + " gets their ships back.");
    if (g_hooks.notice)
        g_hooks.notice(S.peerName + (rejoinFailed ? " could not rejoin the battle: " : " left the battle: ") + why);
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
    if (ok) RememberRejoin(err);
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
        DropPeer("the battles could not be kept in sync" + (S.readyDiag.empty() ? std::string() : " (" + S.readyDiag + ")"));
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
    // Both PCs stand at this boundary (A releases no minute before B's report of it is verified): each writes its own
    // report now, B on this message (sent before the resync's start).
    DesyncReport(S.myHashN, S.peerHash);
    KV kv;
    kv["t"] = "desync";
    kv["n"] = std::to_string(S.myHashN);
    kv["h"] = S.myHash;
    SendMsg(kv);
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

// A: auto-run of both sides started or ended at this release. Both on: A's clock runs the battle to its end at the
// fastest speed (breaks don't stop it, see OnBoundary; the game's "end the scenario?" is answered Yes, see LS_MsgDlg).
// One of them takes back command: the run stops after this minute and the clock is as it was before.
static void AutoClockChanged() {
    if (!S.isA) return;
    if (BothAuto()) {
        S.autoClock = true;
        S.preAutoFastest = S.wantFastest != INT_MIN ? S.wantFastest : S.fastest;
        S.preAutoSpeed = S.wantSpeed != INT_MIN ? S.wantSpeed : CurSpeed();
        S.runLeft = INT_MAX / 2;
        S.runWhat = "auto-run";
        if (FastestShared()) S.wantFastest = 1;
        S.wantSpeed = 5;  // as the Fastest available button's click (without the button: Ultra fast)
        ChatBoth("Both players' ships are on auto-run: the battle runs to its end at the fastest speed. Press Auto-run to "
                 "take your ships back.");
    } else if (S.autoClock) {
        S.autoClock = false;
        S.runLeft = 0;
        if (FastestShared()) S.wantFastest = S.preAutoFastest;
        if (S.preAutoSpeed >= -3 && S.preAutoSpeed <= 5) S.wantSpeed = S.preAutoSpeed;
        ChatBoth("A player took back command: the battle pauses after this minute.");
    }
    SendInfo();
}

// Both PCs: the minute released by A (n = the boundary it starts from).
static void ApplyRelease(const KV& kv) {
    void* m = BattleForm();
    int n = KVInt(kv, "n", -1), s0 = KVInt(kv, "s0"), s1 = KVInt(kv, "s1"), speed = KVInt(kv, "speed", CurSpeed());
    int fastest = KVInt(kv, "fastest", -1);
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
    if (!S.isA) S.clockSpeed = speed;
    if (!S.isA && fastest >= 0) S.clockFastest = fastest;
    CheckFpu("release");
    int withActions = 0;
    bool bothAuto = BothAuto();
    S.inAction = true;
    SharedBegin();
    bool endSoon = KVInt(kv, "endsoon") != 0 && E.scenLenVar;
    bool ok = TryRun([&] {
        withActions = ApplyBatches(s0, s1, n);
        if (S.forceDesync) CorruptForTest();
        if (fastest >= 0) ApplyFastest(fastest != 0);
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
    RunAfterRelease();  // the deferred actions' display work (ship tree, dialogs, their player's chat lines)
    if (BothAuto() != bothAuto) AutoClockChanged();  // (A's clock, from the next release)
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
    CloseTorpDialog();  // its firing solutions are for this boundary
    g_minuteText[0].clear();  // (a break names only the texts of its own minute)
    g_minuteText[1].clear();
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

// A: Save (either player's button, `ls save`) in the host's own live battle: a crash checkpoint. The battle as it is at
// this boundary goes into the host's campaign slot as the start and every resync write it (SaveSnapshot); nothing is
// sent or loaded, and the pending orders stay pending. If the host's game stops, loading that slot resumes the battle
// the game's own way, with the computer commanding side 1 (see OnCampaignLoaded).
static void ATryCheckpoint() {
    if (S.saveWant < 0 || !S.isA || !S.on || S.st != St::Running || S.minuteRunning || S.inAction || g_loading ||
        g_resyncPending || !S.atBoundary || Minute() != S.boundary)
        return;
    void* m = BattleForm();
    if (!m || dl::At<uint8_t>(m, E.fEndGame) || game::ModalOpen() || g_nFrames > 0) return;
    S.saveWant = -1;
    DetectEdits("save");  // the player's latest edits become orders (pending) before the agreed state is put in place
    RestoreAgreed();
    std::string err;
    bool ok = SaveSnapshot(err);
    ReapplyOverlay();
    if (ok)
        ChatBoth("Battle saved (minute " + std::to_string(S.boundary) +
                 ") in the host's campaign slot: if the host's game stops, loading that slot resumes the battle, and "
                 "it waits for " + S.peerName + " to join the host's session again (or goes on against the computer).");
    else
        ChatBoth("The battle could not be saved (" + err + ").");
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
    kv["fastest"] = std::to_string(S.wantFastest != INT_MIN ? S.wantFastest : S.fastest);
    kv["end"] = end ? "1" : "0";
    kv["prop"] = S.proposal ? "1" : "0";  // the game proposed the end: both end with the game's own end routine
    if (S.endSoon) {
        kv["endsoon"] = "1";
        S.endSoon = false;
    }
    S.wantSpeed = INT_MIN;
    S.wantFastest = INT_MIN;
    if (!end && S.runLeft > 0 && S.runLeft < INT_MAX / 4) S.runLeft--;
    kv["left"] = std::to_string(S.runLeft);
    SendMsg(kv);
    S.waitSince = 0;
    ApplyRelease(kv);
}

static void ATryRelease() {
    ATryCheckpoint();  // a Save waits for a boundary like this one
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
    LockMissilePanel();  // the game's missile panel refresh could have enabled them again
    S.boundary = Minute();
    S.brokenMinutes = 0;
    CheckFpu("minute");
    // The hash covers the agreed state: this PC's pending orders (the overlay) are taken out while it is computed, and so
    // is anything display code wrote into the other side's order fields since the minute ended (GetForceFlagship, which
    // the Force orders dialog and the change-force menu call, can rewrite ForceLeadDivision, which the hash now covers).
    DetectEdits("boundary");
    RestoreAgreed();
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
    FlightBoundary();  // (DESYNC forensics: this minute's draws and the objects' bytes, the simulation's set)
    CanonEnd();
    ReapplyOverlay();
    // The strike dialog's list may hold formations this minute freed: fill it again now (its list number writes are
    // this player's orders, sent with the next batch).
    if (!ended && !RefreshStrikeList()) Log("lockstep: the strike buttons wait until its list is filled again");
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
        if (BothAuto()) {  // both players' ships on auto-run: the clock runs on to the end
            Log("lockstep: a break at minute %d ignored (both sides on auto-run): %s", S.boundary, g_minuteText[0].c_str());
        } else {
            S.runLeft = 0;
            // Each player reads the reason of their own: the minute's last game text about their side or the whole
            // battle, else "an event" (the other side's sighting, damage or question stays theirs).
            auto line = [](const std::string& t) {
                return "The battle paused (" + (t.empty() ? std::string("an event") : t.substr(0, 100)) + ").";
            };
            Chat(line(g_minuteText[0]));
            SendInfo(line(g_minuteText[1]));
        }
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
            // The speed box releases Fastest available (as the game's own does).
            bool release = FastestShared() && (S.wantFastest != INT_MIN ? S.wantFastest : S.fastest) != 0;
            if (release) S.wantFastest = 0;
            ChatBoth(who + " set the game speed to " + std::to_string(n) + (release ? " (Fastest available off)" : "") +
                     " (from the next minute).");
        }
    } else if (what == "fastest") {
        bool on = n != 0, cur = (S.wantFastest != INT_MIN ? S.wantFastest : S.fastest) != 0;
        if (!FastestShared()) {
            if (side == 0) Chat("Fastest available is not available in this live battle; use the speed box.");
        } else if (on != cur) {
            S.wantFastest = on ? 1 : 0;
            S.wantSpeed = on ? 5 : 2;  // as the button's own click: down = speed 5 ("Ultra fast"), up = 2 ("Fast")
            ChatBoth(who + " turned Fastest available " + (on ? "on" : "off") + " (from the next minute).");
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
    } else if (what == "save") {
        if (!S.campaignSlot) {  // a battle between two joined players: this PC holds it in a temporary battle slot
            ChatBoth(who + " pressed Save: the host keeps battles between two players, so nothing is saved.");
        } else if (S.saveWant < 0) {
            S.saveWant = side;
            if (S.minuteRunning) ChatBoth(who + " pressed Save: the battle is saved when this minute ends.");
        }
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
static int g_testClockHours = 0;  // TEST ONLY (`ls testclock <hours>`): A's next snapshot dates the battle that much later
// TEST ONLY (`ls testpmt <all-ships index>`): A's next start or resync message carries a pending heavy SSM target for that
// ship (the location of the nearest enemy ship afloat), as if the agreed state had kept one at that boundary (the
// simulation consumes a pending target in the ship's next gun step, so a real one rarely lasts until a boundary).
static int g_testPmtShip = -1;

// The test target for g_testPmtShip: "<index>:<24 hex digits>;" or "" (no such ship or no enemy afloat).
static std::string TestPendingTarget(int i) {
    static int fLoc = dl::Field("TFightingShip", "ShipLocation");
    void* ships = List(E.allShipsVar);
    void* sp = dl::ListItem(ships, i);
    if (!IsFightingShip(sp) || fLoc < 0 || E.fPendingMsl <= 0) return "";
    int side = dl::At<int>(sp, E.fShipSide);
    const int* me = (const int*)((uint8_t*)sp + fLoc);
    void* best = nullptr;
    double bd = 0;
    for (int k = 0; k < dl::ListCount(ships); k++) {
        void* o = dl::ListItem(ships, k);
        if (!IsFightingShip(o) || dl::At<int>(o, E.fShipSide) == side || dl::At<int>(o, E.fShipSide) > 1 ||
            (E.fSunk > 0 && dl::At<uint8_t>(o, E.fSunk)))
            continue;
        const int* p = (const int*)((uint8_t*)o + fLoc);
        double d = (double)(p[0] - me[0]) * (p[0] - me[0]) + (double)(p[1] - me[1]) * (p[1] - me[1]);
        if (!best || d < bd) best = o, bd = d;
    }
    if (!best) return "";
    uint8_t pt[12] = {};
    memcpy(pt, (uint8_t*)best + fLoc, 8);
    return std::to_string(i) + ":" + Hex(pt, 12) + ";";
}
static bool StartTransfer(bool resync, std::string& err) {
    if (!BattleForm()) {
        err = "no battle";
        return false;
    }
    int speed = S.wantSpeed != INT_MIN ? S.wantSpeed : CurSpeed();
    S.wantSpeed = INT_MIN;
    int fastest = S.wantFastest != INT_MIN ? S.wantFastest : S.fastest;
    S.wantFastest = INT_MIN;
    if (S.st == St::Running) RestoreAgreed();  // pending orders are dropped with a new snapshot
    ClearOrders(S.st == St::Running ? "the battle was synchronised again" : nullptr);
    std::string ai1 = SideAiFlags(1);  // (the agreed flags: put back after the loads, see LoadSnapshot)
    std::string ai0 = SideAiFlags(0);  // (engine level 13: side 0's too)
    // A rejoin: the computer commanded side 1 since the drop; it is the player's again (LoadSnapshot's first load makes
    // it human on both PCs; the flags say so for any later use).
    if (S.rejoin && !S.everRunning)
        for (char& c : ai1)
            if (c == '1') c = '0';
    std::string ncap = NightCapFlags();  // (the agreed night CAP: the battle save drops it)
    std::string faw = FireAtWillText(), pmt = PendingTargetsText();  // (agreed: session state, a field the save drops)
    if (g_testPmtShip >= 0) {  // TEST ONLY (`ls testpmt`)
        std::string t = TestPendingTarget(g_testPmtShip);
        Log("lockstep: TEST: the snapshot carries a pending missile target for ship %d: %s", g_testPmtShip,
            t.empty() ? "none (no such ship or no enemy afloat)" : t.c_str());
        pmt += t;
        g_testPmtShip = -1;
    }
    S.epoch++;
    S.seed = NewSeed();
    S.endWant[0] = S.endWant[1] = false;
    S.proposal = false;
    S.peerDoneN = S.verifiedN = S.myHashN = -1;
    S.peerReady = false;
    S.peerReadyHash.clear();
    S.mySeq = S.peerSeq = 0;
    if (g_testClockHours && E.clockVar) {  // TEST ONLY (`ls testclock`): the battle dated later, for both PCs' loads
        *E.clockVar += g_testClockHours / 24.0;
        Log("lockstep: TEST: the battle clock moved by %d hours before the snapshot: t=%s", g_testClockHours,
            ClockText().c_str());
        g_testClockHours = 0;
    }
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
    h["fastest"] = std::to_string(fastest);   // clock state the battle save doesn't keep
    h["lv"] = std::to_string(AnnouncedLsLevel());  // B refuses another live-battle engine (0.3.x sends none: level 1)
    h["prefs"] = PrefsHex();
    h["st"] = TypeFlagsHex();  // engine level 17: the ship-type flags the identification reads (B uses A's)
    h["gd"] = W2U(GameDir());
    h["cw"] = std::to_string(S.cw0);
    // Engine level 16: the transcendental maths in software on both PCs (x87.cpp; TEST: RTW3MP_TEST_X87SOFT=0 keeps each
    // PC's own), A's CPU vendor (diagnostics), and the game data files' hash (B refuses a battle whose files differ).
    S.softMath = x87::Installed() && (g_softWanted >= 0 ? g_softWanted != 0 : EnvStr("RTW3MP_TEST_X87SOFT") != "0");
    h["sm"] = S.softMath ? "1" : "0";
    h["cpu"] = x87::CpuVendor();
    if (!resync && datafiles::Ready(3000)) h["dh"] = datafiles::Combined();
    h["s0"] = S.sideNames[0];
    h["s1"] = S.sideNames[1];
    h["from"] = S.myName;
    h["opp"] = std::to_string(S.opp);
    h["ai1"] = ai1;  // side 1's computer control (the game's resume sets all of it; a resync puts it back)
    h["ai0"] = ai0;  // side 0's (the resume gives the divisions of side 0's computer forces to the computer)
    h["ncap"] = ncap;  // night CAP per force (the battle save doesn't keep it; both PCs put it back after the load)
    h["faw"] = faw;    // Fire at will per side (session state; both PCs apply it after the load)
    h["pmt"] = pmt;    // the ships' pending missile targets (the battle save doesn't keep them)
    std::string autoState = AutoText();
    h["auto"] = autoState;  // auto-run per side: session state, applied after the loads (LoadSnapshot)
    std::string pa = S.preAutoF[0] + "/" + S.preAutoF[1];
    h["pa"] = pa;  // the forces' computer control before auto-run per side (Auto-run off gives it back)
    h["autostrike"] = S.autoStrike ? "1" : "0";  // the computer flies both sides' carrier strikes (AirstrikeSimSide)
    if (S.rejoin && !S.everRunning) {  // a dropped player is back (B: its chat; a restart if B still has the battle)
        h["rj"] = "1";
        h["min"] = std::to_string(Minute());
        // (phase 5b: the battle came from the host's save after its game was restarted; only B's chat line reads it)
        if (S.resumeStart) h["hr"] = "1";
    }
    S.st = St::Loading;
    S.loadStart = GetTickCount();
    S.lastPeerMsg = GetTickCount();
    SendMsg(h, body);
    Log("lockstep: battle %d epoch %d sent (%u files, %u bytes, seed %u, speed %d, fastest %d, auto-run %s%s)", S.bid,
        S.epoch, (unsigned)b.files.size(), (unsigned)body.size(), S.seed, speed, fastest, autoState.c_str(),
        S.rejoin && !S.everRunning ? ", rejoin" : "");
    x87::SetSoft(S.softMath);
    x87::ResetCounts();
    datafiles::SetPhase("battle load");  // (TEST ONLY probe: RTW3MP_TEST_FILELOG)
    if (!LoadSnapshot(slot, S.seed, speed, fastest, !resync && !S.everRunning, ai0, ai1, autoState, ncap, faw, pmt, pa,
                      err))
        return false;
    S.st = St::WaitReady;
    CanonBegin();
    S.myReadyHash = HashNow();
    S.myReadyParts = ReadyParts();
    CanonEnd();
    Log("lockstep: loaded (x87 routines during the load: %s; transcendental maths: %s)", x87::Counts().c_str(),
        S.softMath ? "software" : "this CPU's");
    x87::ResetCounts();
    datafiles::SetPhase("battle");
    S.lastPeerMsg = GetTickCount();
    if (S.peerAI) S.peerReady = true;
    ACheckReady();
    return true;
}

static void ACheckReady() {
    if (!S.isA || S.st != St::WaitReady || (!S.peerReady && !S.peerAI)) return;
    if (!S.peerAI && S.peerReadyHash != S.myReadyHash) {
        S.readyDiag = ReadyDiag();
        std::string what = "the loaded battles differ" + (S.readyDiag.empty() ? std::string() : ": " + S.readyDiag);
        Log("lockstep: the loaded battles differ: here %s, %s %s (%s)", S.myReadyHash.c_str(), S.peerName.c_str(),
            S.peerReadyHash.c_str(), S.readyDiag.empty() ? "no parts from the other PC" : S.readyDiag.c_str());
        // (engine level 15) Both PCs stand at the loaded boundary: each writes its DESYNC report (the loaded objects'
        // bytes and the state), B on this message (sent before the resync's start), so two players' reports show what
        // their loads made differently.
        DesyncReport(S.boundary, S.peerReadyHash, "the loaded battles differ");
        KV kv;
        kv["t"] = "desync";
        kv["n"] = std::to_string(S.boundary);
        kv["h"] = S.myReadyHash;
        kv["why"] = "ready";
        kv["diag"] = S.readyDiag;  // engine level 16: B's notice says what differed
        SendMsg(kv);
        if (g_hooks.notice) g_hooks.notice("Live battle: " + what + " (loading again)");
        RequestResync(what);
        return;
    }
    S.readyDiag.clear();
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
    if (first && S.rejoin) {
        Log("lockstep: rejoin: %s commands side 1 again (battle %d epoch %d, minute %d)%s", S.peerName.c_str(), S.bid,
            S.epoch, S.boundary, S.resumeStart ? " (resumed from the host's save)" : "");
        if (g_rj.on && g_rj.bid == S.bid) g_rj = RejoinMemo();  // (live again; a new drop remembers it again)
        ChatBoth(S.peerName + " is back in the live battle at minute " + std::to_string(S.boundary) + " and commands " +
                 Who(1) + " again. Run, Pause, the speed" + (FastestShared() ? ", Fastest available" : "") +
                 " and End act for both players again; both players' orders apply from the next minute.");
        if (S.rejoinAuto)
            Chat(Who(0) + " stays on auto-run, as before " + S.peerName +
                 " came back (Auto-run takes the ships back).");
    } else if (first) {
        ChatBoth("The live battle starts: " + Who(0) + " against " + Who(1) + ". Run, Pause, the speed" +
                 (FastestShared() ? ", Fastest available" : "") +
                 " and End act for both players; both players' orders apply from the next minute.");
    } else {
        ChatBoth("The battle is synchronised again (minute " + std::to_string(S.boundary) + ").");
    }
    if (first && dl::ListCount(List(E.airVar)) > 0)
        ChatBoth(S.autoStrike ? std::string("Air strikes: the computer flies both sides' carrier and airfield strikes (the "
                                            "host's option)") +
                                    (g_featStrike ? "; \"Order an airstrike\" still launches your own strikes while the "
                                                    "battle is paused, besides the computer's."
                                                  : ".")
                              : "Air strikes: launch your carriers' strikes with \"Order an airstrike\" while the battle is "
                                "paused; the computer flies only the airfields' strikes.");
    if (first && MissileShipsInBattle())
        ChatBoth(std::string("Missiles: ") +
                 (g_featFireAtWill ? "\"Fire at will\" on the missile panel is each player's own (on at the start: your "
                                     "missile ships fire on their own, under the computer's rules); "
                                   : "both sides' missile ships fire on their own, under the computer's rules; ") +
                 (g_featMissile ? "\"Manual fire\" works while the battle is paused; " : "manual salvoes are not available; ") +
                 "\"Fire mode\" is the computer's for both sides (the game applies it to one side only).");
}

// ---- B ----

static void TryLoadQueued() {
    if (!S.loadQueued || S.inAction || S.minuteRunning || g_loading) return;
    std::string wait;
    if (!CleanStack(&wait)) return;
    // a dialog or event of the player's game (not for a restart of the battle this PC shows: its screen is open)
    if (S.firstLoad && !S.restartLoad && g_hooks.loadReady && !g_hooks.loadReady()) return;
    S.loadQueued = false;
    std::string err, body;
    body.swap(S.pendingStart);
    Bundle b;
    if (!b.Unpack(body))
        err = "damaged battle files";
    else if (WriteBattleFiles(b, S.slot, err)) {
        FixFlagFiles(S.slot, S.gameDirA);
        // A's battle preferences (engine level 15: only those the simulation uses) for the load and the battle
        if ((int)S.prefsA.size() == PrefsHexLen()) {
            if (S.firstLoad) {
                std::string d = PrefsDiff(S.savedPrefs, S.prefsA);
                Log("lockstep: battle preferences: %s's for the load and the simulation (%s); this PC's own come back "
                    "after the battle", S.peerName.c_str(), d.empty() ? "the same as this PC's" : d.c_str());
            }
            SetPrefsHex(S.prefsA);
        } else if (!S.prefsA.empty()) {
            Log("lockstep: %s's battle preferences have another layout (%d characters): this PC's own stay",
                S.peerName.c_str(), (int)S.prefsA.size());
        }
        // Engine level 17: A's ship-type flags (the identification reads them; a joined player's game has 0 for all)
        if (g_typeFlagsSync && !S.typesA.empty()) {
            std::string mine = TypeFlagsHex();
            if (!SetTypeFlagsHex(S.typesA))
                Log("lockstep: %s's ship-type flags '%s' not applied (not found here): this PC's own stay",
                    S.peerName.c_str(), S.typesA.c_str());
            else if (mine != S.typesA)
                Log("lockstep: ship-type flags %s here, %s's %s: the load and the battle use %s's", mine.c_str(),
                    S.peerName.c_str(), S.typesA.c_str(), S.peerName.c_str());
        } else if (!g_typeFlagsSync) {
            Log("lockstep: TEST: this PC keeps its own ship-type flags %s (%s's: %s)", TypeFlagsHex().c_str(),
                S.peerName.c_str(), S.typesA.c_str());
        }
        // A's x87 control word also for the load (until level 14 only after it: a PC whose DLLs had changed its own
        // computed the loaded battle with another precision or rounding)
        if (S.cw0 && g_cwBeforeLoad) {
            unsigned mine = FpuCw();
            if (mine != S.cw0)
                Log("lockstep: x87 control word %04x here, %s's %04x: the load and the battle use %s's", mine,
                    S.peerName.c_str(), S.cw0, S.peerName.c_str());
            SetFpuCw(S.cw0);
        } else if (!g_cwBeforeLoad && g_cwSaved) {  // TEST ONLY (`ls testcwload 0`): as level 14's loads
            SetFpuCw(g_cwSaved);
            Log("lockstep: TEST: this load runs with this PC's own x87 control word %04x (the battle's: %04x)", g_cwSaved,
                S.cw0);
        }
        // Engine level 16: the transcendental maths in software on both PCs from the load on (x87.cpp)
        x87::SetSoft(S.softMath);
        Log("lockstep: CPU %s here, %s at %s; transcendental maths: %s", x87::CpuVendor().c_str(),
            S.peerCpu.empty() ? "?" : S.peerCpu.c_str(), S.peerName.c_str(),
            S.softMath ? "software (the same on both PCs)" : "each PC's own x87");
        x87::ResetCounts();
        datafiles::SetPhase("battle load");
        LoadSnapshot(S.slot, S.seed, S.startSpeed, S.startFastest, S.firstLoad, S.startAi0, S.startAi1, S.startAuto,
                     S.startNcap, S.startFaw, S.startPmt, S.startPa, err);
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
    S.restartLoad = false;
    S.st = St::WaitReady;
    KV kv;
    kv["t"] = "ready";
    kv["lv"] = std::to_string(AnnouncedLsLevel());  // A drops a PC with another live-battle engine (0.3.x: none)
    CanonBegin();
    S.myReadyHash = HashNow();  // (also for this PC's report when the loaded battles differ)
    kv["g"] = ReadyParts();     // engine level 16: what differs, in A's diagnostics (ReadyDiag)
    CanonEnd();
    kv["h"] = S.myReadyHash;
    kv["n"] = std::to_string(S.boundary);
    kv["cpu"] = x87::CpuVendor();
    char cw[8];
    snprintf(cw, sizeof(cw), "%04x", S.ownCw);
    kv["cw"] = cw;  // this PC's own x87 control word (the battle runs with A's)
    Log("lockstep: loaded (x87 routines during the load: %s; control word put back %d times)", x87::Counts().c_str(),
        S.cwFixes);
    x87::ResetCounts();
    datafiles::SetPhase("battle");
    SendMsg(kv);
}

// Two PCs whose live-battle engines differ, in player language (the session's notices add the mod versions).
static std::string LevelMismatchText(const std::string& who, int theirs) {
    return who + "'s mod plays live battles at level " + std::to_string(theirs) + ", this PC's at level " +
           std::to_string(MP_LS_LEVEL) + " (a live battle needs the same level on both PCs)";
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
        kv["lv"] = std::to_string(AnnouncedLsLevel());  // (A: whether a rejoin could ever work)
        Log("lockstep: battle %d refused: %s", bid, why.c_str());
        if (replyTo) replyTo(PackHB(KVEncode(kv), ""));
    };
    if (S.on && (S.isA || S.bid != bid)) {
        refuse("busy with another battle");
        return;
    }
    if (S.on && epoch <= S.epoch) return;  // stale
    int lv = KVInt(h, "lv", 1);  // a 0.3.x PC sends no level: live-battle engine level 1
    if (lv != MP_LS_LEVEL) {
        std::string from = KVStr(h, "from", "the other player");
        std::string why = LevelMismatchText(from, lv);
        if (S.on) {  // (a resync of a running battle can't change it)
            KV kv;
            kv["t"] = "abort";
            kv["why"] = why;
            kv["lv"] = std::to_string(AnnouncedLsLevel());
            SendMsg(kv);
            AbortB(why);
            return;
        }
        refuse(why);
        Chat("The live battle with " + from + " could not start: " + why + ".");
        if (g_hooks.notice) g_hooks.notice("A live battle could not start: " + from + "'s mod plays live battles at another level");
        return;
    }
    // Engine level 16: the game data files (datafiles.cpp). A's start carries their hash; a different set here would load
    // and play the battle differently (endless resyncs): refused with this PC's list, A names the differing files to
    // both players (`datadiff`) and fights the battle against the computer.
    std::string dh = KVStr(h, "dh");
    if (!resync) {
        bool ready = datafiles::Ready(3000);
        if (dh.empty() || !ready)
            Log("lockstep: battle %d: the game data files were not compared (%s)", bid,
                dh.empty() ? "no hash from the other PC" : "this PC's hash is not ready");
        else if (dh == datafiles::Combined())
            Log("lockstep: battle %d: the game data files are the same on both PCs (%d files, %s)", bid,
                datafiles::FileCount(), dh.c_str());
    }
    if (!resync && !dh.empty() && datafiles::Ready() && dh != datafiles::Combined()) {
        KV kv;
        kv["t"] = "abort";
        kv["bid"] = std::to_string(bid);
        kv["epoch"] = std::to_string(epoch);
        kv["why"] = "the game data files differ";
        kv["lv"] = std::to_string(AnnouncedLsLevel());
        kv["dm"] = datafiles::Encoded();
        Log("lockstep: battle %d refused: the game data files differ (%s here, %s at %s; %d files)", bid,
            datafiles::Combined().c_str(), dh.c_str(), KVStr(h, "from", "the other player").c_str(), datafiles::FileCount());
        if (replyTo) replyTo(PackHB(KVEncode(kv), ""));
        return;
    }
    bool rejoin = KVInt(h, "rj") != 0 && !resync;  // A starts the battle again after it dropped this player (phase 5a)
    bool restart = S.on && !resync;                // ... while this PC still has the battle (see below)
    std::string rejoinMin = KVStr(h, "min", "?");
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
        if (why.empty() && KVInt(h, "sm") && !x87::Installed())  // (engine level 16; the same game build finds them)
            why = "this PC can't use the battle's CPU-independent maths (the game's x87 routines were not found)";
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
        ResetSimSides();
        if (!g_cwSaved) g_cwSaved = FpuCw();  // this PC's own x87 control word (A's is used during the battle)
        S.ownCw = g_cwSaved;
        S.peerName = KVStr(h, "from", "the other player");
        S.sideNames[0] = KVStr(h, "s0");
        S.sideNames[1] = KVStr(h, "s1");
        S.savedPrefs = PrefsHex();
        S.savedTypes = TypeFlagsHex();
        S.firstLoad = true;
        if (g_hooks.slotMode) g_hooks.slotMode(true);
        if (rejoin) {
            bool hostRestart = KVInt(h, "hr") != 0;  // (phase 5b: A's battle comes from its save after a restart)
            Log("lockstep: rejoin: battle %d epoch %d accepted (minute %s)%s", bid, epoch, rejoinMin.c_str(),
                hostRestart ? " (the host's game was restarted)" : "");
            if (hostRestart)
                Chat("Back in the live battle: " + Who(0) + " against " + Who(1) + ", minute " + rejoinMin + ". " +
                     S.peerName + "'s game was restarted; the battle goes on from its last save, and you command " +
                     Who(1) + " again. Loading...");
            else
                Chat("Back in the live battle: " + Who(0) + " against " + Who(1) + ", minute " + rejoinMin + ". " +
                     S.peerName + " gives you " + Who(1) + " back (the computer commanded it meanwhile). Loading...");
            if (g_hooks.notice) g_hooks.notice("Live battle: you command " + Who(1) + " again");
        } else {
            Chat(S.peerName + " starts a live battle: " + Who(0) + " against " + Who(1) + ". You command " + Who(1) +
                 ". Loading...");
            if (g_hooks.notice) g_hooks.notice("Live battle: you command " + Who(1));
        }
    } else if (restart) {
        // A started the battle again while this PC still has its copy (A dropped this player: it was silent, or its
        // leave hasn't happened yet): a new start of the same battle, as for a PC that comes fresh (side 1 human again
        // after the load), keeping this player's own preferences and x87 control word saved at the first start.
        Log("lockstep: rejoin: battle %d starts again here (this PC still had it at epoch %d, %s; now epoch %d)", bid,
            S.epoch, S.leavePending ? "leaving" : StName(S.st), epoch);
        S.leavePending = false;
        S.leaveWhy.clear();
        S.finSince = 0;
        S.firstLoad = true;
        S.restartLoad = BattleVisible();  // (loadReady waits for no battle screen: this one is replaced by the load)
        S.send = replyTo;
        S.runLeft = 0;
        S.brokenMinutes = 0;
        S.endWant[0] = S.endWant[1] = false;
        S.peerName = KVStr(h, "from", S.peerName);
        Chat("Back in the live battle at minute " + rejoinMin + ": " + S.peerName + " gives you " + Who(1) +
             " back. Loading...");
        if (g_hooks.notice) g_hooks.notice("Live battle: you command " + Who(1) + " again");
    } else {
        Log("lockstep: battle %d resync (epoch %d)", bid, epoch);
    }
    S.epoch = epoch;
    S.peerLevel = lv;
    S.seed = (uint32_t)strtoul(KVStr(h, "seed", "1").c_str(), nullptr, 10);
    S.startSpeed = KVInt(h, "speed", 1);
    S.clockSpeed = S.startSpeed;
    S.startFastest = KVInt(h, "fastest", 0) != 0 ? 1 : 0;
    S.clockFastest = S.startFastest;
    S.prefsA = KVStr(h, "prefs");
    S.typesA = KVStr(h, "st");
    S.gameDirA = KVStr(h, "gd");
    S.cw0 = (unsigned)KVInt(h, "cw");
    S.softMath = KVInt(h, "sm") != 0 && x87::Installed();  // engine level 16: A decides; B can (checked above)
    S.peerCpu = KVStr(h, "cpu");
    S.startAi1 = KVStr(h, "ai1");
    S.startAi0 = KVStr(h, "ai0");
    S.startAuto = KVStr(h, "auto", "00");  // auto-run per side (applied after the load)
    S.startNcap = KVStr(h, "ncap");        // night CAP per force (applied after the load)
    S.startFaw = KVStr(h, "faw", "11");    // Fire at will per side (applied after the load)
    S.startPmt = KVStr(h, "pmt");          // the ships' pending missile targets (applied after the load)
    S.startPa = KVStr(h, "pa", "/");       // the forces' computer control before auto-run (applied after the load)
    S.opp = KVInt(h, "opp", S.opp);        // A's opponent nation (this player's) in the campaign of A's battle files
    S.autoStrike = KVInt(h, "autostrike") != 0;  // the battle's carrier-strike rule, as A decided it
    S.pendingStart.swap(body);
    S.loadQueued = true;
    S.st = St::Loading;
    S.loadStart = S.lastPeerMsg = GetTickCount();
    S.hasPendingRel = false;
    S.atBoundary = false;
    ClearOrders(resync ? "the battle was synchronised again" : restart ? "the battle was started again" : nullptr);
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
    if (t == "rejoin") {  // the host: the second player of a relayed battle this PC dropped them from is back
        std::string err, who = KVStr(h, "p2", "the other player");
        if (!Rejoin(bid, who, replyTo, err)) {
            Log("lockstep: rejoin of battle %d by %s refused: %s", bid, who.c_str(), err.c_str());
            KV kv;
            kv["t"] = "norejoin";
            kv["bid"] = std::to_string(bid);
            kv["epoch"] = "0";
            kv["why"] = err;
            kv["why2"] = RejoinWhyForPlayer(err);  // (the host's notice to the returning player)
            if (replyTo) replyTo(PackHB(KVEncode(kv), ""));
        }
        return;
    }
    if (t == "peerlost" && !S.on && g_rjTry.want && bid == g_rj.bid) {  // the returning player left again (relayed)
        CancelRejoin(KVStr(h, "why", "the other player disconnected"));
        return;
    }
    if (t == "datadiff" && (!S.on || bid == S.bid) && !S.isA) {  // engine level 16: A names the files (B refused)
        std::string why = KVStr(h, "why", "your game data files differ");
        Log("lockstep: battle %d: %s", bid, why.c_str());
        Chat("The live battle could not start: " + why + ". The battle is fought against the computer.");
        if (g_hooks.notice) g_hooks.notice("No live battle: " + why);
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
        if (S.isA) {
            int lv = KVInt(h, "lv");
            if (lv > 0) S.peerLevel = lv;  // (B's level: whether a rejoin could ever work)
            std::string dm = KVStr(h, "dm");
            if (!dm.empty()) {  // engine level 16: B's game data files differ (B's list: name it to both players)
                auto names = datafiles::Differ(dm);
                std::string list = names.empty() ? "?" : datafiles::NamesText(names);
                Log("lockstep: %s's game data files differ from this PC's: %d files: %s", S.peerName.c_str(),
                    (int)names.size(), datafiles::NamesText(names, 40).c_str());
                KV kv;
                kv["t"] = "datadiff";
                kv["why"] = "your game data files differ from " + S.myName + "'s: " + list;
                SendMsg(kv);
                why = "the game data files differ: " + list;
            }
            DropPeer(lv > 0 && lv != MP_LS_LEVEL ? LevelMismatchText(S.peerName, lv) : S.peerName + ": " + why);
        } else {
            AbortB(t == "drop" ? "the AI commands your side now (" + why + ")" : why);
        }
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
            S.peerLevel = KVInt(h, "lv", 1);  // a 0.3.x PC sends no level: live-battle engine level 1
            if (S.peerLevel != MP_LS_LEVEL) {
                DropPeer(LevelMismatchText(S.peerName, S.peerLevel));
                return;
            }
            S.peerReady = true;
            S.peerReadyHash = KVStr(h, "h");
            S.peerReadyParts = KVStr(h, "g");  // engine level 16: B's parts, CPU vendor and own control word
            S.peerCpu = KVStr(h, "cpu");
            S.peerOwnCw = (unsigned)strtoul(KVStr(h, "cw").c_str(), nullptr, 16);
            if (!S.peerCpu.empty() && S.peerCpu != x87::CpuVendor())
                Log("lockstep: CPU %s here, %s at %s (transcendental maths: %s)", x87::CpuVendor().c_str(),
                    S.peerCpu.c_str(), S.peerName.c_str(), S.softMath ? "software on both PCs" : "each PC's own");
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
        S.clockSpeed = KVInt(h, "speed", S.clockSpeed);
        S.clockFastest = KVInt(h, "fastest", S.clockFastest);
        S.endWant[0] = KVInt(h, "e0") != 0;
        S.endWant[1] = KVInt(h, "e1") != 0;
        S.clockAuto[0] = KVInt(h, "a0", S.clockAuto[0]);
        S.clockAuto[1] = KVInt(h, "a1", S.clockAuto[1]);
        std::string msg = KVStr(h, "msg");
        if (!msg.empty()) Chat(msg);
    } else if (t == "desync") {  // A found a DESYNC at this boundary (level 15: or that the loaded battles differ): this
                                 // PC's report too (DesyncReport)
        bool ready = KVStr(h, "why") == "ready";
        if (S.st == St::Running || (ready && S.st == St::WaitReady))
            DesyncReport(KVInt(h, "n", -1), KVStr(h, "h"), ready ? "the loaded battles differ" : nullptr);
        std::string diag = KVStr(h, "diag");  // engine level 16: what differed (A's ReadyDiag)
        if (ready) {
            S.readyDiag = diag;
            Log("lockstep: %s: the loaded battles differ (%s)", S.peerName.c_str(), diag.empty() ? "?" : diag.c_str());
            if (g_hooks.notice)
                g_hooks.notice("Live battle: the loaded battles differ" + (diag.empty() ? std::string() : ": " + diag) +
                               " (loading again)");
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The session API

void SetHooks(const Hooks& h) {
    g_hooks = h;
}

bool Available() { return Resolve() && E.sessionOk; }
bool Prepare() {
    datafiles::StartScan();  // (engine level 16: the game data files' hash for the live battles' start)
    return Available();
}
bool Active() { return S.on && S.st != St::Ended; }
bool Busy() { return (S.on && S.st != St::Ended) || S.leavePending; }
bool IsAuthority() { return S.on && S.isA; }
int BattleId() { return S.on ? S.bid : -1; }
bool IsHolding() { return S.on && S.isA && S.st == St::Holding; }

// The campaign date. Year, Month and Day are Integer unit variables (the game writes them as dwords; EncodeDate's Word
// parameters read their low words). The game's resume path (the `.sac` reader) writes the battle's own day and month
// into them, while the campaign's month advance starts from a separate TDateTime, CampaignDate, that each load sets
// from the save's [General] date before the battle save is read and that nothing in a battle writes.
// - Engine level 10: after every live load (LoadCampaignGuarded: the start, a resync, a rejoin on both PCs, A's
//   recovery) and after the host's resume of a live battle, both PCs write the campaign's own date back into the
//   variables, decoded from CampaignDate (KeepCampaignDate), so the battle runs with the campaign's month and day as a
//   battle that is never reloaded does; the state hash covers them.
// - Since 0.3.2, now belts and braces: A records the date when its battle opens, every snapshot saves that date (else
//   one resync made the drift permanent, and a battle dated in the next month skipped a month), and the results, the
//   month and a battle slot's close (which runs no month) put it back.
static int32_t* g_dateVar[3] = {};  // year, month, day
static int32_t g_campDate[3] = {};
static bool g_campDateSet = false;
static double* g_campDateTdt = nullptr;  // CampaignDate (a TDateTime: days since 1899-12-30)

static bool ResolveDate() {
    static bool tried = false;
    if (tried) return g_dateVar[0] != nullptr;
    tried = true;
    // LoadBuildCampaign: mov ecx, [Day]; movzx ecx, word [ecx]; mov edx, [Month]; movzx edx, word [edx]; mov eax, [Year];
    // movzx eax, word [eax]; call EncodeDate; fstp qword ptr [CampaignDate]
    uint8_t* hit = Find(dl::Method("TfrmBuildCamp", "LoadBuildCampaign"), 0x3000,
                        {0x8B, 0x0D, -1, -1, -1, -1, 0x0F, 0xB7, 0x09, 0x8B, 0x15, -1, -1, -1, -1, 0x0F, 0xB7, 0x12,
                         0xA1, -1, -1, -1, -1, 0x0F, 0xB7, 0x00, 0xE8, -1, -1, -1, -1, 0xDD, 0x1D});
    if (hit) {
        g_dateVar[2] = (int32_t*)VarFromCell(hit + 2);
        g_dateVar[1] = (int32_t*)VarFromCell(hit + 11);
        g_dateVar[0] = (int32_t*)VarFromCell(hit + 19);
        uint32_t tdt = *(uint32_t*)(hit + 33);  // (the fstp's operand: the variable itself, not a cell)
        if (InImage(tdt)) g_campDateTdt = (double*)(uintptr_t)tdt;
    }
    if (!g_dateVar[0] || !g_dateVar[1] || !g_dateVar[2]) g_dateVar[0] = g_dateVar[1] = g_dateVar[2] = nullptr;
    if (!g_dateVar[0]) g_campDateTdt = nullptr;
    if (g_dateVar[0])
        Log("lockstep: campaign date at %p/%p/%p: %d-%02d-%02d; CampaignDate at %p%s", g_dateVar[0], g_dateVar[1],
            g_dateVar[2], *g_dateVar[0], *g_dateVar[1], *g_dateVar[2], g_campDateTdt,
            g_campDateTdt ? "" : " NOT found (live loads keep the battle save's day and month)");
    else
        Log("lockstep: campaign date NOT found (a resync may move the host's campaign date)");
    return g_dateVar[0] != nullptr;
}

static bool CampaignDateNow(int32_t ymd[3]) {
    if (!ResolveDate()) return false;
    for (int i = 0; i < 3; i++) ymd[i] = *g_dateVar[i];
    return true;
}

// CampaignDate as Year, Month and Day, as SysUtils.DecodeDate gives them (the proleptic Gregorian calendar).
static bool CampaignDateOf(int32_t ymd[3]) {
    if (!ResolveDate() || !g_campDateTdt) return false;
    double v = *g_campDateTdt;
    if (!(v > -36522.0 && v < 73051.0)) return false;  // 1800-01-01 .. 2099-12-31 (and not a NaN)
    // Whole days (Delphi's DateTimeToTimeStamp: milliseconds rounded, then div), days since 1970-01-01, then
    // H. Hinnant's civil_from_days (shifted to 0000-03-01; positive for these years).
    long long z = llround(v * 86400000.0) / 86400000 - 25569 + 719468;
    long long era = z / 146097, doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    ymd[2] = (int32_t)(doy - (153 * mp + 2) / 5 + 1);
    ymd[1] = (int32_t)(mp < 10 ? mp + 3 : mp - 9);
    ymd[0] = (int32_t)(yoe + era * 400 + (ymd[1] <= 2 ? 1 : 0));
    return true;
}

// The campaign's own date back into the variables after a live load: CampaignDate, which LoadBuildCampaign built from
// this save's [General] date before the `.sac` reader moved the day and month (the host's snapshots save the campaign's
// own date there). Never from the battle clock: its date is the battle's own (before 1900 even encoded as Year + 100).
// The clock and CampaignDate stay as loaded. Both PCs load the same files, so both keep the same date.
static bool KeepCampaignDate(const char* what) {
    int32_t d[3], was[3];
    if (!CampaignDateNow(was) || !CampaignDateOf(d)) {
        Log("lockstep: WARNING %s: the campaign date could not be kept (%s): the battle save's day and month stay", what,
            !g_dateVar[0] ? "the variables were not found" : !g_campDateTdt ? "CampaignDate was not found"
                                                                           : "CampaignDate is out of range");
        return false;
    }
    for (int i = 0; i < 3; i++) *g_dateVar[i] = d[i];
    bool moved = was[0] != d[0] || was[1] != d[1] || was[2] != d[2];
    Log("lockstep: %s: campaign date %d-%02d-%02d %s %d-%02d-%02d)", what, d[0], d[1], d[2],
        moved ? "kept (the battle save said" : "(as the battle save:", was[0], was[1], was[2]);
    if (was[0] != d[0])  // (the `.sac` reader never writes the year)
        Log("lockstep: WARNING %s: the year was %d before, CampaignDate's is %d", what, was[0], d[0]);
    if (g_campDateSet && (g_campDate[0] != d[0] || g_campDate[1] != d[1] || g_campDate[2] != d[2]))
        Log("lockstep: WARNING the campaign date recorded on this PC is %d-%02d-%02d", g_campDate[0], g_campDate[1],
            g_campDate[2]);
    return true;
}

static void RecordCampaignDate() {
    if (!ResolveDate()) return;
    if (g_campDateSet) {  // this PC's last live battle neither closed nor ran a month (tests start one over another)
        Log("lockstep: campaign date %d-%02d-%02d kept from the battle before (the variables had %d-%02d-%02d)",
            g_campDate[0], g_campDate[1], g_campDate[2], *g_dateVar[0], *g_dateVar[1], *g_dateVar[2]);
        for (int i = 0; i < 3; i++) *g_dateVar[i] = g_campDate[i];
        return;
    }
    for (int i = 0; i < 3; i++) g_campDate[i] = *g_dateVar[i];
    g_campDateSet = true;
    Log("lockstep: campaign date recorded: %d-%02d-%02d", g_campDate[0], g_campDate[1], g_campDate[2]);
}

// The host's resume of its live battle (ResumeFromMarker): the game's own load, outside a live battle, so the record
// was dropped (OnCampaignLoading) and the variables hold the battle save's day and month. The date comes back from the
// loaded save, and it is the record the rejoin's snapshots swap in.
static void RecordLoadedCampaignDate() {
    if (!KeepCampaignDate("resume")) return;
    for (int i = 0; i < 3; i++) g_campDate[i] = *g_dateVar[i];
    g_campDateSet = true;
    Log("lockstep: resume: campaign date recorded from the loaded save: %d-%02d-%02d", g_campDate[0], g_campDate[1],
        g_campDate[2]);
}

// The campaign's month runs (DoStrategicTurn): it goes on from the campaign's own date. The variables get that date back
// first (the steps before the month advance read them), then the record is done.
void OnMonthRuns() {
    if (!g_campDateSet) return;
    if (g_dateVar[0])
        for (int i = 0; i < 3; i++) *g_dateVar[i] = g_campDate[i];
    Log("lockstep: the month runs: campaign date %d-%02d-%02d put back, the record is dropped", g_campDate[0],
        g_campDate[1], g_campDate[2]);
    g_campDateSet = false;
}

// The battle's result screen opens (the battle is over): the aftermath (victory points, prestige, the month's record row)
// and the rest of the month read the campaign's own date, as in a battle that was never reloaded.
static void OnResultsEnter() {
    if (!g_campDateSet || !g_dateVar[0]) return;
    if (*g_dateVar[0] == g_campDate[0] && *g_dateVar[1] == g_campDate[1] && *g_dateVar[2] == g_campDate[2]) return;
    Log("lockstep: the battle's results: campaign date %d-%02d-%02d put back (the battle's date was %d-%02d-%02d)",
        g_campDate[0], g_campDate[1], g_campDate[2], *g_dateVar[0], *g_dateVar[1], *g_dateVar[2]);
    for (int i = 0; i < 3; i++) *g_dateVar[i] = g_campDate[i];
}

// A campaign load that isn't this PC's live battle's own: the recorded date belongs to another state.
void OnCampaignLoading() {
    if (S.on || !g_campDateSet) return;
    Log("lockstep: a campaign is loaded: the recorded campaign date is dropped");
    g_campDateSet = false;
}

// Swaps the recorded campaign date and the date in the variables (around the snapshot's campaign save).
static void SwapCampaignDate() {
    if (!g_campDateSet || !g_dateVar[0]) return;
    for (int i = 0; i < 3; i++) std::swap(g_campDate[i], *g_dateVar[i]);
}

void OnBattleSlotClosed() {
    if (!g_campDateSet || !g_dateVar[0]) return;
    Log("lockstep: campaign date %d-%02d-%02d put back (was %d-%02d-%02d after the battle)", g_campDate[0],
        g_campDate[1], g_campDate[2], *g_dateVar[0], *g_dateVar[1], *g_dateVar[2]);
    for (int i = 0; i < 3; i++) *g_dateVar[i] = g_campDate[i];
    g_campDateSet = false;
}

static void HoldImpl(const std::string& why, int bid) {
    S = Session();
    ResetSimSides();
    S.on = true;
    S.isA = true;
    S.mySide = 0;
    S.bid = bid;
    S.st = St::Holding;
    S.why = why;
    S.lastPeerMsg = GetTickCount();
    RecordCampaignDate();  // a new battle (no load has touched the date yet); a rejoin keeps its battle's record
    Log("lockstep: battle held at its start (%s)", why.c_str());
}

void Hold(const std::string& why, int bid) {
    if (S.on && S.st != St::Ended) return;
    if (!Available() || !EnsureHooks()) return;
    if (g_rjTry.want) {  // (a new battle can't start while the dropped one is on screen; just in case)
        Log("lockstep: rejoin: cancelled (a new battle)");
        g_rjTry = RejoinTry();
    }
    ForgetRejoin("a new battle", true);
    g_faw[0] = g_faw[1] = kFireAtWillDefault;  // a new live battle: each side's Fire at will starts at the default (a
                                               // rejoin keeps the battle's: RejoinTick holds without this)
    HoldImpl(why, bid);
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
    S.campaignSlot = info.campaignSlot;
    // The carrier-strike rule of this battle (the start and every resync carry it): the host's option, or the computer's
    // strikes for both sides when this PC can't offer the strike dialog.
    S.autoStrike = info.autoStrike || !g_featStrike;
    if (!info.autoStrike && !g_featStrike) Log("lockstep: air strikes from the dialog are off here: the computer flies both sides' carrier strikes");
    S.cw0 = FpuCw();
    S.everRunning = false;
    if (info.slot >= 0 && info.slot != game::CurrentSlot()) Log("lockstep: battle slot %d, open slot %d", info.slot, game::CurrentSlot());
    if (S.rejoin)
        Chat("Sending the battle to " + S.peerName + " (minute " + std::to_string(Minute()) + ")...");
    else
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
    // (Loading: this PC's load opened the battle but its setup failed; side 1 may be human there)
    if (S.st == St::Loading || S.st == St::WaitReady || S.st == St::Running) {
        SharedBegin();
        TryRun([] { SetSideControl(1, true, false); });
        SharedEnd();
    }
    Cleanup();
    S.on = false;
    S.st = St::Ended;
    if (S.rejoin)
        Chat(S.peerName + " could not rejoin the battle (" + err + "): the computer keeps commanding " + Who(1) +
             "; the battle goes on here.");
    else
        Chat("The live battle could not start (" + err + "); the battle goes on against the AI.");
    RememberRejoin(err);
    if (g_hooks.onEnded) g_hooks.onEnded(false, err);
    return false;
}

// ---- Rejoin (engine level 8) ----
// A dropped battle goes on here against the computer (RememberRejoin). When its player is back (the session matched
// them to the nation again and calls Rejoin), no new minute starts any more (MinuteGate, LS_StartRun); at the next
// quiet minute boundary (no minute running, a clean stack: RejoinTick) the battle is held and started again with them
// through the normal start: StartAsA's snapshot with a new seed and the next epoch, both PCs load it (B into its
// battle slot), LoadSnapshot's first load makes side 1 human on both PCs, the READY hashes are compared, then `go`.
// Side 1 is not on auto-run (the start's auto=); the game's own auto-run of side 0 (single player since the drop)
// becomes side 0's auto-run. If the boundary doesn't come within g_tRejoin, the start fails or B doesn't load in time,
// the computer keeps side 1 and the battle stays rejoinable.

static void RejoinGiveUp(const std::string& why) {
    if (!g_rjTry.want) return;
    std::string who = g_rjTry.peerName, side1 = g_rjTry.side1;
    g_rjTry = RejoinTry();
    Log("lockstep: rejoin: given up (%s); the computer keeps side 1", why.c_str());
    Chat(who + " could not rejoin the battle (" + why + "): the computer keeps commanding " + side1 +
         "; the battle goes on here.");
    if (g_hooks.notice) g_hooks.notice(who + " could not rejoin the battle: " + why);
}

// The game's Run while a rejoin waits for its boundary (the run is not started; LS_StartRun).
static void RejoinRunNote() {
    if (!g_rjTry.want || GetTickCount() - g_rjTry.lastRunNote < 3000) return;
    g_rjTry.lastRunNote = GetTickCount();
    Chat("The battle waits: " + g_rjTry.peerName + " is rejoining it; it runs again once both PCs have loaded it.");
}

static void RejoinTick() {
    if (!g_rjTry.want || S.on) return;
    DWORD now = GetTickCount();
    bool over = false;
    if (const char* bad = RejoinInvalid(&over)) {
        RejoinGiveUp(bad);
        ForgetRejoin(bad, over);
        return;
    }
    if ((int)(now - g_rjTry.since) > (int)g_tRejoin) {
        RejoinGiveUp("the battle did not reach a quiet minute boundary within " + std::to_string(g_tRejoin / 1000) +
                     " s");
        return;
    }
    void* m = BattleForm();
    std::string wait;
    if (g_sim > 0 || TimerOn(m, E.fMoveTimer))
        wait = "a minute runs";
    else if (game::InTurnProcessing())
        wait = "the turn is being processed";
    else
        CleanStack(&wait);
    if (!wait.empty()) {
        if (now - g_rjTry.lastLog > 5000) {
            g_rjTry.lastLog = now;
            Log("lockstep: rejoin: waiting for the minute boundary (%s; minute %d, t=%s)", wait.c_str(), Minute(),
                ClockText().c_str());
        }
        return;
    }
    // The boundary: the battle starts again with the returning player.
    StartInfo si;
    si.bid = g_rj.bid;
    si.slot = game::CurrentSlot();
    si.campaignSlot = g_rj.campaignSlot;
    si.opp = g_rj.opp;
    si.autoStrike = g_rj.autoStrike;
    si.myName = g_rj.myName;
    si.peerName = g_rjTry.peerName;
    si.sideNames[0] = g_rj.sideNames[0];
    si.sideNames[1] = g_rj.sideNames[1];
    si.send = g_rjTry.send;
    bool gameAuto = g_featAutoRun && E.fAutoRun >= 0 && dl::At<uint8_t>(m, E.fAutoRun) != 0;
    int fastest = FastestShared() && FastestDown(m) ? 1 : 0;
    bool resumed = g_rj.resumed, held = g_rj.held;
    RejoinTry waiting = g_rjTry;
    g_rjTry = RejoinTry();  // (from here the live battle's own gate holds the clock)
    HoldImpl("rejoin of " + si.peerName + " at minute " + std::to_string(Minute()), si.bid);
    S.rejoin = true;
    S.resumeStart = resumed && held;  // (B's chat: the host's game was restarted, the battle is straight from its save)
    S.rejoinAuto = gameAuto;
    S.autoRun[0] = gameAuto;  // (SaveSnapshot writes it under its player; every load hands it to the computer again)
    S.fastest = fastest;      // (the button as single player left it: the start's fastest=)
    S.epoch = g_rj.epoch;     // (the next epoch continues the battle's)
    Log("lockstep: rejoin: battle %d starts again with %s at minute %d (t=%s) after %lu ms%s%s", si.bid,
        si.peerName.c_str(), Minute(), ClockText().c_str(), now - waiting.since,
        gameAuto ? " (the game's auto-run of side 0 becomes side 0's auto-run)" : "",
        resumed ? (held ? " (resumed from the host's save, held until now)" : " (resumed from the host's save)") : "");
    std::string err;
    bool retry = false;
    if (StartAsA(si, err, retry)) {
        g_rj.held = false;  // (the live battle's own gate holds the clock; a failure leaves the battle unheld)
        return;
    }
    if (retry) {  // (not expected: the stack was just checked) wait for the next quiet moment
        Log("lockstep: rejoin: not now (%s)", err.c_str());
        S.on = false;
        S.st = St::Ended;
        g_rjTry = waiting;
        return;
    }
    g_rj.held = false;
    Log("lockstep: rejoin: the start failed (%s)", err.c_str());
}

bool Rejoinable(RejoinInfo& out) {
    out = RejoinInfo();
    if (g_rj.on) {
        out.bid = g_rj.bid;
        out.opp = g_rj.opp;
        out.dropMinute = g_rj.dropMinute;
        out.ageSec = (int)((GetTickCount() - g_rj.since) / 1000);
        out.peerName = g_rj.peerName;
        out.sideNames[0] = g_rj.sideNames[0];
        out.sideNames[1] = g_rj.sideNames[1];
        out.why = g_rj.why;
        out.epoch = g_rj.epoch;
        out.slot = g_rj.slot;
        out.campaignSlot = g_rj.campaignSlot;
        out.resumed = g_rj.resumed;
        out.held = g_rj.held;
        out.slotMode = g_rj.slotMode;
        out.serial = g_rj.serial;
        out.hostName = g_rj.myName;
    }
    out.minute = Minute();
    out.pending = g_rjTry.want;
    if (S.on && S.st != St::Ended) {
        out.held = false;
        if (S.isA && S.dropPending) {  // the player is dropped when this minute ends: the battle is rejoinable then
            out.bid = S.bid;
            out.opp = S.opp;
            out.dropMinute = Minute();
            out.ageSec = 0;
            out.peerName = S.peerName;
            out.sideNames[0] = S.sideNames[0];
            out.sideNames[1] = S.sideNames[1];
            out.why = S.dropWhy + " (when this minute ends)";
            out.epoch = S.epoch;
            out.campaignSlot = S.campaignSlot;
            out.slot = S.campaignSlot ? game::CurrentSlot() : -1;
            out.hostName = S.myName;
            return true;
        }
        out.why = S.rejoin && !S.everRunning ? "the battle is being started again with " + S.peerName
                                             : "a live battle runs on this PC";
        return false;
    }
    if (const char* bad = RejoinInvalid()) {
        out.why = bad;
        out.held = false;
        return false;
    }
    return true;
}

bool Rejoin(int bid, const std::string& peerName, SendFn send, std::string& err) {
    RejoinInfo ri;
    if (!Rejoinable(ri)) {
        err = ri.why;
        return false;
    }
    if (bid != ri.bid) {
        err = "battle " + std::to_string(bid) + " is not the battle dropped here (" + std::to_string(ri.bid) + ")";
        return false;
    }
    if (!Available() || !EnsureHooks()) {
        err = "the live battle engine is not available";
        return false;
    }
    bool again = g_rjTry.want;
    g_rjTry = RejoinTry();
    g_rjTry.want = true;
    g_rjTry.peerName = peerName;
    g_rjTry.side1 = ri.sideNames[1].empty() ? std::string("side 1") : ri.sideNames[1];
    g_rjTry.send = send;
    g_rjTry.since = GetTickCount();
    Log("lockstep: rejoin: %s is back for battle %d (dropped at minute %d: %s)%s: no new minute starts; the battle "
        "starts again at the next minute boundary (minute %d, t=%s)",
        peerName.c_str(), bid, ri.dropMinute, ri.why.c_str(), again ? " (again)" : "", Minute(), ClockText().c_str());
    Chat(peerName + " is back: the battle stops at the next minute and starts again with " + peerName + " commanding " +
         g_rjTry.side1 + ".");
    return true;
}

void CancelRejoin(const std::string& why) { RejoinGiveUp(why); }

bool RejoinPending() { return g_rjTry.want; }

std::string RejoinRequestMessage(int bid, const std::string& peerName) {
    KV kv;
    kv["t"] = "rejoin";
    kv["bid"] = std::to_string(bid);
    kv["epoch"] = "0";
    kv["p2"] = peerName;
    kv["lv"] = std::to_string(AnnouncedLsLevel());
    return PackHB(KVEncode(kv), "");
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

std::string MessageField(const std::string& payload, const char* key) {
    if (payload.size() < 4) return "";
    uint32_t n;
    memcpy(&n, payload.data(), 4);
    if (4 + (size_t)n > payload.size()) return "";
    return KVStr(KVDecode(payload.substr(4, n)), key);  // (the header only: a start's body is the battle's files)
}

std::string PeerLostMessage(int bid, const std::string& why) {
    KV kv;
    kv["t"] = "peerlost";
    kv["bid"] = std::to_string(bid);
    kv["why"] = why;
    return PackHB(KVEncode(kv), "");
}

void OnPeerLost(const std::string& why) {
    CancelRejoin(why);  // (a rejoin waiting for its boundary, or for the drop of this minute: the player is gone again)
    if (!S.on || S.st == St::Ended) return;
    if (S.isA)
        DropPeer(why);
    else
        AbortB(why);
}

// Phase 5b. The game resumed a battle from the host's save outside a live battle (OnCampaignLoaded): if the slot's
// marker describes exactly this battle save, it was the host's live battle against a joined player. It becomes
// rejoinable as after a drop (the memo: the player gets side 1 back through the rejoin's start when they are back) and it
// is held: no minute starts until then, or until the host presses Run (it goes on against the computer and stays
// rejoinable). The state the battle save drops comes back from the marker; a test battle outside a month keeps its
// battle slot mode.
static void ResumeFromMarker() {
    int slot = game::CurrentSlot();
    if (slot < 1 || slot > 49) return;  // (the host's campaign slots; a joined player's slots are 50 and above)
    std::string text;
    if (!ReadFileBytes(MarkerPath(slot), text)) return;  // a battle the game saved itself: nothing to do
    KV mk = KVDecode(text);
    int bid = KVInt(mk, "bid", -1), opp = KVInt(mk, "opp", -1), minute = KVInt(mk, "minute", -1);
    std::string peer = KVStr(mk, "peer"), sac = SacHash(slot), why;
    if (KVInt(mk, "v") != 1)
        why = "unknown marker version";
    else if (sac.empty() || KVStr(mk, "sac") != sac)
        why = "the battle save is not the live battle's last snapshot (saved again by the game, or another battle)";
    else if (bid < 0 || opp < 1 || opp > 8 || peer.empty())
        why = "the marker is incomplete";
    else if (!Available() || !EnsureHooks())
        why = "the live battle engine is not available";
    if (!why.empty()) {
        Log("lockstep: resume: the battle resumed from slot %d is not taken up as a live battle (%s)", slot,
            why.c_str());
        return;
    }
    AlignLogLists(BattleForm(), "the resume");  // (a snapshot of an earlier version: see AlignLogLists)
    if (g_rjTry.want) Log("lockstep: rejoin: cancelled (another battle was loaded)");
    g_rjTry = RejoinTry();
    if (g_rj.on) Log("lockstep: rejoin: battle %d can no longer be rejoined (another battle was loaded)", g_rj.bid);
    g_rj = RejoinMemo();
    g_rj.on = true;
    g_rj.bid = bid;
    g_rj.epoch = KVInt(mk, "epoch", 0);
    g_rj.opp = opp;
    g_rj.campaignSlot = true;
    g_rj.autoStrike = KVInt(mk, "autostrike") != 0;
    g_rj.myName = KVStr(mk, "host", "the host");
    g_rj.peerName = peer;
    g_rj.sideNames[0] = KVStr(mk, "s0");
    g_rj.sideNames[1] = KVStr(mk, "s1");
    g_rj.form = BattleForm();  // (the battle screen this load made)
    g_rj.battleSerial = game::DoBattleCalls();
    g_rj.dropMinute = Minute();
    g_rj.why = "the battle was loaded from the host's save";
    g_rj.since = GetTickCount();
    g_rj.slot = slot;
    g_rj.serial = ++g_rjSerial;
    g_rj.resumed = true;
    g_rj.held = true;
    g_rj.slotMode = KVInt(mk, "slotmode") != 0;
    // The state the battle save dropped: Fire at will per side (the battle's own setting, which a rejoin keeps), and
    // night CAP and the pending missile targets if the save is from the marker's own minute.
    bool same = minute == Minute();
    std::string ncap = KVStr(mk, "ncap"), pmt = KVStr(mk, "pmt"), faw = KVStr(mk, "faw", "11");
    TryRun([&] {
        ApplyFireAtWill(faw);
        if (!same) return;
        if (!ncap.empty() && !ApplyNightCap(ncap)) Log("lockstep: resume: night CAP not put back (the forces differ)");
        if (!ApplyPendingTargets(pmt))
            Log("lockstep: resume: the pending missile targets not put back (the ships differ)");
    });
    if (!same)
        Log("lockstep: resume: the battle save is from minute %d, the marker's state from minute %d: night CAP and the "
            "pending missile targets stay as loaded",
            Minute(), minute);
    if (g_rj.slotMode && !game::BattleSlotMode()) {  // a test battle outside a month: its close runs no month, as before
        game::SetBattleSlotMode(true);
        Log("lockstep: resume: a test battle outside a month: battle slot mode on (its close runs no month)");
    }
    // This load wrote the battle's day and month into the campaign date variables: the battle goes on with the loaded
    // save's own date (as after every live load), which is also the date the rejoin's snapshots swap in.
    RecordLoadedCampaignDate();
    Log("lockstep: resume: battle %d (epoch %d) of slot %d resumed at minute %d (t=%s; marker of %s): a live battle "
        "against %s (%s, nation %d); held until %s rejoins or Run is pressed",
        bid, g_rj.epoch, slot, Minute(), ClockText().c_str(), KVStr(mk, "saved", "?").c_str(), peer.c_str(),
        g_rj.sideNames[1].c_str(), opp, peer.c_str());
    RejoinInfo ri;
    Rejoinable(ri);
    RunOnMainThread([ri] {  // (after the load: the session tells the host and offers the battle)
        if (g_hooks.resumed) g_hooks.resumed(ri);
    });
}

// After the game's LoadBuildCampaign. A campaign whose battle is in progress resumes that battle inside the load
// (LoadScenarioInProgress -> TfrmMain.Execute: the battle screen is open afterwards). A save made in a live battle (the
// host's Save checkpoint, or the snapshot written at the start and at every resync) has side 1 under human control.
// In 1.01.44 the game's resume itself gives side 1 back to the computer (LoadScenarioInProgress sets PlayerSide 0,
// SetupGlobeRTWLayers then side 1's AI flag in its side table, and SetDivisionAIControl(resumed) sets every side-1 force
// and division to AIControlled and clears ForceIsHumanControlled of their ships), so this only acts if a later game
// version stops doing that. A vanilla save never has human ships on side 1: nothing to do there either. Then the
// slot's marker may make it a live battle again (ResumeFromMarker, phase 5b).
void OnCampaignLoaded() {
    if (g_loading || (S.on && S.st != St::Ended) || !game::BattleActive() || !Resolve() || !BattleVisible()) return;
    int ships = 0, forces = 0, divs = 0;
    void* fl = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(fl); i++)
        if (void* fc = dl::ListItem(fl, i))
            if (dl::At<int>(fc, E.fForceSide) == 1 && !dl::At<uint8_t>(fc, E.fFAI)) forces++;
    void* divl = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divl); i++) {
        void* dv = dl::ListItem(divl, i);
        if (!dv || dl::At<int>(dv, E.fDivSide) != 1) continue;
        if (!dl::At<uint8_t>(dv, E.fDAI)) divs++;
        void* sh = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(sh); j++)
            if (void* s = dl::ListItem(sh, j))
                if (dl::At<uint8_t>(s, E.fHuman)) ships++;
    }
    if (!ships) {
        Log("lockstep: a battle in progress was resumed from a save; side 1 is the computer's (no ship flagged human; "
            "%d forces and %d divisions without computer control)", forces, divs);
    } else {
        Log("lockstep: a battle saved in a live battle was resumed: %d ships of side 1 were flagged human; the "
            "computer commands side 1",
            ships);
        SetSideControl(1, true, false);
    }
    ResumeFromMarker();
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
    if (g_rjTry.want && !S.on) {  // A: a rejoin waits for its minute boundary (a start takes seconds: the rest waits)
        RejoinTick();
        return;
    }
    if (g_rj.on && !(S.on && S.st != St::Ended)) {  // A: is the dropped battle still the one on screen?
        bool over = false;
        if (const char* bad = RejoinInvalid(&over)) ForgetRejoin(bad, over);
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
    char b[960];
    void* m = BattleForm();
    snprintf(b, sizeof(b),
             "ok on=%d role=%s st=%s bid=%d epoch=%d side=%d minute=%d t=%s boundary=%d running=%d left=%d speed=%d "
             "fastest=%d auto0=%d auto1=%d peer=%s peerAI=%d muted=%d seq=%d/%d pending=%d/%d overlay=%d verified=%d "
             "peerDone=%d resyncs=%d ok=%d end=%d/%d proposal=%d busy=%d leave=%d atb=%d broken=%d failed=%d peerEnded=%d "
             "lost=%d rejoin=%d rj=%d frames=%d disp=%s hash=%s",
             S.on ? 1 : 0, S.isA ? "A" : "B", StName(S.st), S.bid, S.epoch, S.mySide, Minute(), ClockText().c_str(),
             S.boundary, S.minuteRunning ? 1 : 0, S.runLeft, m ? dl::At<int>(m, E.fGameSpeed) : -99, S.fastest,
             S.autoRun[0] ? 1 : 0, S.autoRun[1] ? 1 : 0, S.peerName.empty() ? "-" : S.peerName.c_str(), S.peerAI ? 1 : 0,
             g_muted ? 1 : 0, S.mySeq, S.peerSeq, (int)S.myBatches.size(), (int)S.peerBatches.size(), (int)g_overlay.size(),
             S.verifiedN, S.peerDoneN, S.resyncs, S.okMinutes, S.endWant[0] ? 1 : 0, S.endWant[1] ? 1 : 0,
             S.proposal ? 1 : 0, Busy() ? 1 : 0, S.leavePending ? 1 : 0, S.atBoundary ? 1 : 0, S.minuteBroken ? 1 : 0,
             S.brokenMinutes, S.peerEnded ? 1 : 0, S.battleLost ? 1 : 0, g_rjTry.want ? 2 : g_rj.on ? 1 : 0,
             S.rejoin ? 1 : 0, g_nFrames, DispatchDepthText().c_str(), S.myHash.empty() ? "-" : S.myHash.c_str());
    return b;
}

bool DebugIsAsync(const std::vector<std::string>& a) {
    std::string c = a.size() > 1 ? a[1] : "";
    return c == "resync" || c == "detach" || c == "nest" || c == "torp" || c == "strikedlg" || c == "missiles";
}

// `ls forces [div]`: the order of battle (indexes into the battle's forces and divisions lists).
static std::string ForcesText(bool divView) {
    if (!Resolve() || !BattleForm()) return "error no battle";
    if (E.fFDivs < 0 || E.fMyForce < 0 || E.fAdmin < 0 || E.fLeadDiv < 0 || E.fFLead < 0)
        return "error the force fields were not found";
    static int fRole = dl::Field("TDivision", "Role");
    void* forces = List(E.forcesVar);
    void* divs = List(E.divisionsVar);
    auto idx = [](int i) { return i >= 0 ? std::to_string(i) : std::string("-"); };
    auto num = [](void* o, int off, int size) {
        return off < 0 ? std::string("?") : std::to_string(size == 1 ? (int)dl::At<uint8_t>(o, off) : dl::At<int>(o, off));
    };
    std::string out = "ok";
    if (!divView) {
        for (int i = 0; i < dl::ListCount(forces); i++) {
            void* fc = dl::ListItem(forces, i);
            if (!fc) continue;
            void* fd = dl::At<void*>(fc, E.fFDivs);
            std::string list;
            int lead = -1;
            for (int j = 0; j < dl::ListCount(fd); j++) {
                void* d = dl::ListItem(fd, j);
                list += (j ? "," : "") + idx(d ? IndexIn(divs, d) : -1);
                if (lead < 0 && d && dl::At<uint8_t>(d, E.fFLead) && ShipCount(d) > 0) lead = IndexIn(divs, d);
            }
            out += " " + std::to_string(i) + ":" + std::to_string(dl::At<int>(fc, E.fForceSide)) + ":" + W2U(ObjName(fc)) +
                   ":divs=" + list + ":lead=" + idx(lead) + ":changed=" + num(fc, E.fHasChangedLead, 1) +
                   ":turnaway=" + num(fc, E.fTimeToTurnAway, 4) + ":ai=" + num(fc, E.fFAI, 1) +
                   ":withdraw=" + num(fc, E.fWithdraw, 4) + ":mission=" + num(fc, E.fMission, 1) + ";";
        }
        return out;
    }
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* d = dl::ListItem(divs, i);
        if (!d) continue;
        void* ld = dl::At<void*>(d, E.fLeadDiv);
        out += " " + std::to_string(i) + ":" + std::to_string(dl::At<int>(d, E.fDivSide)) + ":" + W2U(ObjName(d)) +
               ":force=" + idx(IndexIn(forces, dl::At<void*>(d, E.fMyForce))) + ":admin=" + num(d, E.fAdmin, 4) +
               ":lead=" + idx(ld ? IndexIn(divs, ld) : -1) + ":role=" + num(d, fRole, 1) + ":flead=" + num(d, E.fFLead, 1) +
               ":ai=" + num(d, E.fDAI, 1) + ";";
    }
    return out;
}

// The air formations now, per side: by mission (TAirMission, 8 values) and status (TAircraftStatus, 15 values).
static std::string AirStatText() {
    int n[3] = {0, 0, 0}, mis[2][8] = {}, st[2][15] = {}, confirm = 0;
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++) {
        void* af = dl::ListItem(air, i);
        if (!af) continue;
        int sd = dl::At<int>(af, E.fAfSide), k = sd == 0 || sd == 1 ? sd : 2;
        n[k]++;
        if (k == 2) continue;
        int mi = E.fAfMission >= 0 ? dl::At<uint8_t>(af, E.fAfMission) : -1;
        int si = E.fAfStatus >= 0 ? dl::At<uint8_t>(af, E.fAfStatus) : -1;
        if (mi >= 0 && mi < 8) mis[k][mi]++;
        if (si >= 0 && si < 15) st[k][si]++;
        if (E.fAfConfirm >= 0 && dl::At<uint8_t>(af, E.fAfConfirm)) confirm++;
    }
    std::string out = "ok af=" + std::to_string(n[0] + n[1] + n[2]) + " side0=" + std::to_string(n[0]) +
                      " side1=" + std::to_string(n[1]) + " other=" + std::to_string(n[2]);
    for (int k = 0; k < 2; k++) {
        out += " m" + std::to_string(k) + "=";
        for (int i = 0; i < 8; i++) out += (i ? "," : "") + std::to_string(mis[k][i]);
        out += " st" + std::to_string(k) + "=";
        for (int i = 0; i < 15; i++) out += (i ? "," : "") + std::to_string(st[k][i]);
    }
    out += " confirm=" + std::to_string(confirm) + " aistrike=" + std::to_string(S.aiAir[0]) + "/" +
           std::to_string(S.aiAir[1]) + " ainaval=" + std::to_string(S.aiNaval[0]) + "/" + std::to_string(S.aiNaval[1]) +
           " aicalls=" + std::to_string(S.aiAirCalls) + " cleared=" + std::to_string(S.confirmCleared) +
           " planSkipped=" + std::to_string(S.planSkipped) + " missileCalls=" + std::to_string(S.missileCalls) +
           " searchCalls=" + std::to_string(S.scoutCalls) + " landcapCalls=" + std::to_string(S.landCapCalls) +
           " rules=" + std::to_string(g_ruleAir ? 1 : 0) + std::to_string(g_rulePlan ? 1 : 0) +
           std::to_string(g_ruleMissile ? 1 : 0) + std::to_string(g_rulePanel ? 1 : 0) +
           std::to_string(g_ruleLandCap ? 1 : 0) + std::to_string(g_ruleScout ? 1 : 0);
    return out;
}

// ---- Routine air ops and land-based CAP on the bridge (engine level 6) ----

static std::string DivRef(void* d) {  // "<index in the divisions list>:<name>" or "-"
    if (!d) return "-";
    int i = IndexIn(List(E.divisionsVar), d);
    return (i >= 0 ? std::to_string(i) : std::string("?")) + ":" + W2U(ObjName(d));
}

// The land-CAP requests in the game's layout, the display swap, the feature, the mod's copy of the routine's counts, and
// every airfield division (its flagship IsAirfield) with its CappedDivision: `landcap0=<i>:<name>|- landcap1=...
// swapped=<0|1> mine=<side> feature=<0|1> rule=<0|1> calls=<n> other=<n> airfields=<i>:<side>:<capped i|->;...`
static std::string LandCapText() {
    if (!E.landCap) return "landcap=unknown";
    std::string out = "landcap0=" + DivRef(*LandCapSlot(0)) + " landcap1=" + DivRef(*LandCapSlot(1)) +
                      " swapped=" + std::to_string(g_lcSwapped ? 1 : 0) + " mine=" + std::to_string(S.on ? S.mySide : -1) +
                      " feature=" + std::to_string(g_featLandCap ? 1 : 0) + " rule=" + std::to_string(g_ruleLandCap ? 1 : 0) +
                      " calls=" + std::to_string(S.landCapCalls) + " other=" + std::to_string(S.landCapOther) + " airfields=";
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* d = dl::ListItem(divs, i);
        if (!d || ShipCount(d) <= 0 || !E.isAirfield || !E.getFlagship) continue;
        bool ok = false;
        void* fs = (void*)(uintptr_t)SafeCall(E.getFlagship, U(d), 0, 0, 0, nullptr, &ok);
        if (!ok || !fs || !(SafeCall(E.isAirfield, U(fs), 0, 0, 0, nullptr, &ok) & 0xFF) || !ok) continue;
        void* capped = E.fCapped >= 0 ? dl::At<void*>(d, E.fCapped) : nullptr;
        int ci = capped ? IndexIn(divs, capped) : -1;
        out += std::to_string(i) + ":" + std::to_string(dl::At<int>(d, E.fDivSide)) + ":" +
               (capped ? (ci >= 0 ? std::to_string(ci) : std::string("?")) : std::string("-")) + ";";
    }
    return out;
}

// `ls airforce <force>`: the force's routine air ops settings (the guard's order fields), the simulation's search
// count-downs and the land-CAP requests.
static std::string AirForceText(void* force) {
    if (!IsForce(force)) return "error force";
    if (E.fCapLevel < 0 || E.fOrdered < 0 || E.ordSize <= 0) return "error the force's air fields were not found";
    auto num = [force](int off, int size) {
        return off < 0 ? std::string("?") : std::to_string(size == 1 ? (int)dl::At<uint8_t>(force, off) : dl::At<int>(force, off));
    };
    static int fFlag = dl::Field("TForce", "ForceFlagship");
    void* flag = fFlag >= 0 ? dl::At<void*>(force, fFlag) : nullptr;
    std::string out = "ok force=" + std::to_string(IndexIn(List(E.forcesVar), force)) + " name=" + W2U(ObjName(force)) +
                      " side=" + num(E.fForceSide, 4) + " cap=" + num(E.fCapLevel, 4) + " nightcap=" + num(E.fNightCap, 1) +
                      " left=" + num(E.fSearchLeft, 4) + " right=" + num(E.fSearchRight, 4) + " dist=" + num(E.fSearchDist, 4) +
                      " interval=" + num(E.fSearchInterval, 4) + " twophase=" + num(E.fTwoPhase, 1) +
                      " repeat=" + num(E.fRepeatSearch, 1) + " nightsearch=" + num(E.fNightSearch, 1) + " ordered=";
    for (int i = 0; i < E.ordSize; i++) out += dl::At<uint8_t>(force, E.fOrdered + i) ? '1' : '0';
    out += " current=";
    for (int i = 0; E.fCurrent > 0 && i < E.curSize / 4; i++)
        out += (i ? "," : "") + std::to_string(dl::At<int>(force, E.fCurrent + 4 * i));
    out += " flagship=" + (flag ? W2U(ObjName(flag)) : std::string("-")) + " feature=" + std::to_string(g_featAirOps ? 1 : 0) +
           " | " + LandCapText();
    return out;
}

// `ls airops <force> [cap=0-3] [nightcap=0|1] [left=] [right=] [dist=] [interval=] [twophase=0|1] [repeat=0|1]
// [nightsearch=0|1] [raw=1]`: what the routine air ops dialog's OK writes (its nine fields, each given value or the
// force's own as the dialog shows it, then ProcessSearchDirectons) on this PC's own force; the order guard takes the
// edit at once (an order batch). The ranges are the dialog's spin edits'; the nation's two-phase-search tech (which
// enables that box) is not checked. raw=1 (tests): another side's force too, as a display write: the guard must put it
// back at the next step and send nothing.
static std::string AirOpsCommand(const std::vector<std::string>& a) {
    if (!g_featAirOps) return "error routine air operations are not available in this live battle (see the log)";
    std::string err;
    if (!CanOrderNow(err)) return "error " + err;
    void* force = a.size() > 2 ? dl::ListItem(List(E.forcesVar), atoi(a[2].c_str())) : nullptr;
    if (!IsForce(force)) return "error force";
    struct Set {
        const char* key;
        int off, size, lo, hi, val;
        bool given;
    } sets[] = {{"cap", E.fCapLevel, 4, 0, 3},          {"nightcap", E.fNightCap, 1, 0, 1},
                {"left", E.fSearchLeft, 4, -10, 370},   {"right", E.fSearchRight, 4, -10, 360},
                {"dist", E.fSearchDist, 4, 0, 1000},     {"interval", E.fSearchInterval, 4, 0, 350},
                {"twophase", E.fTwoPhase, 1, 0, 1},      {"repeat", E.fRepeatSearch, 1, 0, 1},
                {"nightsearch", E.fNightSearch, 1, 0, 1}};
    bool raw = false;
    for (size_t i = 3; i < a.size(); i++) {
        size_t eq = a[i].find('=');
        if (eq == std::string::npos) return "error " + a[i] + " (use name=value)";
        std::string k = a[i].substr(0, eq);
        int v = atoi(a[i].c_str() + eq + 1);
        if (k == "raw") {
            raw = v != 0;
            continue;
        }
        Set* s = nullptr;
        for (auto& x : sets)
            if (k == x.key) s = &x;
        if (!s) return "error unknown setting " + k;
        if (v < s->lo || v > s->hi) return "error " + k + " must be " + std::to_string(s->lo) + ".." + std::to_string(s->hi);
        s->val = v;
        s->given = true;
    }
    int side = dl::At<int>(force, E.fForceSide);
    if (side != S.mySide && !raw)
        return "error That force is " + Who(side) + "'s: you can set the routine air operations of your own forces only.";
    for (auto& s : sets)
        if (s.given) {
            if (s.size == 1)
                dl::At<uint8_t>(force, s.off) = (uint8_t)s.val;
            else
                dl::At<int>(force, s.off) = s.val;
        }
    bool ok = false;
    SafeCall(E.processSearch, U(force), 0, 0, 0, nullptr, &ok);
    if (!ok) return "error ProcessSearchDirectons failed";
    int before = S.mySeq;
    DetectEdits("routine air ops");
    return AirForceText(force) + " | batch=" + (S.mySeq > before ? "#" + std::to_string(S.mySeq) : std::string("none"));
}

// `ls landcap <division>|-`: what the division report's OK writes with its land-CAP button down (LandCAPDivision[0] :=
// the division, in this PC's display layout: this player's own request); the order guard takes the edit at once. `-`
// clears the request (tests; the game's dialog can only move it to another division).
static std::string LandCapCommand(const std::vector<std::string>& a) {
    if (!g_featLandCap) return "error land-based CAP is not available in this live battle (see the log)";
    std::string err;
    if (!CanOrderNow(err)) return "error " + err;
    void* div = nullptr;
    if (a[2] != "-") {
        div = dl::ListItem(List(E.divisionsVar), atoi(a[2].c_str()));
        if (!IsDivision(div)) return "error division";
    }
    E.landCap[0] = div;  // the dialog's own write (bnOkClick)
    if (*LandCapSlot(S.mySide) != div)
        return "error slot 0 is not this player's request here (the display swap is off: ls view 0?) | " + LandCapText();
    int before = S.mySeq;
    DetectEdits("land-based CAP");
    return "ok " + LandCapText() + " | batch=" + (S.mySeq > before ? "#" + std::to_string(S.mySeq) : std::string("none"));
}

// ---- Torpedo salvoes on the bridge (engine level 5) ----

// A ship: "<division index>.<ship index>" (the battle's divisions list, that division's ships) or the index in the list
// of all the battle's ships.
static void* ShipArg(const std::string& s) {
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        void* dv = dl::ListItem(List(E.divisionsVar), atoi(s.substr(0, dot).c_str()));
        return dv ? dl::ListItem(dl::At<void*>(dv, E.fShips), atoi(s.c_str() + dot + 1)) : nullptr;
    }
    return s.empty() ? nullptr : dl::ListItem(List(E.allShipsVar), atoi(s.c_str()));
}

// `ls mounts <ship>`: the ship's torpedo and missile mounts in Tship.TorpedomountList order (a `torp` order's index).
static std::string MountsText(void* ship) {
    if (!IsFightingShip(ship)) return "error ship";
    if (E.fMountList <= 0 || E.fTorpsLeft < 0) return "error the torpedo mount fields were not found";
    auto num = [](void* o, int off, int size) {
        return off < 0 ? std::string("?") : std::to_string(size == 1 ? (int)dl::At<uint8_t>(o, off) : dl::At<int>(o, off));
    };
    std::string out = "ok ship=" + W2U(ObjName(ship)) + " side=" + num(ship, E.fShipSide, 4) + " sunk=" +
                      num(ship, E.fSunk, 1) + " speed=" + num(ship, E.fCurSpeed, 4) + " type=" + num(ship, E.fShipType, 1) +
                      " fired=" + num(ship, E.fFiredMount, 1);
    void* ml = MountList(ship);
    int n = dl::ListCount(ml);
    out += " mounts=" + std::to_string(n);
    for (int i = 0; i < n; i++) {
        void* mt = dl::ListItem(ml, i);
        if (!mt) continue;
        wchar_t pos = E.fMountPos >= 0 ? dl::At<wchar_t>(mt, E.fMountPos) : L'?';
        bool ok = false;
        uint32_t sub = E.isSubmerged ? SafeCall(E.isSubmerged, U(mt), 0, 0, 0, nullptr, &ok) & 0xFF : 0;
        out += " " + std::to_string(i) + ":type=" + num(mt, E.fMountType, 1) +
               ":pos=" + (pos > 32 && pos < 127 ? std::string(1, (char)pos) : std::string("?")) +
               ":tubes=" + num(mt, E.fTubes, 4) + ":left=" + num(mt, E.fTorpsLeft, 4) +
               ":reload=" + num(mt, E.fReloadTime, 4) + ":reloads=" + num(mt, E.fReloads, 4) +
               ":destroyed=" + num(mt, E.fMountDestroyed, 1) + ":sub=" + (ok ? std::to_string(sub) : std::string("?")) +
               ";";
    }
    return out;
}

// The torpedo dialog's lists as its handlers use them.
static int LbCount(void* lb) {
    HWND h = lb ? game::FormHwnd(lb) : nullptr;
    return h ? (int)SendMessageW(h, LB_GETCOUNT, 0, 0) : -1;
}
static std::string LbText(void* lb, int i) {
    HWND h = lb ? game::FormHwnd(lb) : nullptr;
    int n = h ? (int)SendMessageW(h, LB_GETTEXTLEN, (WPARAM)i, 0) : -1;
    if (n < 0 || n > 4000) return "";
    std::wstring s((size_t)n + 1, L'\0');
    SendMessageW(h, LB_GETTEXT, (WPARAM)i, (LPARAM)&s[0]);
    s.resize((size_t)n);
    return W2U(s);
}
static void* LbObject(void* lb, int i) {  // Items.Objects[i]
    void* items = lb && E.fListItems > 0 ? dl::At<void*>(lb, E.fListItems) : nullptr;
    if (!items || E.itemsObjSlot <= 0) return nullptr;
    bool ok = false;
    void* o = (void*)(uintptr_t)SafeCall((*(void***)items)[E.itemsObjSlot / 4], U(items), (uint32_t)i, 0, 0, nullptr, &ok);
    return ok ? o : nullptr;
}
static bool CtlSelect(void* ctl, int i) {  // ItemIndex := i (a list box or a combo box)
    if (!ctl || E.lbSetIdxSlot <= 0) return false;
    bool ok = false;
    SafeCall((*(void***)ctl)[E.lbSetIdxSlot / 4], U(ctl), (uint32_t)i, 0, 0, nullptr, &ok);
    return ok;
}
static bool CallHandler(void* fn, void* self, void* sender) {  // a click / change handler (Self, Sender)
    bool ok = false;
    if (fn && self) SafeCall(fn, U(self), U(sender), 0, 0, nullptr, &ok);
    return ok;
}

// `ls torp <division> [ship|-] [target|first] [mount|first]` (async: the game's own message boxes may open): a player's
// path to a torpedo salvo. The division report of this PC's division (ShowDivisionDialog, as a click on it), its Fire
// torpedoes (the hooked handler: the gate, the game's check of the ship, the dialog), the ship (the index in the division,
// through the dialog's ship box), the target (its index in the dialog's target list, or the first one with a firing
// solution for the mount), the mount (its index in Tship.TorpedomountList, or the first line with a solution) and Fire
// (the hooked handler: the gate, the dialog's own solution and jitter, FireMount's hook).
static std::string TorpCommand(const std::vector<std::string>& a) {
    auto arg = [&](size_t i, const std::string& def) { return i < a.size() && !a[i].empty() ? a[i] : def; };
    if (!g_featTorp) return "error manual torpedo salvoes are not available in this live battle (see the log)";
    if (!E.showDivDialog || !E.lbTargetsClick || !E.lbMountsClick || !E.cbShipsChange || E.lbSetIdxSlot <= 0 ||
        E.itemsObjSlot <= 0 || E.fListItems <= 0)
        return "error the dialogs' pieces for the bridge were not found";
    if (!BetweenMinutes()) return "error Pause the battle first: torpedoes are fired between minutes.";
    void* m = BattleForm();
    void* div = dl::ListItem(List(E.divisionsVar), atoi(arg(2, "-1").c_str()));
    if (!m || !IsDivision(div)) return "error division";
    if (dl::At<int>(div, E.fDivSide) != S.mySide) return "error that division is not yours";
    void *report = DialogForm("TdlgDivisionReport"), *dlg = DialogForm("TdlgTorpTargets");
    if (!report || !dlg) return "error the division report or the torpedo dialog was not found";
    static int fBnTorps = dl::Field("TdlgDivisionReport", "bnTorps"), fEnabled = dl::Field("TControl", "FEnabled"),
               fLbT = dl::Field("TdlgTorpTargets", "lbTargets"), fLbM = dl::Field("TdlgTorpTargets", "lbMounts"),
               fCb = dl::Field("TdlgTorpTargets", "cbShipsInDivision"), fBnFire = dl::Field("TdlgTorpTargets", "bnFire");
    if (fLbT <= 0 || fLbM <= 0 || fCb <= 0) return "error the torpedo dialog's controls were not found";
    if (!CallHandler(E.showDivDialog, m, div) || !game::FormVisible(report)) return "error the division report did not open";
    void* bn = fBnTorps > 0 ? dl::At<void*>(report, fBnTorps) : nullptr;
    // (the button is enabled when the battle preference Realism is above its second level: TdlgDivisionReport.FormShow)
    std::string button = bn && fEnabled >= 0 ? std::to_string(dl::At<uint8_t>(bn, fEnabled)) : "?";
    if (button == "0")  // the player's gate: a disabled button can't be clicked
        return "error the division report's Fire torpedoes is disabled (button=0: the battle's Realism setting; the "
               "player can't open the torpedo dialog either)";
    CallHandler(E.torpsClick, report, bn);
    if (!game::FormVisible(dlg)) return "error the torpedo dialog did not open (button=" + button + ")";
    if (arg(3, "-") != "-") {
        void* cb = dl::At<void*>(dlg, fCb);
        if (!CtlSelect(cb, atoi(arg(3, "0").c_str())) || !CallHandler(E.cbShipsChange, dlg, cb)) return "error ship";
    }
    void* ship = dl::At<void*>(m, E.fSelShip);
    if (!IsFightingShip(ship)) return "error no ship selected";
    void *lbT = dl::At<void*>(dlg, fLbT), *lbM = dl::At<void*>(dlg, fLbM);
    std::string mw = arg(5, "first"), tw = arg(4, "first");
    void* wantMount = mw == "first" ? nullptr : dl::ListItem(MountList(ship), atoi(mw.c_str()));
    if (mw != "first" && !wantMount) return "error mount";
    auto lineToFire = [&]() -> int {  // a line of the mount list with a mount (a HIGH or LOW solution), -1: none
        int nm = LbCount(lbM);
        for (int j = 0; j < nm; j++)
            if (void* o = LbObject(lbM, j))
                if (!wantMount || o == wantMount) return j;
        return -1;
    };
    int nt = LbCount(lbT), ti = -1, mj = -1;
    if (tw != "first" && (atoi(tw.c_str()) < 0 || atoi(tw.c_str()) >= nt))
        return "error target " + tw + " (the dialog lists " + std::to_string(nt) + ")";
    for (int i = tw == "first" ? 0 : atoi(tw.c_str()); i >= 0 && i < nt; i++) {
        if (!CtlSelect(lbT, i) || !CallHandler(E.lbTargetsClick, dlg, lbT)) return "error target";
        ti = i;
        mj = lineToFire();
        if (mj >= 0 || tw != "first") break;
    }
    std::string sols;
    for (int j = 0, nm = LbCount(lbM); j < nm; j++) {
        void* o = LbObject(lbM, j);
        int k = o ? IndexIn(MountList(ship), o) : -1;
        sols += std::to_string(j) + " " + LbText(lbM, j) + (k >= 0 ? " [mount " + std::to_string(k) + "]" : "") + "; ";
    }
    std::string targets = std::to_string(nt) + ":";
    for (int i = 0; i < nt && targets.size() < 1500; i++) targets += " " + std::to_string(i) + " " + LbText(lbT, i) + ";";
    std::string tail = " | ship=" + W2U(ObjName(ship)) + " button=" + button + " | solutions: " + sols + "| targets " + targets;
    if (ti < 0 || mj < 0)
        return "ok queued=0 target=" + std::to_string(ti) + " (no firing solution" +
               (wantMount ? " for that mount" : std::string()) + ")" + tail;
    void* mount = LbObject(lbM, mj);
    std::string line = LbText(lbM, mj);
    if (!CtlSelect(lbM, mj) || !CallHandler(E.lbMountsClick, dlg, lbM)) return "error mount list";
    size_t before = PendingDeferred().size();
    g_torpLast = "the fire routine was not called";
    CallHandler(E.torpFireClick, dlg, fBnFire > 0 ? dl::At<void*>(dlg, fBnFire) : nullptr);
    bool queued = PendingDeferred().size() > before;
    return "ok queued=" + std::to_string(queued ? 1 : 0) + " target=" + std::to_string(ti) +
           " mount=" + std::to_string(IndexIn(MountList(ship), mount)) + " | result: " + g_torpLast + " | solution: " + line +
           tail;
}

// ---- Missiles on the bridge (engine level 9) ----

// `ls missilestat`: per side the missile ships afloat, the missiles aboard by launcher type (TorpsLeft + Reloads of
// mount types 4..6 SSM and 1..3 SAM), the ships with a pending heavy SSM target, the missiles in the air (torpedo list
// entries with Calibre >= 30) and the missile submarines (SubType 4; ready: Downtime <= 0); then Fire at will (this
// PC's values, the agreed ones, its box), the automatic fire's holds and this PC's salvo counters.
static std::string MissileStatText() {
    int ships[2] = {}, stock[2][7] = {}, pending[2] = {}, air[3] = {}, msub[2] = {}, msubReady[2] = {}, nat[2] = {-1, -1};
    void* all = List(E.allShipsVar);
    for (int i = 0; i < dl::ListCount(all); i++) {
        void* sp = dl::ListItem(all, i);
        int side = sp ? dl::At<int>(sp, E.fShipSide) : -1;
        if (side != 0 && side != 1) continue;
        if (nat[side] < 0 && E.fShipNation > 0) nat[side] = dl::At<int>(sp, E.fShipNation);
        bool sunk = E.fSunk > 0 && dl::At<uint8_t>(sp, E.fSunk);
        bool any = false;
        void* ml = MountList(sp);
        for (int k = 0; k < dl::ListCount(ml); k++) {
            void* mt = dl::ListItem(ml, k);
            if (!IsMissileMount(mt)) continue;
            any = true;
            if (!sunk)
                stock[side][dl::At<uint8_t>(mt, E.fMountType)] +=
                    dl::At<int>(mt, E.fTorpsLeft) + (E.fReloads >= 0 ? dl::At<int>(mt, E.fReloads) : 0);
        }
        if (any && !sunk) ships[side]++;
        if (E.fPendingMsl > 0) {
            uint8_t* p = (uint8_t*)sp + E.fPendingMsl;
            if (*(uint32_t*)p || *(uint32_t*)(p + 4)) pending[side]++;
        }
    }
    void* torps = List(E.torpVar);
    for (int i = 0; i < dl::ListCount(torps) && E.fTorpCalibre >= 0 && E.fTorpSide >= 0; i++)
        if (void* t = dl::ListItem(torps, i))
            if (dl::At<int>(t, E.fTorpCalibre) >= 30) {
                int sd = dl::At<int>(t, E.fTorpSide);
                air[sd == 0 || sd == 1 ? sd : 2]++;
            }
    void* subs = List(E.subsVar);
    for (int i = 0; i < dl::ListCount(subs) && E.fSubType > 0; i++)
        if (void* sb = dl::ListItem(subs, i)) {
            int sd = dl::At<int>(sb, E.fSubSide);
            if ((sd != 0 && sd != 1) || dl::At<uint8_t>(sb, E.fSubType) != 4) continue;
            msub[sd]++;
            if (dl::At<int>(sb, E.fSubDowntime) <= 0) msubReady[sd]++;
        }
    auto agreed = [](int s) {
        auto it = g_agreed.find(FK("Mfireatwill#" + std::to_string(s), 0));
        return it == g_agreed.end() ? std::string("-") : it->second == "00" ? std::string("0") : std::string("1");
    };
    void* m = BattleForm();
    void* cb = m && E.fCbFireAtWill > 0 ? dl::At<void*>(m, E.fCbFireAtWill) : nullptr;
    int pendingOrders = 0;
    for (auto& d : PendingDeferred()) pendingOrders += d.kind == "msl";
    std::string out = "ok faw=" + FireAtWillText() + " agreed=" + agreed(0) + agreed(1) +
                      " box=" + (cb && E.fCbState > 0 ? std::to_string(dl::At<uint8_t>(cb, E.fCbState)) : std::string("?")) +
                      " boxEnabled=" + (cb && E.fCtlEnabled > 0 ? std::to_string(dl::At<uint8_t>(cb, E.fCtlEnabled)) : std::string("?")) +
                      " feature=" + std::to_string(g_featFireAtWill ? 1 : 0) + std::to_string(g_featMissile ? 1 : 0) +
                      " calls=" + std::to_string(S.missileCalls) + " held=" + std::to_string(S.mslHeld[0]) + "/" +
                      std::to_string(S.mslHeld[1]) + " heldCalls=" + std::to_string(S.mslHeldCalls[0]) + "/" +
                      std::to_string(S.mslHeldCalls[1]);
    static const char* kTypes[7] = {"", "heavySAM", "mediumSAM", "lightSAM", "heavySSM", "mediumSSM", "lightSSM"};
    for (int s = 0; s < 2; s++) {
        out += " | side" + std::to_string(s) + " ships=" + std::to_string(ships[s]);
        for (int t = 4; t <= 6; t++) out += std::string(" ") + kTypes[t] + "=" + std::to_string(stock[s][t]);
        for (int t = 1; t <= 3; t++) out += std::string(" ") + kTypes[t] + "=" + std::to_string(stock[s][t]);
        out += " pending=" + std::to_string(pending[s]) + " air=" + std::to_string(air[s]) +
               " msub=" + std::to_string(msub[s]) + "/" + std::to_string(msubReady[s]);
    }
    out += " | otherAir=" + std::to_string(air[2]) + " subs=" + std::to_string(dl::ListCount(subs));
    // missiles shot down / decoyed defending each side: the lines ResolveMissileHit writes into the target ship's myHits
    // (" Missile shot down by LSAM.|MSAM.|<adjacent ship>", "... by CIWS.", "... by radar guided MAA.", "... decoyed by
    // countermeasures."; for both sides, unlike the battle's MessageLog)
    {
        static int fHits = dl::Field("TFightingShip", "myHits"), fSLList = dl::Field("TStringList", "FList"),
                   fSLCount = dl::Field("TStringList", "FCount");
        int down[2] = {}, sam[2] = {}, decoy[2] = {};
        for (int i = 0; i < dl::ListCount(all) && fHits > 0 && fSLList > 0 && fSLCount > 0; i++) {
            void* sp = dl::ListItem(all, i);
            int side = sp ? dl::At<int>(sp, E.fShipSide) : -1;
            void* hl = (side == 0 || side == 1) ? dl::At<void*>(sp, fHits) : nullptr;
            if (!hl || !dl::IsInstanceOf(hl, "TStringList")) continue;
            int n = dl::At<int>(hl, fSLCount);
            void** items = dl::At<void**>(hl, fSLList);
            for (int k = 0; k < n && k < 5000 && items; k++) {
                std::wstring s = dl::ReadUStr(items[k * 2]);
                if (s.find(L"shot down") != std::wstring::npos) {
                    down[side]++;
                    if (s.find(L"SAM") != std::wstring::npos) sam[side]++;
                } else if (s.find(L"decoyed") != std::wstring::npos) {
                    decoy[side]++;
                }
            }
        }
        out += " | intercepted shotDown=" + std::to_string(down[0]) + "/" + std::to_string(down[1]) + " bySAM=" +
               std::to_string(sam[0]) + "/" + std::to_string(sam[1]) + " decoyed=" + std::to_string(decoy[0]) + "/" +
               std::to_string(decoy[1]);
    }
    // the sides' nations (their first ship's) and the nation record's missile values: +0x8c medium SAM (the missile
    // dialog's SAM question), +0x90 medium SAM against missiles (DoesSAMvsMissile, "Improved medium SAM"), +0xc8, the
    // fire-at-will gate value
    out += " | nations";
    for (int s = 0; s < 2; s++) {
        out += " " + std::to_string(s) + ":" + std::to_string(nat[s]);
        if (E.nations && E.nationStride > 0 && E.nationGate > 0 && nat[s] >= 0 && nat[s] < 64) {
            const uint8_t* r = E.nations + nat[s] * E.nationStride;
            out += ":msam" + std::to_string(r[0x8c]) + ":antimissile" + std::to_string(r[0x90]) + ":c8=" +
                   std::to_string(r[0xc8]) + ":gate" + std::to_string(*(const int*)(r + E.nationGate));
        }
    }
    out += std::string(" | orders=") + std::to_string(S.mslOrders) + " pendingOrders=" + std::to_string(pendingOrders) +
           " replays=" + std::to_string(S.mslReplays) + " fired=" + std::to_string(S.mslFired) +
           " skipped=" + std::to_string(S.mslSkipped) + " ssmRefused=" + std::to_string(S.ssmRefused) +
           " subsUnchecked=" + std::to_string(S.mslSubsUnchecked) + " last=" + (g_mslLast.empty() ? "-" : g_mslLast);
    return out;
}

// `ls fireatwill [0|1]` (live battle): this PC's side's Fire at will as a click sets it (the box, then its OnClick, the
// hooked handler), and the guard's look at once: `ok mine= faw= box= batch=#<n>|none`.
static std::string FireAtWillCommand(const std::vector<std::string>& a) {
    if (!g_featFireAtWill) return "error Fire at will per side is not available in this live battle (see the log)";
    void* m = BattleForm();
    void* cb = m && E.fCbFireAtWill > 0 ? dl::At<void*>(m, E.fCbFireAtWill) : nullptr;
    if (!cb) return "error no Fire at will box";
    std::string batch = "none";
    if (a.size() > 2) {
        std::string err;
        if (!CanOrderNow(err)) return "error " + err;
        bool on = a[2] != "0";
        if ((dl::At<uint8_t>(cb, E.fCbState) == 1) != on) {
            dl::At<uint8_t>(cb, E.fCbState) = on ? 1 : 0;
            HWND h = game::FormHwnd(cb);
            if (h) SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            static void* handler = dl::Method("TfrmMain", "cbShowBaseFlagsClick");
            if (!CallHandler(handler, m, cb)) return "error the box's click handler failed";
        }
        int seq = S.mySeq;
        DetectEdits("fire at will");
        if (S.mySeq != seq) batch = "#" + std::to_string(S.mySeq);
    }
    return "ok mine=" + std::to_string(S.mySide == 0 || S.mySide == 1 ? g_faw[S.mySide] : -1) + " faw=" + FireAtWillText() +
           " box=" + std::to_string(dl::At<uint8_t>(cb, E.fCbState)) + " batch=" + batch;
}

// `ls missiles <division> [target|first] [1|2|4] [all]` (live battle, async: the game's own message boxes are answered
// quietly here): a player's path to a missile salvo through the missile dialog's own routines, without showing it.
// Manual fire's three fills (sbFireSalvoClick without its ShowModal), the target (its index in the dialog's target list,
// first = 0) through lbMissileTargetsClick (the shooter list for that target), the shooter list's check boxes: this
// PC's ships of that division (all: every shooter listed, missile submarines included), then Fire one / half / all (the
// hooked handler: the gate, the submarines left out, FireMissiles, FireMissileMount's hook per launcher).
static std::string MissilesCommand(const std::vector<std::string>& a) {
    auto arg = [&](size_t i, const std::string& def) { return i < a.size() && !a[i].empty() ? a[i] : def; };
    if (!g_featMissile) return "error manual missile salvoes are not available in this live battle (see the log)";
    if (E.lbSetIdxSlot <= 0 || E.itemsObjSlot <= 0 || E.fListItems <= 0 || !E.mslTargetsClick || !E.mslFireOne ||
        !E.mslFireHalf || !E.mslFireAll || E.fMslTargets <= 0)
        return "error the dialog's pieces for the bridge were not found";
    void* m = BattleForm();
    void* div = dl::ListItem(List(E.divisionsVar), atoi(arg(2, "-1").c_str()));
    if (!m || !IsDivision(div)) return "error division";
    if (dl::At<int>(div, E.fDivSide) != S.mySide) return "error that division is not yours";
    std::string err;
    if (!MissileOrdersOpen(err)) return "error " + err;
    void* dlg = E.mslDlgVar ? *E.mslDlgVar : nullptr;
    if (!dlg || !dl::IsInstanceOf(dlg, "TdlgMissileFire")) return "error no missile dialog";
    int mode = atoi(arg(4, "4").c_str());
    if (mode != 1 && mode != 2 && mode != 4) return "error mode (1, 2 or 4)";
    bool every = arg(5, "") == "all";
    uint8_t empty[12] = {};
    bool ok1 = false, ok2 = false, ok3 = false;
    SafeCall(E.mslFillTargets, U(dlg), 0, 0, 0, nullptr, &ok1);
    SafeCall(E.mslFillShooters, U(dlg), U(empty), 0, 0, nullptr, &ok2);  // FillShooterList(EmptyPoint, nil)
    SafeCall(E.mslTechText, U(dlg), 0, 0, 0, nullptr, &ok3);
    if (!ok1 || !ok2 || !ok3) return "error the dialog's lists could not be filled";
    void* lbT = dl::At<void*>(dlg, E.fMslTargets);
    int nt = LbCount(lbT);
    std::string targets = std::to_string(nt) + ":";
    for (int i = 0; i < nt && targets.size() < 1500; i++) targets += " " + std::to_string(i) + " " + LbText(lbT, i) + ";";
    std::string tw = arg(3, "first");
    int ti = tw == "first" ? 0 : atoi(tw.c_str());
    if (ti < 0 || ti >= nt) return "ok queued=0 (no target " + tw + ") | targets " + targets;
    if (!CtlSelect(lbT, ti) || !CallHandler(E.mslTargetsClick, dlg, lbT)) return "error target";
    void* items = MslItems(dlg);
    int ni = MslItemCount(items), checked = 0;
    std::string shooters;
    for (int i = 0; i < ni && i < 200; i++) {
        void* it = MslItem(items, i);
        void* data = MslItemData(it);
        bool want = data && (every || (IsFightingShip(data) && IndexIn(dl::At<void*>(div, E.fShips), data) >= 0));
        MslItemCheck(it, want);
        checked += want;
        shooters += std::to_string(i) + ":" + (data ? W2U(ObjName(data)) : std::string("?")) + ":" + (want ? "1" : "0") + "; ";
    }
    if (!checked) return "ok queued=0 target=" + std::to_string(ti) + " (no shooter of that division listed) | shooters: " +
                         shooters + "| targets " + targets;
    static int fOne = dl::Field("TdlgMissileFire", "bnFireOne"), fHalf = dl::Field("TdlgMissileFire", "bnfireHalf"),
               fAll = dl::Field("TdlgMissileFire", "bnFireAll");
    void* fn = mode == 1 ? E.mslFireOne : mode == 2 ? E.mslFireHalf : E.mslFireAll;
    int fb = mode == 1 ? fOne : mode == 2 ? fHalf : fAll;
    size_t before = PendingDeferred().size();
    g_mslLast = "the fire routine was not called";
    g_lastQuietBox.clear();
    g_quiet++;
    bool ok = CallHandler(fn, dlg, fb > 0 ? dl::At<void*>(dlg, fb) : nullptr);
    g_quiet--;
    auto v = PendingDeferred();
    int queued = 0, missiles = 0;
    for (size_t i = before; i < v.size(); i++)
        if (v[i].kind == "msl") {
            queued++;
            missiles += v[i].args.size() >= 7 ? atoi(v[i].args[6].c_str()) : 0;
        }
    return std::string(ok ? "ok" : "error the fire handler failed") + " queued=" + std::to_string(queued) +
           " missiles=" + std::to_string(missiles) + " target=" + std::to_string(ti) + " | result: " + g_mslLast +
           " | summary: " + (g_lastQuietBox.empty() ? "-" : g_lastQuietBox) + " | shooters: " + shooters +
           "| targets " + targets;
}

// ---- Air strikes on the bridge (engine level 7) ----

// TGlobe's units: the game formats a TPointLL coordinate as degrees by dividing by 5,400,000 (0xa8c3b9 in 1.01.44:
// imul eax, [ebp-8], 5265C0h).
static const double kGuPerDegree = 5400000.0;

static void* AirById(int id) {
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* af = dl::ListItem(air, i))
            if (dl::At<int>(af, E.fAfId) == id) return af;
    return nullptr;
}

static std::string PointText(const void* p) {  // "<lat>,<lon>" in degrees
    char b[48];
    snprintf(b, sizeof(b), "%.3f,%.3f", ((const int*)p)[1] / kGuPerDegree, ((const int*)p)[0] / kGuPerDegree);
    return b;
}

static int MissionArg(const std::string& s) {  // a TAirMission by number or name
    static const char* names[] = {"none", "recon", "cap", "naval", "ground", "escort", "support", "special"};
    for (int i = 0; i < 8; i++)
        if (s == names[i]) return i;
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    return !s.empty() && end && *end == 0 && v >= 0 && v < 8 ? (int)v : -1;
}

// `ls airlaunch <source AFId> <number> <mission> <lat> <lon> | @<ship> [loadout] [spot] [state]`: LaunchSomeAircraft
// from outside the simulation, as the strike dialog calls it (the same action path; none of the dialog's own
// post-processing): mission by number (TAirMission) or name, the target in degrees or a ship's location ("@<division
// index>.<ship index>" or "@<ship list index>"), loadout (default: the source's, 1 if none), spot now 0|1, state 6
// (Launch: asFlying, default) or 2 (Ready: asReady).
static std::string AirLaunchCommand(const std::vector<std::string>& a) {
    auto arg = [&](size_t i, const std::string& def = "") { return i < a.size() ? a[i] : def; };
    std::string err;
    if (!StrikeReady(err)) return "error " + err;
    void* src = AirById(atoi(arg(2, "-1").c_str()));
    if (!src) return "error no formation with AFId " + arg(2);
    if (dl::At<int>(src, E.fAfSide) != S.mySide) return "error that formation is not this player's";
    int n = atoi(arg(3, "0").c_str()), mission = MissionArg(arg(4));
    if (n <= 0 || mission < 0) return "error usage: ls airlaunch <AFId> <number> <mission> <lat> <lon>|@<ship> [loadout] [spot] [state]";
    int target[3] = {0, 0, 0};
    size_t next = 6;
    if (!arg(5).empty() && arg(5)[0] == '@') {
        static int fLoc = dl::Field("TFightingShip", "ShipLocation");
        void* ship = ShipArg(arg(5).substr(1));
        if (!ship || fLoc < 0) return "error ship";
        memcpy(target, (uint8_t*)ship + fLoc, 8);
    } else {
        target[1] = (int)std::lround(atof(arg(5, "0").c_str()) * kGuPerDegree);
        target[0] = (int)std::lround(atof(arg(6, "0").c_str()) * kGuPerDegree);
        next = 7;
    }
    int loadout = a.size() > next ? atoi(a[next].c_str()) : dl::At<int>(src, E.fAfLoadout);
    if (loadout <= 0) loadout = 1;  // (as the dialog: "if Loadout = 0 then Loadout := 1")
    int spot = a.size() > next + 1 ? atoi(a[next + 1].c_str()) : 0;
    int state = a.size() > next + 2 ? atoi(a[next + 2].c_str()) : 6;
    if (state != 2 && state != 6) return "error state: 6 (launch) or 2 (ready)";
    int seq = S.mySeq, launches = S.launches;
    uint32_t st[5] = {U(target), 0, (uint32_t)state, (uint32_t)loadout, (uint32_t)(spot ? 1 : 0)};
    bool ok = false;
    void* nf = (void*)(uintptr_t)SafeCall(E.launchFn, U(src), (uint32_t)n, (uint32_t)mission, 5, st, &ok);
    if (!ok) return "error an exception in the game's routine";
    if (!nf) return "ok none (no undamaged aircraft to launch)";
    return "ok afid=" + std::to_string(dl::At<int>(nf, E.fAfId)) + " name=" +
           W2U(dl::ReadUStr(dl::At<void*>(nf, E.fAfName))) + " recorded=" + (S.launches > launches ? "1" : "0") +
           " batch=" + (S.mySeq > seq ? "#" + std::to_string(S.mySeq) : std::string("none"));
}

// `ls airlist`: the air formations in list order and the launch counters.
static std::string AirListText() {
    void* air = List(E.airVar);
    int n = dl::ListCount(air);
    auto ref = [](void* p) { return p ? std::to_string(dl::At<int>(p, E.fAfId)) : std::string("-"); };
    auto i32 = [](void* p, int off) { return off >= 0 ? std::to_string(dl::At<int>(p, off)) : std::string("?"); };
    auto u8 = [](void* p, int off) { return off >= 0 ? std::to_string((int)dl::At<uint8_t>(p, off)) : std::string("?"); };
    std::string out = "ok n=" + std::to_string(n) + " mark=" + std::to_string(g_airMark) +
                      " afid=" + (E.afIdCounter ? std::to_string(*E.afIdCounter) : std::string("?")) +
                      " scout=" + (E.scoutCount ? std::to_string(*E.scoutCount) : std::string("?")) +
                      " launches=" + std::to_string(S.launches) + " replays=" + std::to_string(S.launchReplays) +
                      " failed=" + std::to_string(S.launchFailed) + " sorts=" + std::to_string(S.launchSorts) +
                      " next=" + std::to_string(S.launchK[0]) + "/" + std::to_string(S.launchK[1]) +
                      " feature=" + std::to_string(g_featStrike ? 1 : 0) + " auto=" + std::to_string(S.autoStrike ? 1 : 0) +
                      " fresh=" + std::to_string(g_strikeListFresh ? 1 : 0) + " |";
    for (int i = 0; i < n && out.size() < 60000; i++) {
        void* af = dl::ListItem(air, i);
        if (!af) continue;
        out += " " + i32(af, E.fAfId) + ":" + i32(af, E.fAfSide) + ":m" + u8(af, E.fAfMission) + ":s" + u8(af, E.fAfStatus) +
               ":o" + u8(af, E.fAfOrdered) + ":" + i32(af, E.fAfNumber) + "/" + i32(af, E.fAfLoaded) + ":l" +
               i32(af, E.fAfLoadout) + ":p" + ref(E.fAfParent >= 0 ? dl::At<void*>(af, E.fAfParent) : nullptr) + ":L" +
               ref(E.fAfLead >= 0 ? dl::At<void*>(af, E.fAfLead) : nullptr) + ":g" + i32(af, E.fAfGroup) + ":c" +
               u8(af, E.fAfConfirm) + ":t" + (E.fAfTarget >= 0 ? PointText((uint8_t*)af + E.fAfTarget) : "?") + ":at" +
               (E.fAfLocation >= 0 ? PointText((uint8_t*)af + E.fAfLocation) : "?") + ":" +
               W2U(dl::ReadUStr(dl::At<void*>(af, E.fAfName))) + ";";
    }
    return out;
}

// `ls airbase` (tests, read-only): each formation's home base (ScenHomeBase) and whether it is an airfield, then every
// base ship once with its air weapons (AirTorpedoes / AirToSurfaceMissiles, where Stand down returns them).
static std::string AirBaseText() {
    if (E.fAfHome < 0 || E.fAfId < 0 || E.fAfSide < 0) return "error the air formation fields were not found";
    void* air = List(E.airVar);
    void* all = List(E.allShipsVar);
    std::vector<void*> bases;
    auto airfield = [](void* sp) {
        bool ok = false;
        return sp && E.isAirfield && (SafeCall(E.isAirfield, U(sp), 0, 0, 0, nullptr, &ok) & 0xFF) && ok;
    };
    std::string out = "ok |";
    for (int i = 0; i < dl::ListCount(air) && out.size() < 50000; i++) {
        void* af = dl::ListItem(air, i);
        if (!af) continue;
        void* home = dl::At<void*>(af, E.fAfHome);
        if (home && std::find(bases.begin(), bases.end(), home) == bases.end()) bases.push_back(home);
        out += " " + std::to_string(dl::At<int>(af, E.fAfId)) + ":" + std::to_string(dl::At<int>(af, E.fAfSide)) + ":" +
               (home ? W2U(ObjName(home)) : std::string("-")) + ":" + (airfield(home) ? "field" : "ship") + ";";
    }
    out += " | bases";
    for (void* sp : bases)
        out += " " + std::to_string(IndexIn(all, sp)) + ":" + W2U(ObjName(sp)) + ":" + (airfield(sp) ? "field" : "ship") +
               ":torps=" + (E.fAirTorps >= 0 ? std::to_string(dl::At<int>(sp, E.fAirTorps)) : std::string("?")) +
               ":asm=" + (E.fAirAsm >= 0 ? std::to_string(dl::At<int>(sp, E.fAirAsm)) : std::string("?")) + ";";
    return out;
}

// `ls strike`: the strike dialog's state, its rows (the formation each lists, selected or not) and the window handles
// a test clicks (BM_CLICK) or selects rows in (LVM_SETITEMSTATE on lv=).
static std::string StrikeText() {
    void* dlg = StrikeDialog();
    if (!dlg) return "error no strike dialog";
    auto hwnd = [&](const char* field) {
        int off = dl::Field("TdlgStrike", field);
        void* c = off > 0 ? dl::At<void*>(dlg, off) : nullptr;
        char b[24];
        snprintf(b, sizeof(b), "%x", (unsigned)(uintptr_t)(c ? game::FormHwnd(c) : nullptr));
        return std::string(b);
    };
    std::string err;
    bool ready = StrikeReady(err, false);
    void* div = E.fStrikeDiv > 0 ? dl::At<void*>(dlg, E.fStrikeDiv) : nullptr;
    int fTarget = dl::Field("TdlgStrike", "aTargetLocation");
    char dh[24];
    snprintf(dh, sizeof(dh), "%x", (unsigned)(uintptr_t)game::FormHwnd(dlg));
    std::string out = "ok visible=" + std::to_string(game::FormVisible(dlg) ? 1 : 0) +
                      " division=" + (div && IsDivision(div) ? std::to_string(IndexIn(List(E.divisionsVar), div)) + ":" +
                                                                   W2U(ObjName(div))
                                                             : std::string("-")) +
                      " target=" + (fTarget > 0 ? PointText((uint8_t*)dlg + fTarget) : "?") +
                      " fresh=" + std::to_string(g_strikeListFresh ? 1 : 0) + " ready=" + (ready ? "1" : "0 (" + err + ")") +
                      " hwnd=" + dh + " lv=" + hwnd("lvAirFormations") + " launch=" + hwnd("bnLaunch") +
                      " ready=" + hwnd("bnReady") + " standdown=" + hwnd("bnStandDown") +
                      " autoselect=" + hwnd("bnAutoSelectStrike") + " cancel=" + hwnd("bnCancel") + " rows=";
    void* lv = E.fStrikeLv > 0 ? dl::At<void*>(dlg, E.fStrikeLv) : nullptr;
    void* items = lv && E.fLvItems > 0 ? dl::At<void*>(lv, E.fLvItems) : nullptr;
    HWND h = lv ? game::FormHwnd(lv) : nullptr;
    int n = h ? (int)SendMessageW(h, LVM_GETITEMCOUNT, 0, 0) : 0;
    for (int i = 0; i < n && i < 100; i++) {
        bool ok = false;
        void* it = items && E.lvGetItem ? (void*)(uintptr_t)SafeCall(E.lvGetItem, U(items), (uint32_t)i, 0, 0, nullptr, &ok)
                                       : nullptr;
        void* af = ok && it && E.fItemData > 0 ? dl::At<void*>(it, E.fItemData) : nullptr;
        // (a hidden dialog keeps the items of its last fill: after a reload they point to freed formations)
        bool live = af && IndexIn(List(E.airVar), af) >= 0;
        int sel = (int)SendMessageW(h, LVM_GETITEMSTATE, (WPARAM)i, LVIS_SELECTED) & LVIS_SELECTED ? 1 : 0;
        out += std::to_string(i) + ":" + (live ? std::to_string(dl::At<int>(af, E.fAfId)) : std::string("?")) + ":" +
               std::to_string(sel) + ";";
    }
    return out;
}

// `ls strikedlg [report index]` (async): the strike dialog as the battle screen's "Order an airstrike" opens it (with
// its pause request), or for a report of the reports list (its Items.Objects[i], a TReport) as the list's "Launch strike".
static std::string StrikeDialogCommand(const std::vector<std::string>& a) {
    if (!g_featStrike) return "error air strikes are not available in this live battle (see the log)";
    void* m = BattleForm();
    if (!m || !StrikeDialog()) return "error no battle";
    if (a.size() <= 2) {
        if (S.runLeft > 0 || S.minuteRunning) ClockRequest("pause", 0);
        OpenStrikeDialog(nullptr);
        return "ok " + StrikeText();
    }
    void* lb = E.fLbReports > 0 ? dl::At<void*>(m, E.fLbReports) : nullptr;
    void* rep = LbObject(lb, atoi(a[2].c_str()));
    if (!rep || !dl::IsInstanceOf(rep, "TReport")) return "error no report " + a[2] + " in the reports list";
    OpenStrikeDialog(rep);
    return "ok " + StrikeText();
}

// Bridge `ls ...` (docs/TESTING.md). The live-battle rules (engine level 2):
//   ls level            this PC's live-battle engine level and mod version, and the other PC's level once a live battle
//                       told it (B: the start, A: "ready"): `ok level=<level> mod=<version> peer=<level|->`
//   ls simside          the simulation's PlayerSide, the game's PlayerSide now, this PC's view side and the context depths
//   ls airstat          the air formations by side, mission (m<side>=8 counts) and status (st<side>=15 counts), the ones
//                       still to confirm their target, and since this battle started: the formations the computer's
//                       strike routine made per side (aistrike=<side 0>/<side 1>, naval attack: ainaval=), its calls,
//                       the confirmations cleared, the search re-plans skipped, the missile fire calls, the forces' air
//                       searches and land-CAP checks run with the live rules (searchCalls=, landcapCalls=), and which rule
//                       hooks are in place (rules=<strikes><search plan><missile fire><missile panel><land CAP side>
//                       <scout question>)
//   ls fastest [0|1]    (live battle) ask the clock for Fastest available off/on, as the button does; without an argument
//                       its state: `ok fastest=<applied> down=<button> want=<A: next release> announced=<B: A's last>`
// Force orders and Save (engine level 3):
//   ls forces           the forces: `ok <i>:<side>:<name>:divs=<division indexes in force order>:lead=<division index
//                       of the first division in that order with ForceLeadDivision and ships, or ->:changed=<HasChangedLead>
//                       :turnaway=<TimeToTurnAway>:ai=<AIControlled>;...`
//   ls forces div       the divisions' order of battle: `ok <i>:<side>:<name>:force=<force index|->:admin=<Admin>
//                       :lead=<LeadDivision's index|->:role=<Role>:flead=<ForceLeadDivision>:ai=<AIControlled>;...`
//   ls chforce <div> <force>    (live battle) a deferred change of force of this PC's division (as the ship tree's
//                       "Change force" menu after its question)
//   ls lead <force> <div>       (live battle) a deferred change of lead division (as Force orders > Change lead > OK)
//   ls turnaway <force> <div>   (live battle) a deferred battle turn-away with that new lead (as Force orders > Battle
//                       turn away > OK)
//   ls pending          (live battle) this PC's deferred actions no release has carried yet:
//                       `ok n=<count> #<batch>:<kind>:<minute>:<object>><object>;...`
//   ls save             (live battle) as the Save button: a clock request; A saves at a boundary (the host's own battle)
//                       or says that the host keeps the battle
// Auto-run per player (engine level 4):
//   ls autorun on|home|off      (live battle) this PC's side to the computer (home: and all its forces to the closest
//                       friendly base) or back, as the button after its question: a deferred action
//   ls autorun          (live battle) `ok auto0=<side 0 on auto-run> auto1= mine=<this PC's side incl. its pending
//                       orders> announced=<B: A's last a0a1> sim=<the simulation's PlayerSide> clock=<A: the continuous
//                       run of both sides on auto-run> feature=<the pieces were found>`
// Manual torpedo salvoes (engine level 5):
//   ls mounts <division>.<ship> | <ship list index>     (any battle) the ship's torpedo and missile mounts in
//                       TorpedomountList order: `ok ship= side= sunk= speed=<CurrentSpeed> type=<ShipType>
//                       fired=<FiredMountThisTurn> mounts=<n> <i>:type=<MountType>:pos=<Position>:tubes=:left=<TorpsLeft>
//                       :reload=<ReloadTime>:reloads=:destroyed=:sub=<IsSubmerged>;...`
//   ls torp <division> [ship|-] [target|first] [mount|first]   (live battle, async: the result is logged as
//                       `ls torp -> ...`) the player's path: the division report, Fire torpedoes, the dialog's ship box,
//                       target list and mount list, Fire: `ok queued=<0|1> target=<i> mount=<i> | result: <queued #<batch>
//                       | refused: <why> | the game's own refusal (...)> | solution: <the line fired> | ship=<name>
//                       button=<Fire torpedoes enabled> | solutions: <i> <line> [mount <i>]; ... | targets <n>: <i> <line>;
//                       ...`, or `ok queued=0 target=<i> (no firing solution)...`, or `error <why>`
// Routine air ops and land-based CAP (engine level 6):
//   ls airforce <force> (any battle) `ok force=<i> name= side= cap=<CapLevel> nightcap= left= right= dist= interval=
//                       twophase= repeat= nightsearch= ordered=<SearchPatternOrdered, a digit per byte>
//                       current=<SearchPatternCurrent, the count-downs> flagship=<ForceFlagship|-> feature=<0|1> |
//                       <the ls landcap text>`
//   ls landcap          (any battle) `ok landcap0=<i>:<name>|- landcap1= swapped=<the display swap> mine=<side>
//                       feature= rule=<the airfield-side hook> calls= other= airfields=<i>:<side>:<CappedDivision i|->;...`
//   ls landcap <division>|-     (live battle) as the division report's OK with its land-CAP button down: this PC's own
//                       request (the guard takes it at once): `ok <the ls landcap text> | batch=#<n>|none`
//   ls airops <force> [cap=] [nightcap=] [left=] [right=] [dist=] [interval=] [twophase=] [repeat=] [nightsearch=]
//                       [raw=1]    (live battle) as the routine air ops dialog's OK on this PC's force (raw=1: any
//                       force, tests): `ok <the ls airforce text> | batch=#<n>|none`
// Air strikes (engine level 7):
//   ls airlist          (any battle) `ok n= mark=<the list count the release's sort starts from> afid=<AFID counter>
//                       scout=<ScoutCount> launches= replays= failed= sorts= next=<k side 0>/<k side 1> feature= auto=
//                       fresh=<the strike list is up to date> | <AFId>:<side>:m<Mission>:s<Status>:o<OrderedState>
//                       :<AircraftNumber>/<LoadedNumber>:l<Loadout>:p<parent AFId|->:L<lead AFId|->:g<GroupIndex>
//                       :c<ConfirmTargetLoc>:t<target lat,lon>:at<location lat,lon>:<name>; ...` (list order)
//   ls airbase          (any battle, tests) `ok | <AFId>:<side>:<ScenHomeBase's name>:field|ship; ... | bases <ship list
//                       index>:<name>:field|ship:torps=<AirTorpedoes>:asm=<AirToSurfaceMissiles>; ...`
//   ls airlaunch <AFId> <number> <mission> <lat> <lon>|@<ship> [loadout] [spot] [state]   (live battle, between
//                       minutes) LaunchSomeAircraft as the strike dialog calls it (an immediate action): `ok afid=<new>
//                       name= recorded=<0|1> batch=#<n>|none`, `ok none (...)` or `error <why>`
//   ls strike           (any battle) the strike dialog: `ok visible= division=<i>:<name>|- target=<lat,lon> fresh=
//                       ready=<1|0 (why)> hwnd= lv= launch= ready= standdown= autoselect= cancel= rows=<row>:<AFId>:<selected>;...`
//   ls strikedlg [report]   (live battle, async: logged as `ls strikedlg -> ...`) opens it as "Order an airstrike" (with
//                       its pause request) or for a report of the reports list as its "Launch strike": `ok <the ls strike text>`
// Missiles (engine level 9):
//   ls missilestat      (any battle) `ok faw=<this PC's g_faw: side 0, side 1> agreed=<the guard's> box=<the Fire at will
//                       box's FState> boxEnabled= feature=<fire at will per side><manual salvoes> calls=<automatic fire
//                       calls> held=<divisions held in the last call, side 0/1> heldCalls= | side<s> ships=<missile ships
//                       afloat> heavySSM= mediumSSM= lightSSM= heavySAM= mediumSAM= lightSAM=<missiles aboard: TorpsLeft +
//                       Reloads> pending=<ships with a PendingMissileTarget> air=<missiles in the air> msub=<missile
//                       submarines>/<ready> | ... | otherAir= subs= | orders= pendingOrders= replays= fired= skipped=
//                       ssmRefused= subsUnchecked= last=<this PC's last salvo order>`
//   ls fireatwill [0|1] (live battle) this PC's side's Fire at will through its box and click handler, then the guard:
//                       `ok mine= faw= box= batch=#<n>|none`
//   ls missiles <division> [target|first] [1|2|4] [all]   (live battle, async: logged as `ls missiles -> ...`) the
//                       missile dialog's own routines without showing it (its boxes answered quietly): `ok queued=<orders>
//                       missiles=<predicted> target=<i> | result: <queued #<batch> (n) | nothing to fire (<why>) |
//                       refused: <why>> | summary: <the dialog's summary box> | shooters: <i>:<name>:<checked>; ... |
//                       targets <n>: <i> <line>; ...`
std::string Debug(const std::vector<std::string>& a) {
    auto arg = [&](size_t i, const std::string& def = "") { return i < a.size() ? a[i] : def; };
    std::string c = arg(1, "status");
    if (c == "status") return Status();
    if (c == "resolve") return Available() ? "ok" : "error " + Missing();
    if (c == "level")
        return "ok level=" + std::to_string(MP_LS_LEVEL) + " mod=" MP_MOD_VERSION " peer=" +
               (S.peerLevel >= 0 ? std::to_string(S.peerLevel) : std::string("-")) +
               " announced=" + std::to_string(AnnouncedLsLevel());
    if (c == "simside")
        return "ok sim=" + std::to_string(g_simPlayerSide) +
               " playerSide=" + std::to_string(E.playerSideVar ? *E.playerSideVar : -1) +
               " view=" + std::to_string(g_viewSide) + " viewOn=" + std::to_string(g_viewOn ? 1 : 0) +
               " simDepth=" + std::to_string(g_sim) + " uiDepth=" + std::to_string(g_ui);
    if (c == "airstat") {
        if (!BattleForm()) return "error no battle";
        if (E.fAfSide < 0) return "error the air formation fields were not found";
        return AirStatText();
    }
    if (c == "airlist") {
        if (!Resolve() || !BattleForm()) return "error no battle";
        if (E.fAfId < 0 || E.fAfSide < 0) return "error the air formation fields were not found";
        return AirListText();
    }
    if (c == "strike") {
        if (!Resolve() || !BattleForm()) return "error no battle";
        return StrikeText();
    }
    if (c == "airbase") {  // ls airbase: the formations' home bases and the bases' air weapons (tests, read-only)
        if (!Resolve() || !BattleForm()) return "error no battle";
        return AirBaseText();
    }
    if (c == "hash") {
        if (!BattleForm()) return "error no battle";
        CanonBegin();
        std::string h = HashLine("ls hash");
        CanonEnd();
        return "ok " + h;
    }
    if (c == "forces") return ForcesText(arg(2) == "div");
    if (c == "airforce") {  // ls airforce <force index>: its routine air ops settings and the land-CAP requests
        if (!Resolve() || !BattleForm()) return "error no battle";
        return AirForceText(dl::ListItem(List(E.forcesVar), atoi(arg(2, "-1").c_str())));
    }
    if (c == "landcap" && a.size() <= 2) {  // ls landcap: the land-CAP requests (`ls landcap <division>|-` sets this one's)
        if (!Resolve() || !BattleForm()) return "error no battle";
        return "ok " + LandCapText();
    }
    if (c == "mounts") {  // ls mounts <division>.<ship> | <ship list index>: the ship's torpedo mounts
        if (!Resolve() || !BattleForm()) return "error no battle";
        return MountsText(ShipArg(arg(2)));
    }
    if (c == "missilestat") {  // ls missilestat: missiles per side, Fire at will, the salvo counters (engine level 9)
        if (!Resolve() || !BattleForm()) return "error no battle";
        return MissileStatText();
    }
    if (c == "timeout") {  // ls timeout <boundary s> [lost s] [load s] [heartbeat s] [rejoin s]
        if (a.size() > 2) g_tBoundary = (DWORD)atoi(arg(2).c_str()) * 1000;
        if (a.size() > 3) g_tLost = (DWORD)atoi(arg(3).c_str()) * 1000;
        if (a.size() > 4) g_tLoad = (DWORD)atoi(arg(4).c_str()) * 1000;
        if (a.size() > 5) g_tHb = (DWORD)atoi(arg(5).c_str()) * 1000;
        if (a.size() > 6) g_tRejoin = (DWORD)atoi(arg(6).c_str()) * 1000;
        return "ok boundary=" + std::to_string(g_tBoundary / 1000) + " lost=" + std::to_string(g_tLost / 1000) +
               " load=" + std::to_string(g_tLoad / 1000) + " hb=" + std::to_string(g_tHb / 1000) +
               " rejoin=" + std::to_string(g_tRejoin / 1000);
    }
    if (c == "rejoinable") {  // A: the live battle dropped here that its player can rejoin (engine level 8)
        RejoinInfo ri;
        bool ok = Rejoinable(ri);
        return "ok on=" + std::to_string(ok ? 1 : 0) + " bid=" + std::to_string(ri.bid) + " opp=" +
               std::to_string(ri.opp) + " peer=" + (ri.peerName.empty() ? std::string("-") : ri.peerName) +
               " side=1 dropMinute=" + std::to_string(ri.dropMinute) + " minute=" + std::to_string(ri.minute) +
               " age=" + std::to_string(ri.ageSec) + " epoch=" + std::to_string(g_rj.on ? g_rj.epoch : -1) +
               " pending=" + std::to_string(ri.pending ? 1 : 0) +
               " why=" + (ri.why.empty() ? std::string("-") : ri.why);
    }
    if (c == "resumable") {  // ls resumable: a live battle resumed from the host's save (phase 5b), the slot's marker
        RejoinInfo ri;
        bool ok = Rejoinable(ri);
        int slot = game::CurrentSlot();
        std::string marker = "-";
        if (slot >= 1 && slot <= 49) {
            std::string text;
            if (ReadFileBytes(MarkerPath(slot), text)) {
                KV mk = KVDecode(text);
                std::string sac = SacHash(slot);
                marker = std::string(!sac.empty() && KVStr(mk, "sac") == sac ? "match" : "stale") + ":bid" +
                         KVStr(mk, "bid", "?") + ":epoch" + KVStr(mk, "epoch", "?") + ":min" +
                         KVStr(mk, "minute", "?") + ":faw" + KVStr(mk, "faw", "?") + ":slotmode" +
                         KVStr(mk, "slotmode", "?");
            } else {
                marker = "none";
            }
        }
        return "ok on=" + std::to_string(ok && ri.resumed ? 1 : 0) + " held=" + std::to_string(ok && ri.held ? 1 : 0) +
               " rejoinable=" + std::to_string(ok ? 1 : 0) + " bid=" + std::to_string(ri.bid) +
               " epoch=" + std::to_string(ri.epoch) + " opp=" + std::to_string(ri.opp) +
               " slot=" + std::to_string(ri.slot) + " slotMode=" + std::to_string(ri.slotMode ? 1 : 0) +
               " serial=" + std::to_string(ri.serial) + " loadedAt=" + std::to_string(ri.dropMinute) +
               " minute=" + std::to_string(ri.minute) + " age=" + std::to_string(ri.ageSec) +
               " pending=" + std::to_string(ri.pending ? 1 : 0) + " marker=" + marker +
               " peer=" + (ri.peerName.empty() ? std::string("-") : ri.peerName) +
               " why=" + (ri.why.empty() ? std::string("-") : ri.why);
    }
    if (c == "resume") {  // ls resume run: what the host's Run does to a held resumed battle (without starting a run)
        if (arg(2) != "run") return "error usage: ls resume run";
        if (!ResumeHeld()) return "error no resumed battle is held";
        ReleaseResumeHold("ls resume run");
        return "ok released";
    }
    if (c == "autostart") {  // ls autostart 0|1|2: the answer to the game's next "Select autostart point." (tests)
        g_testAutostart = std::max(0, std::min(2, atoi(arg(2, "2").c_str())));
        if (g_testAutostart && !EnsureHooks()) return "error hooks";
        return "ok autostart=" + std::to_string(g_testAutostart);
    }
    if (c == "mute") {  // ls mute 0|1: this PC neither sends nor receives lockstep messages (a silent player)
        g_muted = arg(2, "1") != "0";
        Log("lockstep: TEST mute %d", g_muted ? 1 : 0);
        return "ok muted=" + std::to_string(g_muted ? 1 : 0);
    }
    if (c == "leakcheck" && arg(2, "") == "ident") {  // ls leakcheck ident 0|1: the identification in the leak check
        g_leakIdent = arg(3, "1") != "0";
        g_exitHashOk = false;
        return "ok leakident=" + std::to_string(g_leakIdent ? 1 : 0);
    }
    if (c == "testident") {  // ls testident <ship index> <type> (TEST ONLY, between minutes, on one PC): a display write
        // of the identification into the simulation's set, as the leak check must catch it (LEAK ... ident)
        void* sp = dl::ListItem(List(E.allShipsVar), atoi(arg(2, "-1").c_str()));
        if (!IsFightingShip(sp)) return "error ship";
        if (g_sim > 0 || S.minuteRunning) return "error not between minutes";
        auto it = g_dual.find(sp);
        if (it == g_dual.end() || !it->second.known || !g_viewOn) return "error no display set in place";
        int was = it->second.simType;
        it->second.simType = it->second.lastSimType = (uint8_t)atoi(arg(3, "3").c_str());
        Log("lockstep: TEST %s's identification in the simulation's set %d -> %d", W2U(ObjName(sp)).c_str(), was,
            it->second.simType);
        return "ok ship=" + W2U(ObjName(sp)) + " was=" + std::to_string(was);
    }
    if (c == "leakcheck") {  // ls leakcheck 0|1: the simulation state may not change between its steps (logs LEAK)
        g_leakCheck = arg(2, "1") != "0";
        g_exitHashOk = false;
        g_leaks = 0;
        return "ok leakcheck=" + std::to_string(g_leakCheck ? 1 : 0);
    }
    if (c == "desyncreport") {  // ls desyncreport: this PC's DESYNC report of its boundary now (tests: the writer)
        if (!S.on || S.st != St::Running || S.minuteRunning) return "error not between minutes of a live battle";
        DesyncReport(S.boundary, "");
        return "ok";
    }
    if (c == "trace") {  // ls trace 0|1: per-minute files of the shared draws' call sites (RTW3MP data folder)
        g_trace = arg(2, "1") != "0";
        return "ok trace=" + std::to_string(g_trace ? 1 : 0);
    }
    if (c == "view") {  // ls view 0|1: the display swap to this PC's side (from the next load)
        g_noView = arg(2, "1") == "0";
        return "ok view=" + std::to_string(g_noView ? 0 : 1);
    }
    if (c == "mapview") {  // ls mapview [on|off|apply]: what this PC's map shows per side (engine level 15)
        std::string v = arg(2, "");
        if (v == "off") g_mapOn = false;
        if (v == "on") g_mapOn = true;
        if (v == "apply" || v == "on") ApplyMapView("probe");
        return MapViewProbe();
    }
    if (c == "testcwload") {  // ls testcwload 0|1 (TEST ONLY): 0 = B loads with its own x87 control word (as 0.4.0)
        if (a.size() > 2) g_cwBeforeLoad = arg(2, "1") != "0";
        return "ok testcwload=" + std::to_string(g_cwBeforeLoad ? 1 : 0) + " cw=" + std::to_string(FpuCw()) +
               " battle=" + std::to_string(S.cw0);
    }
    if (c == "testdatafile") {  // ls testdatafile <name>[;<name>...]|off (TEST ONLY): those data files differ on this copy
        datafiles::SetTestDiff(arg(2, ""));
        return "ok data=" + datafiles::Combined() + " files=" + std::to_string(datafiles::FileCount());
    }
    if (c == "x87") {  // ls x87 [bench|selftest|check|reset|soft 0|1|pert ext|dbl|off [N]]: engine level 16's
                       // transcendental routines (TEST ONLY: `soft` A's choice for the next start, `pert` this copy's nudge)
        std::string sub = arg(2, "");
        if (sub == "reset") x87::ResetCounts();
        if (sub == "soft" && a.size() > 3) g_softWanted = arg(3, "1") != "0";
        if (sub == "pert" && a.size() > 3) x87::SetPerturb(arg(3, "off"), atol(arg(4, "1").c_str()));
        char cw[8];
        snprintf(cw, sizeof(cw), "%04x", FpuCw());
        std::string out = "ok installed=" + std::to_string(x87::Installed() ? 1 : 0) +
                          " soft=" + std::to_string(x87::Soft() ? 1 : 0) + " battleSoft=" + std::to_string(S.softMath ? 1 : 0) +
                          " cpu=" + x87::CpuVendor() + " peerCpu=" + (S.peerCpu.empty() ? "-" : S.peerCpu) + " cw=" + cw +
                          " cwFixes=" + std::to_string(S.cwFixes) + " calls=" + x87::Counts() +
                          " test=" + (x87::TestSwitch().empty() ? "-" : x87::TestSwitch()) +
                          " data=" + (datafiles::Ready() ? datafiles::Combined() : "-") +
                          " files=" + std::to_string(datafiles::FileCount()) +
                          " readyDiag=" + (S.readyDiag.empty() ? "-" : S.readyDiag);
        if (sub == "bench") out += " bench=" + x87::Bench();
        if (sub == "selftest") out += " selftest=" + x87::SelfTest();
        if (sub == "check") out += " check=" + x87::StubCheck();
        for (auto& ch : out)
            if (ch == '\n') ch = ' ';
        return out;
    }
    if (c == "prefs") {  // ls prefs: the battle preferences of this PC's record now (engine level 15: the ones the
                         // simulation uses), and this PC's own kept for after a live battle
        std::string out = "ok";
        std::string h = PrefsHex();
        for (size_t i = 0; i < sizeof(kPrefs) / sizeof(kPrefs[0]); i++)
            out += std::string(" ") + kPrefs[i].key + "=" + std::to_string(PrefValue(h, i));
        if (E.prefs)
            out += " | DarkMode=" + std::to_string(*(int*)(E.prefs + 0x39)) + " Scale=" + std::to_string(E.prefs[0x34e]) +
                   " SoundVolume=" + std::to_string(*(int*)(E.prefs + 0x52)) + " MainFormState=" +
                   std::to_string(*(int*)(E.prefs + 0x65));
        if (!S.savedPrefs.empty()) out += " | after the battle: " + PrefsDiff(h, S.savedPrefs);
        return out;
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
    if (c == "testclock") {  // ls testclock <hours> (TEST ONLY): this PC's next start or resync snapshot as A dates the
        if (a.size() > 2) g_testClockHours = atoi(a[2].c_str());  // battle that much later (also armed before a battle)
        if (g_testClockHours) Log("lockstep: TEST: the next snapshot of this PC as A moves the battle clock by %d hours",
                                  g_testClockHours);
        return "ok testclock=" + std::to_string(g_testClockHours);
    }
    if (c == "testpmt") {  // ls testpmt <all-ships index> (TEST ONLY): A's next start or resync message carries a
        if (a.size() > 2) g_testPmtShip = atoi(a[2].c_str());  // pending missile target for that ship
        if (g_testPmtShip >= 0)
            Log("lockstep: TEST: the next snapshot of this PC as A carries a pending missile target for ship %d",
                g_testPmtShip);
        return "ok testpmt=" + std::to_string(g_testPmtShip);
    }
    if (c == "shiptypes") {  // ls shiptypes [<6 hex digits> | sync | nosync | hash | nohash] (TEST ONLY): the ship-type
        std::string v = arg(2, "");  // flags (BB, BC, carrier exist): read; set this copy's own (between battles, as a
        if (v == "sync" || v == "nosync")  // processed month leaves them); nosync: as B, keep this copy's own for the
            g_typeFlagsSync = v == "sync";  // live battles; nohash: the flags out of the state hash (both: as 0.4.1 RC)
        else if (v == "hash" || v == "nohash")
            g_typeFlagsHash = v == "hash";
        else if (!v.empty() && !SetTypeFlagsHex(v))
            return "error flags (6 hex digits, or the flags were not found)";
        else if (!v.empty())
            Log("lockstep: TEST ship-type flags set to %s", v.c_str());
        return "ok shiptypes=" + TypeFlagsHex() + " sync=" + std::to_string(g_typeFlagsSync ? 1 : 0) +
               " hash=" + std::to_string(g_typeFlagsHash ? 1 : 0);
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
    if (c == "fastest") {
        if (a.size() > 2) return ClockRequest("fastest", atoi(arg(2).c_str()) != 0 ? 1 : 0), "ok";
        return "ok fastest=" + std::to_string(S.fastest) + " down=" + std::to_string(FastestDown(BattleForm()) ? 1 : 0) +
               " want=" + (S.wantFastest != INT_MIN ? std::to_string(S.wantFastest) : std::string("-")) +
               " announced=" + std::to_string(S.clockFastest) + " shared=" + std::to_string(FastestShared() ? 1 : 0);
    }
    if (c == "end") return ClockRequest("end", 0), "ok";
    if (c == "save") return ClockRequest("save", 0), "ok";
    if (c == "pending") {
        auto v = PendingDeferred();
        std::string out = "ok n=" + std::to_string(v.size());
        for (auto& d : v) {
            out += " #" + std::to_string(d.seq) + ":" + d.kind + ":" + std::to_string(d.minute) + ":";
            for (size_t i = 0; i < d.args.size(); i++) out += (i ? ">" : "") + KeyName(d.args[i]);
            out += ";";
        }
        return out;
    }
    if (c == "autorun") {  // ls autorun [on|home|off]: the button's deferred action without its question
        if (a.size() > 2) {
            std::string err;
            return QueueAutoRun(arg(2), err) ? "ok" : "error " + err;
        }
        return "ok auto0=" + std::to_string(S.autoRun[0] ? 1 : 0) + " auto1=" + std::to_string(S.autoRun[1] ? 1 : 0) +
               " mine=" + std::to_string(EffectiveAuto(S.mySide) ? 1 : 0) + " announced=" +
               std::to_string(S.clockAuto[0]) + std::to_string(S.clockAuto[1]) + " sim=" + std::to_string(g_simPlayerSide) +
               " clock=" + std::to_string(S.autoClock ? 1 : 0) + " feature=" + std::to_string(g_featAutoRun ? 1 : 0);
    }
    if (c == "chforce" || c == "lead" || c == "turnaway") {  // the hooks' queue (without the dialogs)
        std::string err;
        if (!CanOrderNow(err)) return "error " + err;
        void* divs = List(E.divisionsVar);
        void* forces = List(E.forcesVar);
        bool ok = c == "chforce"
                      ? QueueChangeForce(dl::ListItem(divs, atoi(arg(2, "-1").c_str())),
                                         dl::ListItem(forces, atoi(arg(3, "-1").c_str())), err)
                      : QueueForceLead(c == "turnaway", dl::ListItem(forces, atoi(arg(2, "-1").c_str())),
                                       dl::ListItem(divs, atoi(arg(3, "-1").c_str())), err);
        return ok ? "ok" : "error " + err;
    }
    if (c == "desync") return S.forceDesync = 1, "ok (this PC's next minute differs)";
    if (c == "testreadydiff")  // TEST ONLY: this PC's next live load differs (A finds that the loaded battles differ)
        return g_testReadyDiff = true, "ok (this PC's next live load differs)";
    if (c == "torp") return TorpCommand(a);  // (async: DebugIsAsync)
    if (c == "fireatwill") return FireAtWillCommand(a);
    if (c == "missiles") return MissilesCommand(a);  // (async: DebugIsAsync)
    if (c == "airops") return AirOpsCommand(a);
    if (c == "landcap") return LandCapCommand(a);
    if (c == "airlaunch") return AirLaunchCommand(a);
    if (c == "strikedlg") return StrikeDialogCommand(a);  // (async: DebugIsAsync)
    if (c == "endsoon") {  // ls endsoon (A): the game itself ends the battle inside the next released minute
        if (!S.isA) return "error A only";
        if (!E.scenLenVar) return "error scenario length not found";
        S.endSoon = true;
        return "ok length=" + std::to_string(*E.scenLenVar) + " minute=" + std::to_string(Minute());
    }
    if (c == "throw") return g_testThrow = true, "ok (an exception in this PC's next MoveAllShips)";
    if (c == "testcurspeed") {  // ls testcurspeed <ship index> <knots> (TEST ONLY, between minutes, given on both PCs
        void* sp = dl::ListItem(List(E.allShipsVar), atoi(arg(2, "-1").c_str()));  // alike): a ship's CurrentSpeed,
        if (!IsFightingShip(sp)) return "error ship";  // e.g. above its maximum, as a worse weather leaves it
        if (g_sim > 0 || S.minuteRunning) return "error not between minutes";
        int off = CurrentSpeedOff(), was = dl::At<int>(sp, off);
        dl::At<int>(sp, off) = atoi(arg(3, "0").c_str());
        if (g_exitHashOk) {  // (the leak check's baseline includes the test's own write; taken with the simulation's
            CanonBegin();    // set of the display-swapped fields in place, as at a step's end)
            LeakCheckExit(g_exitFrame);
            CanonEnd();
        }
        Log("lockstep: TEST %s's current speed %d -> %d", W2U(ObjName(sp)).c_str(), was, dl::At<int>(sp, off));
        return "ok ship=" + W2U(ObjName(sp)) + " was=" + std::to_string(was);
    }
    if (c == "guard") {  // ls guard 0|1: the sighting fields' display/simulation sets (tests: 0 = the old leak)
        g_guardOn = arg(2, "1") != "0";
        return "ok guard=" + std::to_string(g_guardOn ? 1 : 0);
    }
    if (c == "flagsel") {  // ls flagsel <division> [0|1] (TEST ONLY): its map flag selected as a click on the map does,
        static int fFlag = dl::Field("TDivision", "myFlag");  // so that the tool bar's Routine air ops opens the dialog
        void* dv = dl::ListItem(List(E.divisionsVar), atoi(arg(2, "-1").c_str()));  // for that division's force
        void* flag = dv && IsDivision(dv) && fFlag >= 0 ? dl::At<void*>(dv, fFlag) : nullptr;
        if (!flag) return "error division";
        uint16_t& st = dl::At<uint16_t>(flag, 0x28);  // TGlobeObject's state set: 0x40 = selected (getter 0xab2ff0)
        uint16_t was = st;
        st = arg(3, "1") != "0" ? (uint16_t)(st | 0x40) : (uint16_t)(st & ~0x40);
        Log("lockstep: TEST the map flag of %s %s", W2U(ObjName(dv)).c_str(), (st & 0x40) ? "selected" : "not selected");
        return "ok division=" + W2U(ObjName(dv)) + " selected=" + std::to_string((was & 0x40) ? 1 : 0) + "->" +
               std::to_string((st & 0x40) ? 1 : 0);
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
    if (c == "ctl") {  // ls ctl <form class> <field>: a control's state (tests: blocked toggles put back, panel locks)
        std::string cls = arg(2), fld = arg(3);
        void* form = nullptr;
        if (cls == "TfrmMain") {
            form = Frm();
        } else {
            void** v = FormVarOf(cls.c_str());
            form = v ? *v : nullptr;
        }
        if (!form) return "error no " + cls;
        int off = dl::Field(cls.c_str(), fld.c_str());
        void* ctl = off >= 0 ? dl::At<void*>(form, off) : nullptr;
        if (!ctl) return "error no control " + fld;
        static int fEn = dl::Field("TControl", "FEnabled"), fVis = dl::Field("TControl", "FVisible");
        std::string out = std::string("ok class=") + dl::ClassNameOf(ctl);
        if (fEn >= 0) out += " enabled=" + std::to_string(dl::At<uint8_t>(ctl, fEn));
        if (fVis >= 0) out += " visible=" + std::to_string(dl::At<uint8_t>(ctl, fVis));
        if (dl::IsInstanceOf(ctl, "TSpeedButton") && E.fDownOff > 0)
            out += " down=" + std::to_string(dl::At<uint8_t>(ctl, E.fDownOff)) +
                   (E.fGroupOff > 0 ? " group=" + std::to_string(dl::At<int>(ctl, E.fGroupOff)) : "");
        if (E.fCtlHint > 0 && dl::At<void*>(ctl, E.fCtlHint))  // (last: the hint has spaces)
            out += " hint=" + W2U(dl::ReadUStr(dl::At<void*>(ctl, E.fCtlHint)));
        return out;
    }
    if (c == "airwin") {  // ls airwin [show]: the "Show air formations" window and what it lists (tests: fog of war)
        ResolveBattleDialogs();
        void* dlg = g_dialogVars[0] ? *g_dialogVars[0] : nullptr;
        if (!dlg) return "error no air formations window";
        if (arg(2) == "show") {  // as the toolbar button does
            void* m = Frm();
            void* fn = dl::Method("TfrmMain", "sbAircraftClick");
            if (!m || !fn) return "error no battle";
            bool ok = false;
            SafeCall(fn, U(m), 0, 0, 0, nullptr, &ok);
            if (!ok) return "error the window did not open";
        }
        int bySide[3] = {0, 0, 0};
        int fSide = dl::Field("TAirFormation", "Side");
        void* air = List(E.airVar);
        for (int i = 0; i < dl::ListCount(air) && fSide >= 0; i++)
            if (void* af = dl::ListItem(air, i)) {
                int sd = dl::At<int>(af, fSide);
                bySide[sd == 0 ? 0 : sd == 1 ? 1 : 2]++;
            }
        int flv = dl::Field("TdlgShowAirFormations", "lvAirFormations");
        void* lv = flv >= 0 ? dl::At<void*>(dlg, flv) : nullptr;
        HWND h = lv ? game::FormHwnd(lv) : nullptr;
        int n = h ? (int)SendMessageW(h, LVM_GETITEMCOUNT, 0, 0) : -1;
        std::string out = "ok visible=" + std::to_string(game::FormVisible(dlg) ? 1 : 0) + " items=" + std::to_string(n) +
                          " af0=" + std::to_string(bySide[0]) + " af1=" + std::to_string(bySide[1]) +
                          " afx=" + std::to_string(bySide[2]);
        for (int i = 0; i < n && i < 60; i++) {  // the first three columns of each row
            out += " |";
            for (int col = 0; col < 3; col++) {
                wchar_t buf[128] = {};
                LVITEMW it = {};
                it.iSubItem = col;
                it.pszText = buf;
                it.cchTextMax = 127;
                SendMessageW(h, LVM_GETITEMTEXTW, (WPARAM)i, (LPARAM)&it);
                out += (col ? "~" : "") + W2U(buf);
            }
        }
        return out;
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

static std::string StateDumpText() {
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
            if (void* s = dl::ListItem(ships, j)) {
                DumpFields(out, ("  S" + std::to_string(i) + "." + std::to_string(j) + " " + W2U(ObjName(s))).c_str(), s,
                           g_shipFields, sizeof(g_shipFields) / sizeof(g_shipFields[0]));
                void* ml = g_fMountList >= 0 ? dl::At<void*>(s, g_fMountList) : nullptr;
                for (int k = 0; k < dl::ListCount(ml); k++)
                    if (void* mt = dl::ListItem(ml, k))
                        DumpFields(out, ("    M" + std::to_string(k)).c_str(), mt, g_mountFields,
                                   sizeof(g_mountFields) / sizeof(g_mountFields[0]));
            }
    }
    void* torps = List(E.torpVar);
    for (int i = 0; i < dl::ListCount(torps); i++)
        if (void* t = dl::ListItem(torps, i))
            DumpFields(out, ("T" + std::to_string(i)).c_str(), t, g_torpFields, sizeof(g_torpFields) / sizeof(g_torpFields[0]));
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* af = dl::ListItem(air, i))
            DumpFields(out, ("A" + std::to_string(i)).c_str(), af, g_airFields, sizeof(g_airFields) / sizeof(g_airFields[0]));
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* fc = dl::ListItem(forces, i)) {
            std::string tag = "F" + std::to_string(i) + " " + W2U(ObjName(fc)) + " divs=";
            void* fd = E.fFDivs >= 0 ? dl::At<void*>(fc, E.fFDivs) : nullptr;
            for (int j = 0; j < dl::ListCount(fd); j++) tag += (j ? "," : "") + W2U(ObjName(dl::ListItem(fd, j)));
            DumpFields(out, tag.c_str(), fc, g_forceFields, sizeof(g_forceFields) / sizeof(g_forceFields[0]));
        }
    if (E.landCap)
        out += "L landcap0=" + W2U(ObjName(*LandCapSlot(0))) + " landcap1=" + W2U(ObjName(*LandCapSlot(1))) + "\r\n";
    if (E.afIdCounter && E.scoutCount)
        out += "C afid=" + std::to_string(*E.afIdCounter) + " scout=" + std::to_string(*E.scoutCount) + "\r\n";
    void* subs = List(E.subsVar);
    for (int i = 0; i < dl::ListCount(subs); i++)
        if (void* sb = dl::ListItem(subs, i))
            DumpFields(out, ("U" + std::to_string(i)).c_str(), sb, g_subFields, sizeof(g_subFields) / sizeof(g_subFields[0]));
    if (g_featFireAtWill) out += "M faw=" + std::to_string(g_faw[0]) + std::to_string(g_faw[1]) + "\r\n";
    return out;
}

bool DumpState(const std::wstring& path) {
    if (!BattleForm()) return false;
    return WriteFileBytes(path, StateDumpText());
}

// ---------------------------------------------------------------------------------------------
// DESYNC forensics. Every live minute each PC keeps the call sites of its shared draws and, at each boundary, a copy of
// the bytes of the battle's objects (the battle screen, forces, divisions, ships, air formations, torpedoes,
// submarines) in the simulation's set, for that boundary and the one before. On a DESYNC both PCs write them to
// ls_desync_<A|B>_<battle>_<epoch>_<minute>.txt in the RTW3MP data folder (A when it finds it, B on A's "desync"
// message; both stand at that boundary): the first call site where the two lists part names the routine that drew on
// one PC only, and the bytes of the boundary before show inputs the state hash doesn't cover (tools/desync_diff.py).
// Reading only: nothing here draws a number or writes the game's state.
struct FlightMinute {
    int epoch = -1, minute = -1;
    std::vector<uint32_t> sites;
};
static const int kFlightKeep = 4;
static FlightMinute g_flight[kFlightKeep];
static int g_flightNext = 0;
static std::string g_rawPrev, g_rawCur;  // records: tag NUL, size (4 bytes), the bytes
static int g_rawPrevMin = -1, g_rawCurMin = -1;
static std::string g_rawStart;  // the same when the running minute's StartMinute began (after this PC's display ran)
static int g_rawStartMin = -1;

static void RawAppend(std::string& out, const std::string& tag, void* obj) {
    int n = obj ? dl::InstanceSize(obj) : -1;
    if (n <= 0) return;
    uint32_t sz = (uint32_t)n;
    out += tag;
    out.push_back('\0');
    out.append((const char*)&sz, 4);
    out.append((const char*)obj, sz);
}

static std::string RawSnapshot() {
    std::string out;
    out.reserve(320 * 1024);
    if (void* m = BattleForm()) RawAppend(out, "M", m);
    void* forces = List(E.forcesVar);
    for (int i = 0; i < dl::ListCount(forces); i++)
        if (void* fc = dl::ListItem(forces, i)) RawAppend(out, "F" + std::to_string(i) + " " + W2U(ObjName(fc)), fc);
    void* divs = List(E.divisionsVar);
    for (int i = 0; i < dl::ListCount(divs); i++) {
        void* dv = dl::ListItem(divs, i);
        if (!dv) continue;
        RawAppend(out, "D" + std::to_string(i) + " " + W2U(ObjName(dv)), dv);
        void* ships = dl::At<void*>(dv, E.fShips);
        for (int j = 0; j < dl::ListCount(ships); j++)
            if (void* s = dl::ListItem(ships, j))
                RawAppend(out, "S" + std::to_string(i) + "." + std::to_string(j) + " " + W2U(ObjName(s)), s);
    }
    void* air = List(E.airVar);
    for (int i = 0; i < dl::ListCount(air); i++)
        if (void* af = dl::ListItem(air, i))
            RawAppend(out, "A" + std::to_string(i) + (E.fAfId >= 0 ? " AF" + std::to_string(dl::At<int>(af, E.fAfId)) : ""),
                      af);
    void* torps = List(E.torpVar);
    for (int i = 0; i < dl::ListCount(torps); i++)
        if (void* t = dl::ListItem(torps, i)) RawAppend(out, "T" + std::to_string(i), t);
    void* subs = List(E.subsVar);
    for (int i = 0; i < dl::ListCount(subs); i++)
        if (void* sb = dl::ListItem(subs, i)) RawAppend(out, "U" + std::to_string(i), sb);
    return out;
}

// OnBoundary, the simulation's set in place: this minute's draws into the kept ones, the objects' bytes.
static void FlightBoundary() {
    int last = (g_flightNext + kFlightKeep - 1) % kFlightKeep;
    if (!(g_flight[last].epoch == S.epoch && g_flight[last].minute == S.boundary)) {
        FlightMinute& fm = g_flight[g_flightNext];
        g_flightNext = (g_flightNext + 1) % kFlightKeep;
        fm.epoch = S.epoch;
        fm.minute = S.boundary;
        fm.sites.swap(g_flightSeq);
        g_rawPrev.swap(g_rawCur);
        g_rawPrevMin = g_rawCurMin;
    }
    g_flightSeq.clear();
    g_rawCur = RawSnapshot();
    g_rawCurMin = S.boundary;
}

// After a live load: nothing kept from before; the loaded objects' bytes as the boundary before the next minute.
static void FlightReset() {
    g_flightSeq.clear();
    for (auto& f : g_flight) f = FlightMinute();
    g_flightNext = 0;
    g_rawPrev.clear();
    g_rawPrevMin = -1;
    g_rawStart.clear();
    g_rawStartMin = -1;
    CanonBegin();
    g_rawCur = RawSnapshot();
    CanonEnd();
    g_rawCurMin = S.boundary;
}

// StartMinute begins (the simulation's set in place, the orders released): the objects' bytes the minute starts from.
// A field the display wrote between the boundary and this point differs here between the PCs, not at the boundary.
static void FlightMinuteStart() {
    if (!S.on || S.st != St::Running || g_loading) return;
    g_rawStart = RawSnapshot();
    g_rawStartMin = S.boundary + 1;
}

static void RawText(std::string& out, const std::string& raw) {
    static const char* hx = "0123456789abcdef";
    size_t p = 0;
    while (p < raw.size()) {
        size_t z = raw.find('\0', p);
        if (z == std::string::npos || z + 5 > raw.size()) break;
        std::string tag = raw.substr(p, z - p);
        uint32_t sz;
        memcpy(&sz, raw.data() + z + 1, 4);
        size_t at = z + 5;
        if (at + sz > raw.size()) break;
        char b[64];
        snprintf(b, sizeof(b), " size=%u\r\n", sz);
        out += "obj " + tag + b;
        for (uint32_t o = 0; o < sz; o += 32) {
            snprintf(b, sizeof(b), "  +%04x ", o);
            out += b;
            for (uint32_t k = o; k < sz && k < o + 32; k++) {
                uint8_t c = (uint8_t)raw[at + k];
                out += hx[c >> 4];
                out += hx[c & 15];
            }
            out += "\r\n";
        }
        p = at + sz;
    }
}

// The newest reports stay (at most 12 files: a report is a few hundred kB).
static void PruneDesyncReports() {
    std::vector<std::pair<uint64_t, std::wstring>> files;
    WIN32_FIND_DATAW fd;
    std::wstring dir = ModDataDir();
    HANDLE h = FindFirstFileW((dir + L"ls_desync_*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        uint64_t t = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        files.push_back({t, fd.cFileName});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (files.size() < 12) return;
    std::sort(files.begin(), files.end());
    for (size_t i = 0; i + 11 < files.size(); i++) DeleteFileW((dir + files[i].second).c_str());
}

static void DesyncReport(int minute, const std::string& peerHash, const char* why) {
    if (!BattleForm()) return;
    if (g_sim > 0 || g_loading) {  // (only at a boundary: never inside a simulation step or a load)
        Log("lockstep: DESYNC report of minute %d not written (not between minutes here)", minute);
        return;
    }
    PruneDesyncReports();
    std::string out = "RTW3MP " MP_MOD_VERSION " DESYNC report: " + std::string(S.isA ? "A" : "B") + " side " +
                      std::to_string(S.mySide) + " battle " + std::to_string(S.bid) + " epoch " + std::to_string(S.epoch) +
                      " minute " + std::to_string(minute) + " (boundary here " + std::to_string(S.boundary) +
                      ", engine level " + std::to_string(MP_LS_LEVEL) + ")\r\n";
    const std::string& mine = why ? S.myReadyHash : S.myHash;  // (the loaded battles: the hash the READY check compared)
    out += "hash here " + mine + (peerHash.empty() ? std::string() : std::string(S.isA ? " B " : " A ") + peerHash) +
           "\r\n";
    if (why) out += std::string("why ") + why + "\r\n";
    out += "base " + std::to_string((uint32_t)dl::g_base) + " t=" + ClockText() + "\r\n";
    std::string pend;
    for (auto& ov : g_overlay) pend += " " + ov.first.substr(0, 60);
    out += "pending own orders (in their fields on this PC):" + (pend.empty() ? std::string(" none") : pend) + "\r\n";
    for (int k = 0; k < kFlightKeep; k++) {  // oldest first
        const FlightMinute& fm = g_flight[(g_flightNext + k) % kFlightKeep];
        if (fm.minute < 0 || fm.epoch != S.epoch) continue;
        out += "draws minute " + std::to_string(fm.minute) + " count " + std::to_string(fm.sites.size()) + "\r\n";
        char b[16];
        for (uint32_t c : fm.sites) {
            snprintf(b, sizeof(b), "%x\r\n", c);
            out += b;
        }
    }
    const char* classes[] = {"TfrmMain", "TForce", "TDivision", "TFightingShip", "TAirFormation", "TTorpedo", "TSubmarine"};
    for (const char* cls : classes) {
        out += std::string("fields ") + cls + ":";
        for (auto& f : dl::FieldList(cls)) {
            char b[16];
            snprintf(b, sizeof(b), " %x=", f.first);
            out += b + f.second;
        }
        out += "\r\n";
    }
    if (!g_rawPrev.empty()) {
        out += "raw boundary " + std::to_string(g_rawPrevMin) + "\r\n";
        RawText(out, g_rawPrev);
    }
    if (!g_rawStart.empty() && g_rawStartMin == minute) {
        out += "raw start " + std::to_string(g_rawStartMin) + "\r\n";
        RawText(out, g_rawStart);
    }
    if (!g_rawCur.empty()) {
        out += "raw boundary " + std::to_string(g_rawCurMin) + "\r\n";
        RawText(out, g_rawCur);
    }
    CanonBegin();
    out += "state\r\n" + StateDumpText();
    CanonEnd();
    char name[96];
    snprintf(name, sizeof(name), "ls_desync_%s_%d_%d_%d.txt", S.isA ? "A" : "B", S.bid, S.epoch, minute);
    bool ok = WriteFileBytes(ModDataDir() + U2W(name), out);
    Log("lockstep: DESYNC report %s%s (%u kB; compare it with the other PC's: tools/desync_diff.py)", name,
        ok ? " written" : " NOT written", (unsigned)(out.size() / 1024));
}

std::string StateText() {
    void* m = Frm();
    char b[800];
    if (!m) return "ok frm=0";
    HWND h = game::FormHwnd(m);
    void* fastBtn = E.fFastestBtn >= 0 ? dl::At<void*>(m, E.fFastestBtn) : nullptr;
    int fastest = fastBtn && E.fDownOff > 0 && E.fDownOff < 0x1000 ? dl::At<uint8_t>(fastBtn, E.fDownOff) : -1;
    snprintf(b, sizeof(b),
             "ok running=%d moving=%d frm=1 visible=%d t=%s min=%d left=%d finished=%d pause=%d autorun=%d endgame=%d "
             "engaged=%d speed=%d intervals=%d ivsec=%d playerSide=%d forces=%d divs=%d allships=%d af=%d torps=%d "
             "draws=%llu/%llu/%llu gate=%d/%d ls=%s fastest=%d",
             TimerOn(m, E.fRunTimer) ? 1 : 0, TimerOn(m, E.fMoveTimer) ? 1 : 0, h && IsWindowVisible(h) ? 1 : 0,
             ClockText().c_str(), Minute(), dl::At<int>(m, E.fMinutesLeft), dl::At<uint8_t>(m, E.fFinished),
             dl::At<uint8_t>(m, E.fPause), E.fAutoRun >= 0 ? dl::At<uint8_t>(m, E.fAutoRun) : -1,
             dl::At<uint8_t>(m, E.fEndGame), E.fEngaged >= 0 ? dl::At<uint8_t>(m, E.fEngaged) : -1,
             dl::At<int>(m, E.fGameSpeed), E.fIntervals >= 0 ? dl::At<int>(m, E.fIntervals) : -1,
             E.intervalSecVar ? *E.intervalSecVar : -1, E.playerSideVar ? *E.playerSideVar : -1,
             dl::ListCount(List(E.forcesVar)), dl::ListCount(List(E.divisionsVar)), dl::ListCount(List(E.allShipsVar)),
             dl::ListCount(List(E.airVar)), dl::ListCount(List(E.torpVar)), (unsigned long long)g_draws[0],
             (unsigned long long)g_draws[1], (unsigned long long)g_draws[2], g_pocGate ? 1 : 0, g_pocTokens,
             S.on ? StName(S.st) : "off", fastest);
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
