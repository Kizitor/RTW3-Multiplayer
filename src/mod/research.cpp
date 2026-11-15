#include "research.h"
#include "economy.h"
#include "game.h"
#include "lockstep.h"
#include "nationplay.h"
#include "../../third_party/minhook/include/MinHook.h"
#include <algorithm>
#include <random>

// See research.h. Game facts (1.01.44; docs/GAME-INTERNALS.md "Research messages"):
// - TfrmBuildCamp.DoResearch(aNationIdx, NoNonGunmessages) runs once per nation and month (DoStrategicTurn). Every one of
//   its 19 ShowPicMessage and 29 ShowResearchResult calls, its air/sea rescue question (TdlgEventAnswer.ExecuteGeneral)
//   and CheckTechSharing run only for aNationIdx 0 or the nation that is PlayerNation; so do the messages of the routines
//   it calls (SetResearchID's DoMessage, ShowGunImprovementMessage, DoGunResearch, CheckOldTechs). For another nation it
//   runs the computer's rules (its AI priorities, a 5 % chance of a doubled monthly gain per area, milder setbacks, an
//   extra catch-up gift) and gives the player intelligence reports about it (AddIntelReport).
// - TdlgPictureMessage.ShowResearchResult(aCaption, aMessage, anEffect, PictureFileName) shows the modal "Research
//   breakthrough" dialog and adds "aCaption, aMessage, anEffect" to TfrmBuildCamp.TurnMessages; ShowPicMessage(text,
//   picture, CopyMessageToLog) adds the text when CopyMessageToLog is set.
namespace research {

namespace {

struct Api {
    bool resolved = false, ok = false, hooked = false;
    void *mShowPic = nullptr, *mShowRes = nullptr, *mExecGeneral = nullptr, *mClearHints = nullptr,
         *mAirbaseOK = nullptr, *mHasId = nullptr, *mSetId = nullptr, *mAddIntel = nullptr, *mDoResearch = nullptr;
    int fASR = -1, fAskAS = -1, fTechEx = -1, fEffort = -1, fName = -1, fName2 = -1, fGuns = -1, fAreas = -1;
    uint32_t picCell = 0, askCell = 0;  // the cells holding the addresses of dlgPictureMessage and dlgEventAnswer
    // TEST ONLY (`resforce`): the research tables (SetResearchID): area a's level l at techs + a*0x414 + l*0x1d (name
    // +0, effect +4, year +8, cost +0x11, id +0x15), the areas' names and pictures, the last area's index (the guns').
    uint8_t* techs = nullptr;
    void** areaNames = nullptr;
    void** areaPics = nullptr;
    int* lastArea = nullptr;
} A;

const int kAreaRec = 0x35;  // TBuilderNation.ResearchAreas: Advantage +0, Prio +1, months +5, CurrentLevel +9,
                            // ResearchPoints +0xd, the 36 levels' known flags +0x11
const int kLevels = 36;
const size_t kMaxDialogs = 40, kMaxLines = 200;  // kept per nation while its player is away

// A dialog of the research month: P a picture message (text, picture; copy), R a research result (caption, message,
// effect, picture), Q a question (prompt, answers 1-3; preselect; `as`: the air/sea rescue question).
struct Item {
    char kind = 'P';
    std::wstring f[4];
    int n = 0;
};

struct Run {  // the month being captured (host)
    int K = -1;
    std::vector<Item> items;
    int questions = 0;
};
Run g_run;
bool g_capturing = false;  // ShowResearchResult is recorded (d_ShowRes)

struct Last {  // `resinfo K` (host)
    int date = -1;
    std::string text;
};
std::map<int, Last> g_last;
std::map<int, std::vector<Item>> g_dialogs;       // host: K's dialogs not sent yet
std::map<int, std::vector<std::wstring>> g_lines;  // host: K's turn report lines not sent yet
bool g_testOff = false;                            // TEST ONLY (`resplayer 0`): the computer's research for joined nations
struct ForceNote {                                  // TEST ONLY (`resforce`): logged when its month runs
    std::string what;
};
std::map<int, ForceNote> g_force;
std::mt19937 g_rng((unsigned)(GetTickCount() * 2654435761u + 17));
int Rnd(int n) { return n <= 0 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(g_rng); }

struct Client {
    int seq = -1;
    std::vector<Item> items;
    std::string last;  // `resinfo` on the client: what the last month showed
} g_client;

uint8_t* Code(void* p) { return (uint8_t*)p; }
uint8_t* Target(uint8_t* call) { return call + 5 + *(int32_t*)(call + 1); }

bool InImg(uint32_t a) { return a >= dl::g_base && a < dl::g_base + dl::g_size; }

void* Dialog(uint32_t cell) {  // `mov eax,[cell]; mov eax,[eax]`: the form object
    if (!cell) return nullptr;
    void* var = *(void**)(uintptr_t)cell;
    return var ? *(void**)var : nullptr;
}

std::string Esc(const std::wstring& w) {  // one field of `kres`: line breaks kept, separators and '|' replaced
    std::string s = W2U(w);
    for (char& c : s)
        if (c == '\r') c = '\x1d';
        else if (c == '\n') c = '\x1c';
        else if (c == '\x1e' || c == '\x1f' || c == '|') c = ' ';
    return s;
}

std::wstring Unesc(const std::string& s0) {
    std::string s = s0;
    for (char& c : s)
        if (c == '\x1d') c = '\r';
        else if (c == '\x1c') c = '\n';
    return U2W(s);
}

std::string OneLine(const std::wstring& w, size_t max = 300) {  // for the log
    std::string s = W2U(w);
    for (char& c : s)
        if (c == '\r' || c == '\n') c = ' ';
    return s.size() > max ? s.substr(0, max) + "..." : s;
}

std::string Encode(const std::vector<Item>& v) {
    std::string out;
    for (const Item& it : v) {
        out += it.kind;
        int nf = it.kind == 'P' ? 2 : 4;
        for (int i = 0; i < nf; i++) out += '\x1f' + Esc(it.f[i]);
        out += '\x1f' + std::to_string(it.n);
        out += '\x1e';
    }
    return out;
}

std::vector<Item> Decode(const std::string& s) {
    std::vector<Item> out;
    for (const std::string& rec : Split(s, '\x1e')) {
        if (rec.empty()) continue;
        std::vector<std::string> f = Split(rec, '\x1f');
        Item it;
        it.kind = f[0].empty() ? '?' : f[0][0];
        size_t nf = it.kind == 'P' ? 2 : 4;
        if ((it.kind != 'P' && it.kind != 'R' && it.kind != 'Q') || f.size() < nf + 2) continue;
        for (size_t i = 0; i < nf; i++) it.f[i] = Unesc(f[i + 1]);
        it.n = atoi(f[nf + 1].c_str());
        out.push_back(it);
    }
    return out;
}

bool Resolve() {
    if (A.resolved) return A.ok;
    A.resolved = true;
    A.mShowPic = dl::Method("TdlgPictureMessage", "ShowPicMessage");
    A.mShowRes = dl::Method("TdlgPictureMessage", "ShowResearchResult");
    A.mExecGeneral = dl::Method("TdlgEventAnswer", "ExecuteGeneral");
    A.mClearHints = dl::Method("TdlgEventAnswer", "ClearHints");
    A.mAirbaseOK = dl::Method("TBuilderNation", "AirbaseSizeOK");
    A.mHasId = dl::Method("TBuilderNation", "HasResearchId");
    A.mSetId = dl::Method("TBuilderNation", "SetResearchID");
    A.mAddIntel = dl::Method("TfrmBuildCamp", "AddIntelReport");
    A.mDoResearch = dl::Method("TfrmBuildCamp", "DoResearch");
    A.fASR = dl::Field("TBuilderNation", "AirSeaRescue");
    A.fAskAS = dl::Field("TBuilderNation", "AskAS");
    A.fTechEx = dl::Field("TBuilderNation", "TechnicalExcellence");
    A.fEffort = dl::Field("TBuilderNation", "IntelEffort");
    A.fName = dl::Field("TBuilderNation", "NationName");
    A.fName2 = dl::Field("TBuilderNation", "Name2");
    A.fGuns = dl::Field("TBuilderNation", "Guns");  // Guns[2..20] (ShowGunImprovementMessage: [nation + cal*4 + 0x11d])
    A.fAreas = dl::Field("TBuilderNation", "ResearchAreas");
    // DoResearch's dialogs: `mov eax,[cell]; mov eax,[eax]; call ShowResearchResult` (its breakthrough) and `mov eax,[cell];
    // mov eax,[eax]; mov ecx,<answer 1>; mov edx,<prompt>; call ExecuteGeneral` (the air/sea rescue question)
    uint8_t* p = Code(A.mDoResearch);
    for (size_t i = 0; p && i + 22 < 0x6000 && (!A.picCell || !A.askCell); i++) {
        uint8_t* q = p + i;
        if (q[0] != 0xA1 || q[5] != 0x8B || q[6] != 0x00 || !InImg(*(uint32_t*)(q + 1))) continue;
        if (!A.picCell && q[7] == 0xE8 && A.mShowRes && Target(q + 7) == Code(A.mShowRes)) A.picCell = *(uint32_t*)(q + 1);
        if (!A.askCell && q[7] == 0xB9 && q[12] == 0xBA && q[17] == 0xE8 && A.mExecGeneral &&
            Target(q + 17) == Code(A.mExecGeneral))
            A.askCell = *(uint32_t*)(q + 1);
    }
    // SetResearchID: `mov [ebp-10h],<levels' id field>; mov [ebp-14h],<area names>; mov [ebp-18h],<area pictures>` after
    // `mov eax,[<last area index>]`
    if (uint8_t* s = Code(A.mSetId)) {
        uint8_t* h = dl::FindPattern(s, 0x80, {0xA1, -1, -1, -1, -1, 0x85, 0xC0, 0x0F, 0x8C});
        if (h && InImg(*(uint32_t*)(h + 1))) A.lastArea = *(int**)(h + 1);
        uint8_t* m = dl::FindPattern(s, 0x80, {0xC7, 0x45, 0xF0, -1, -1, -1, -1, 0xC7, 0x45, 0xEC, -1, -1, -1, -1, 0xC7, 0x45,
                                               0xE8, -1, -1, -1, -1});
        if (m && InImg(*(uint32_t*)(m + 3)) && InImg(*(uint32_t*)(m + 10)) && InImg(*(uint32_t*)(m + 17))) {
            A.techs = (uint8_t*)(uintptr_t)(*(uint32_t*)(m + 3) - 0x15);
            A.areaNames = (void**)(uintptr_t) * (uint32_t*)(m + 10);
            A.areaPics = (void**)(uintptr_t) * (uint32_t*)(m + 17);
        }
    }
    A.ok = A.mShowPic && A.mShowRes && A.mExecGeneral && A.mAirbaseOK && A.mHasId && A.mAddIntel && A.mDoResearch &&
           A.fASR >= 0 && A.fAskAS >= 0 && A.fTechEx >= 0 && A.fEffort >= 0 && A.fName >= 0 && A.fName2 >= 0 &&
           A.fGuns >= 0 && A.fAreas >= 0 && A.picCell && A.askCell;
    Log("research: %s (dialogs %08x/%08x, show %p/%p, ask %p, airbase %p, fields %d/%d/%d/%d guns %d areas %d; tables %s)",
        A.ok ? "ok" : "UNAVAILABLE", A.picCell, A.askCell, A.mShowPic, A.mShowRes, A.mExecGeneral, A.mAirbaseOK, A.fASR,
        A.fAskAS, A.fTechEx, A.fEffort, A.fGuns, A.fAreas, A.techs && A.areaNames && A.areaPics && A.lastArea ? "found" : "not found");
    return A.ok;
}

int Gun(void* rec, int cal) { return dl::At<int>(rec, A.fGuns + (cal - 2) * 4); }  // cal 2..20

int MaxAirbase(void* rec) {  // the largest airbase (in steps of 20) the nation's research allows (AirbaseSizeOK)
    int best = 0;
    for (int s = 20; s <= 400; s += 20) {
        if (!(dl::Call(A.mAirbaseOK, (uint32_t)(uintptr_t)rec, (uint32_t)s) & 0xFF)) break;
        best = s;
    }
    return best;
}

void AddHostIntel(void* form, void* rec, const std::wstring& text) {  // the game's AddIntelReport (the host's own)
    dl::ConstUStr t(text);
    dl::Call(A.mAddIntel, (uint32_t)(uintptr_t)form, (uint32_t)(uintptr_t)rec, t.ptr());
}

// The capture's sinks (in K's view, during DoResearch): each dialog as the game would have shown it, its turn report
// line where the game adds one.
void PicSink(const std::wstring& text, const std::wstring& pic, bool copy) {
    Item it;
    it.kind = 'P';
    it.f[0] = text;
    it.f[1] = pic;
    it.n = copy ? 1 : 0;
    g_run.items.push_back(it);
    if (copy) game::TurnMessageAdd(text);
}

int QuestionSink(const std::wstring& prompt, const std::wstring& a1, const std::wstring& a2, const std::wstring& a3,
                 int preselect) {
    Item it;
    it.kind = 'Q';
    it.f[0] = prompt;
    it.f[1] = a1;
    it.f[2] = a2;
    it.f[3] = a3;
    it.n = preselect;
    g_run.items.push_back(it);
    g_run.questions++;
    return 2;  // (the second answer: the air/sea rescue question's "No"; the player's own answer comes later)
}

}  // namespace

// TdlgPictureMessage.ShowResearchResult(Self=EAX, aCaption=EDX, aMessage=ECX; anEffect, PictureFileName), ret 8: while
// a research month is captured, recorded with its turn report line instead of the modal dialog.
extern "C" void* research_oShowRes = nullptr;
extern "C" int __stdcall research_ShowRes(const wchar_t* caption, const wchar_t* msg, const wchar_t* effect,
                                          const wchar_t* pic) {
    if (!g_capturing) return 0;
    try {
        Item it;
        it.kind = 'R';
        it.f[0] = caption ? dl::ReadUStr(caption) : L"";
        it.f[1] = msg ? dl::ReadUStr(msg) : L"";
        it.f[2] = effect ? dl::ReadUStr(effect) : L"";
        it.f[3] = pic ? dl::ReadUStr(pic) : L"";
        g_run.items.push_back(it);
        game::TurnMessageAdd(it.f[0] + L", " + it.f[1] + L", " + it.f[2]);
    } catch (...) {
    }
    return 1;
}

extern "C" __declspec(naked) void research_dShowRes() {
    __asm {
        push eax
        push ecx
        push edx
        mov eax, esp               // [eax+16] PictureFileName, [eax+20] anEffect
        push dword ptr [eax + 16]  // PictureFileName
        push dword ptr [eax + 20]  // anEffect
        push ecx                   // aMessage
        push edx                   // aCaption
        call research_ShowRes
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jnz skip
        jmp dword ptr [research_oShowRes]
    skip:
        ret 8
    }
}

bool Install() {
    if (!Resolve()) return false;
    MH_STATUS st = MH_CreateHook(A.mShowRes, (void*)&research_dShowRes, &research_oShowRes);
    if (st == MH_OK) {
        st = MH_EnableHook(A.mShowRes);
        if (st != MH_OK) MH_RemoveHook(A.mShowRes);
    }
    A.hooked = st == MH_OK;
    if (!A.hooked) {
        research_oShowRes = nullptr;
        Log("research: ShowResearchResult: hook failed (%d); joined nations' research stays the computer's", (int)st);
    }
    return A.hooked;
}

bool Available() { return A.ok && A.hooked; }

bool HostRun(void* form, int idx, bool noNonGunMessages) {
    if (!Available() || noNonGunMessages || idx < 1 || idx > 8 || !hookcb::IsHumanRemoteNation(idx)) return false;
    const int K = idx;
    if (g_testOff) {
        Log("research: nation %d's month: the computer's research (TEST resplayer 0)", K);
        return false;
    }
    void* rec = game::Nation(K);
    if (!form || !rec || game::ViewNation() != 0 || game::PlayerNation() != game::Nation(0) || game::BattleSlotMode())
        return false;
    // before: what the host's own intelligence and the question's rule compare with
    const uint8_t ask0 = dl::At<uint8_t>(rec, A.fAskAS), asr0 = dl::At<uint8_t>(rec, A.fASR);
    int guns0[21] = {};
    for (int c = 2; c <= 20; c++) guns0[c] = Gun(rec, c);
    game::PlayerResearchIn in;
    in.K = K;
    in.war = hookcb::KWarCounter(K);
    const bool ts = hookcb::KTechSharing(K, in.techSharing);
    const bool ie = nplay::HostIntelEffort(K, in.intelEffort);
    in.airbaseSize = MaxAirbase(rec);
    g_run = Run();
    g_run.K = K;
    if (!lockstep::BeginPictureCapture(&PicSink)) {
        Log("research: nation %d's month: the picture capture is unavailable; the computer's research", K);
        return false;
    }
    if (!lockstep::BeginQuestionCapture(&QuestionSink)) {
        lockstep::EndPictureCapture();
        Log("research: nation %d's month: the question capture is unavailable; the computer's research", K);
        return false;
    }
    game::SetBoxCapture(true);
    g_capturing = true;
    game::PlayerResearchOut out;
    const bool ran = game::RunPlayerResearch(in, out);
    g_capturing = false;
    const std::wstring box = game::SetBoxCapture(false);
    lockstep::EndQuestionCapture();
    lockstep::EndPictureCapture();
    std::vector<Item> items;
    items.swap(g_run.items);
    if (!ran) {
        Log("research: nation %d's month: its view is not available here; the computer's research", K);
        return false;
    }
    // the question: answered on its player's PC, or (an older mod, nobody connected) by the computer's rule as before
    std::string question = "none";
    const bool asked = g_run.questions > 0 && !ask0 && dl::At<uint8_t>(rec, A.fAskAS) && !dl::At<uint8_t>(rec, A.fASR);
    if (asked && hookcb::ResearchDialogsShown(K)) {
        question = "the air/sea rescue question goes to its player";
    } else if (g_run.questions > 0) {
        items.erase(std::remove_if(items.begin(), items.end(), [](const Item& it) { return it.kind == 'Q'; }), items.end());
        if (asked) {
            bool yes = dl::At<uint8_t>(rec, A.fTechEx) != 0 || Rnd(100) < 20;
            if (yes) dl::At<uint8_t>(rec, A.fASR) = 1;
            question = std::string("the air/sea rescue question: its player cannot answer it (an older mod or not "
                                   "connected): the computer's rule: ") + (yes ? "yes" : "no");
        } else {
            question = std::to_string(g_run.questions) + " unexpected question(s) answered 2";
        }
    }
    // the turn report and K's own intelligence
    std::vector<std::wstring> lines = out.turnLines;
    int kIntel = nplay::HostAddIntelReports(K, out.intelLines);
    // K's tech-sharing deals the research's war rule ended
    std::string tsEnded;
    for (int j = 0; j < 9; j++)
        if (j != K && in.techSharing[j] > 0 && out.techSharing[j] <= 0) {
            hookcb::KEndTechSharing(K, j);
            tsEnded += " " + std::to_string(j);
        }
    // the host's own intelligence on K's research, as the computer's research gave it (DoResearch, DoGunResearch,
    // ShowGunImprovementMessage): the techs of its breakthroughs, its new and improved guns
    int hostIntel = 0;
    const int effort = dl::At<int>(rec, A.fEffort);
    const std::wstring name = dl::ReadUStr(dl::At<void*>(rec, A.fName)), adj = dl::ReadUStr(dl::At<void*>(rec, A.fName2));
    std::string techs;
    const std::wstring brk = L"Research breakthrough: ";
    for (const Item& it : items) {
        if (it.kind != 'R' || it.f[0].compare(0, brk.size(), brk) != 0 || it.f[1].empty()) continue;
        techs += (techs.empty() ? "" : "; ") + OneLine(it.f[0].substr(brk.size())) + ": " + OneLine(it.f[1]);
        if (Rnd(100) < effort * 18) {
            AddHostIntel(form, rec, L"There are reports that " + adj + L" scientists have invented " + it.f[1] + L".");
            hostIntel++;
        }
    }
    std::string guns;
    for (int c = 2; c <= 20; c++) {
        int g = Gun(rec, c);
        if (g == guns0[c]) continue;
        guns += " " + std::to_string(c) + ":" + std::to_string(guns0[c]) + "->" + std::to_string(g);
        if (guns0[c] == 9) {  // a new calibre
            if (Rnd(100) < effort * 10 + 40 && game::WarCounter() <= 0) {
                AddHostIntel(form, rec, adj + L" gun manufacturers are now advertising " + std::to_wstring(c) + L" inch guns!");
                hostIntel++;
            }
        } else if (Rnd(20) < effort + 2) {
            AddHostIntel(form, rec, name + L" is reported to have invented improved " + std::to_wstring(c) +
                                        L" inch guns (Quality " + std::to_wstring(g) + L")");
            hostIntel++;
        }
    }
    int nP = 0, nR = 0, nQ = 0;
    for (const Item& it : items) (it.kind == 'P' ? nP : it.kind == 'R' ? nR : nQ)++;
    auto& dv = g_dialogs[K];
    dv.insert(dv.end(), items.begin(), items.end());
    if (dv.size() > kMaxDialogs) dv.erase(dv.begin(), dv.begin() + (dv.size() - kMaxDialogs));
    auto& lv = g_lines[K];
    lv.insert(lv.end(), lines.begin(), lines.end());
    if (lv.size() > kMaxLines) lv.erase(lv.begin(), lv.begin() + (lv.size() - kMaxLines));
    std::string rec2 = "research: nation " + std::to_string(K) + "'s month as its player's (its view, war counter " +
                       std::to_string(in.war) + (ts ? ", its tech-sharing deals" : ", no tech-sharing deals kept") +
                       (ie ? ", its intelligence effort" : ", no intelligence record") +
                       (out.fault ? ", A GAME EXCEPTION" : "") + "): " + std::to_string(nR) + " research result(s), " +
                       std::to_string(nP) + " picture message(s), " + std::to_string(nQ) + " question(s); " +
                       std::to_string(lines.size()) + " turn report line(s), " + std::to_string(kIntel) +
                       " intelligence report(s) of its own; breakthroughs: " + (techs.empty() ? "none" : techs) +
                       "; question: " + question + "; airbase size " + std::to_string(in.airbaseSize) + " -> " +
                       std::to_string(out.airbaseSize) + "; guns" + (guns.empty() ? " unchanged" : guns) +
                       "; tech-sharing deals ended:" + (tsEnded.empty() ? " none" : tsEnded) + "; the host's intelligence: " +
                       std::to_string(hostIntel) + " report(s) (effort " + std::to_string(effort) + ")" +
                       (box.empty() ? "" : "; its error box (not shown): " + OneLine(box)) +
                       (asr0 != dl::At<uint8_t>(rec, A.fASR) ? "; AirSeaRescue " + std::to_string(asr0) + " -> " +
                                                                  std::to_string(dl::At<uint8_t>(rec, A.fASR))
                                                            : "");
    auto f = g_force.find(K);
    if (f != g_force.end()) {
        rec2 += "; TEST resforce: " + f->second.what;
        g_force.erase(f);
    }
    Log("%s", rec2.c_str());
    for (const Item& it : items)
        Log("research: nation %d: %s '%s'%s", K, it.kind == 'P' ? "picture message" : it.kind == 'R' ? "research result" : "question",
            OneLine(it.kind == 'R' ? it.f[0] + L" | " + it.f[1] + L" | " + it.f[2] : it.f[0]).c_str(),
            it.kind == 'P' ? (" (" + W2U(it.f[1]) + ")").c_str() : it.kind == 'R' ? (" (" + W2U(it.f[3]) + ")").c_str() : "");
    Last& l = g_last[K];
    l.date = economy::CurrentDate();
    l.text = rec2;
    return true;
}

std::string TakeDialogs(int K) {
    auto it = g_dialogs.find(K);
    if (it == g_dialogs.end()) return "";
    std::string s = Encode(it->second);
    g_dialogs.erase(it);
    return s;
}

std::vector<std::wstring> TakeLines(int K) {
    std::vector<std::wstring> out;
    auto it = g_lines.find(K);
    if (it != g_lines.end()) {
        out.swap(it->second);
        g_lines.erase(it);
    }
    return out;
}

void ClearNews() {
    size_t n = 0;
    for (auto& e : g_dialogs) n += e.second.size();
    for (auto& e : g_lines) n += e.second.size();
    g_dialogs.clear();
    g_lines.clear();
    g_force.clear();
    if (n) Log("research: messages kept for the joined nations dropped: %u", (unsigned)n);
}

// ---------------------------------------------------------------------------------------------
// A joined player's PC

void ClientSetMonth(int seq, const std::string& encoded) {
    std::vector<Item> items = Decode(encoded);
    if (items.empty() && !g_client.items.empty()) {  // (a month sent again: the dialogs not shown yet stay)
        g_client.seq = seq;
        Log("research: %d research message(s) not shown yet wait for this month (seq %d)", (int)g_client.items.size(),
            seq);
        return;
    }
    g_client.items.swap(items);
    g_client.seq = g_client.items.empty() ? -1 : seq;
    if (!g_client.items.empty())
        Log("research: the month (seq %d) brings %d research message(s) of this nation", seq, (int)g_client.items.size());
}

bool ClientDue(int seq) { return g_client.seq >= 0 && g_client.seq == seq && !g_client.items.empty(); }

void ClientReset() {
    g_client.seq = -1;
    g_client.items.clear();
}

void ClientShow(int seq) {
    if (!ClientDue(seq)) return;
    std::vector<Item> items;
    items.swap(g_client.items);
    g_client.seq = -1;
    void* pic = Dialog(A.picCell);
    void* ask = Dialog(A.askCell);
    if (!Resolve() || !pic) {
        Log("research: %d research message(s) of the month not shown (the dialogs are unavailable)", (int)items.size());
        return;
    }
    int nP = 0, nR = 0, nQ = 0;
    std::string shown;
    for (const Item& it : items) {
        auto S = [](const dl::ConstUStr& u, const std::wstring& w) { return w.empty() ? 0u : u.ptr(); };
        if (it.kind == 'P') {
            dl::ConstUStr t(it.f[0]), p(it.f[1]);
            uint32_t st[1] = {(uint32_t)(it.n ? 1 : 0)};
            Log("research: picture message '%s' (%s)", OneLine(it.f[0]).c_str(), W2U(it.f[1]).c_str());
            dl::Call(A.mShowPic, (uint32_t)(uintptr_t)pic, S(t, it.f[0]), S(p, it.f[1]), 1, st);
            nP++;
        } else if (it.kind == 'R') {
            dl::ConstUStr c(it.f[0]), m(it.f[1]), e(it.f[2]), p(it.f[3]);
            uint32_t st[2] = {S(e, it.f[2]), S(p, it.f[3])};
            Log("research: research result '%s' '%s' '%s' (%s)", OneLine(it.f[0]).c_str(), OneLine(it.f[1]).c_str(),
                OneLine(it.f[2]).c_str(), W2U(it.f[3]).c_str());
            dl::Call(A.mShowRes, (uint32_t)(uintptr_t)pic, S(c, it.f[0]), S(m, it.f[1]), 2, st);
            nR++;
        } else if (it.kind == 'Q' && ask) {
            dl::ConstUStr q(it.f[0]), a1(it.f[1]), a2(it.f[2]), a3(it.f[3]);
            uint32_t st[3] = {S(a2, it.f[2]), S(a3, it.f[3]), (uint32_t)it.n};
            if (A.mClearHints) dl::Call(A.mClearHints, (uint32_t)(uintptr_t)ask);
            int r = (int)dl::Call(A.mExecGeneral, (uint32_t)(uintptr_t)ask, S(q, it.f[0]), S(a1, it.f[1]), 3, st);
            void* pn = game::PlayerNation();
            std::string what = "no change";
            if (r == 1 && pn) {  // DoResearch: answer 1 institutes the service (AirSeaRescue)
                dl::At<uint8_t>(pn, A.fASR) = 1;
                what = "AirSeaRescue := 1";
            }
            Log("research: question '%s' answered %d (%s)", OneLine(it.f[0], 120).c_str(), r, what.c_str());
            shown += " question answered " + std::to_string(r) + ";";
            nQ++;
        } else if (it.kind == 'Q') {
            Log("research: question '%s' not asked (the question dialog is unavailable)", OneLine(it.f[0], 120).c_str());
        }
    }
    g_client.last = std::to_string(nR) + " research result(s), " + std::to_string(nP) + " picture message(s), " +
                    std::to_string(nQ) + " question(s);" + shown;
    Log("research: shown: %s", g_client.last.c_str());
}

// ---------------------------------------------------------------------------------------------
// Bridge

namespace {

std::string Force(int K, int area, bool as) {
    void* rec = game::Nation(K);
    if (!rec || !A.techs || !A.areaNames || !A.areaPics || !A.lastArea) return "error the research tables were not found";
    const int year = economy::CurrentDate() / 12, last = *A.lastArea;
    // preferably a tech that no other copy's player can see completed this month: the host's nation (for a joined
    // nation's tech), the nations of the connected joined players with this mod (for the host's own) know it already or
    // research another of the area's techs now; so only this nation's research brings it
    std::vector<void*> others;
    for (int n = 0; n <= 8; n++)
        if (n != K && (K > 0 ? n == 0 : hookcb::ResearchDialogsShown(n)) && game::Nation(n))
            others.push_back(game::Nation(n));
    auto safe = [](void* o, int a, int lv) {
        uint8_t* r = (uint8_t*)o + A.fAreas + a * kAreaRec;
        return r[0x11 + lv] != 0 || *(int*)(r + 9) != lv;
    };
    uint8_t* base = (uint8_t*)rec + A.fAreas;
    std::string out;
    for (int pass = area >= 0 ? 1 : 0; pass < 2 && out.empty(); pass++)
        for (int a = area >= 0 ? area : 0; a < last && out.empty(); a++) {
            uint8_t* r = base + a * kAreaRec;
            int lv = *(int*)(r + 9), pts = *(int*)(r + 0xd);
            if (lv < 0 || lv >= kLevels || r[0x11 + lv] || (area < 0 && pts <= 0) || !A.areaNames[a]) {
                if (area >= 0) return "error area " + std::to_string(a) + " has no tech to complete now";
                continue;
            }
            uint8_t* t = A.techs + a * 0x414 + lv * 0x1d;
            void* nm = *(void**)t;
            int ty = *(int*)(t + 8), cost = *(int*)(t + 0x11);
            if (!nm || cost <= 0 || ty > year + 2) {  // (DoResearch blocks a completion while the year is 3 or more ahead)
                if (area >= 0) return "error area " + std::to_string(a) + "'s next tech is not available yet";
                continue;
            }
            bool othersSafe = std::all_of(others.begin(), others.end(), [&](void* o) { return safe(o, a, lv); });
            if (pass == 0 && !othersSafe) continue;
            const int want = cost * 2 + 2;  // above the cost even after the month's halving (setbacks, a tech ahead)
            *(int*)(r + 0xd) = want;
            out = "area=" + std::to_string(a) + " level=" + std::to_string(lv) + " tech=" + W2U(dl::ReadUStr(nm)) +
                  " areaName=" + W2U(dl::ReadUStr(A.areaNames[a])) + " picture=" + W2U(dl::ReadUStr(A.areaPics[a])) +
                  " effect=" + W2U(dl::ReadUStr(*(void**)(t + 4))) + " year=" + std::to_string(ty) + " cost=" +
                  std::to_string(cost) + " points=" + std::to_string(pts) + "->" + std::to_string(want) +
                  " others=" + (othersSafe ? "1" : "0");
        }
    if (out.empty()) return "error no area with a tech to complete now";
    if (as) {  // the air/sea rescue question in the next month: the tech that asks it (id 1717), not asked yet
        uint32_t pt[2] = {0, 0}, st[1] = {(uint32_t)(uintptr_t)pt};
        if (!(dl::Call(A.mHasId, (uint32_t)(uintptr_t)rec, 1717) & 0xFF))
            dl::Call(A.mSetId, (uint32_t)(uintptr_t)rec, 1717, 0, 1, st);
        dl::At<uint8_t>(rec, A.fASR) = 0;
        dl::At<uint8_t>(rec, A.fAskAS) = 0;
        out += " as=1 (tech 1717: " + std::string((dl::Call(A.mHasId, (uint32_t)(uintptr_t)rec, 1717) & 0xFF) ? "yes" : "NO") +
               ", AirSeaRescue 0, AskAS 0)";
    }
    g_force[K].what = out;
    Log("TEST: research: resforce nation %d: %s", K, out.c_str());
    return "ok " + out;
}

}  // namespace

std::string BridgeCommand(const std::string& cmd, const std::vector<std::string>& args) {
    auto arg = [&](size_t i, const char* d = "") { return i < args.size() ? args[i] : std::string(d); };
    if (cmd == "resinfo") {
        int K = atoi(arg(1, "-1").c_str());
        if (K >= 1 && K <= 8) {
            auto it = g_last.find(K);
            void* rec = game::Nation(K);
            std::string s = "ok K=" + std::to_string(K) + " available=" + (Available() ? "1" : "0") +
                            " dialogs=" + std::to_string(g_dialogs.count(K) ? g_dialogs[K].size() : 0) +
                            " lines=" + std::to_string(g_lines.count(K) ? g_lines[K].size() : 0);
            if (rec && A.fASR >= 0)
                s += " asr=" + std::to_string(dl::At<uint8_t>(rec, A.fASR)) + " ask=" +
                     std::to_string(dl::At<uint8_t>(rec, A.fAskAS));
            s += " date=" + (it == g_last.end() ? std::string("-") : economy::DateString(it->second.date)) +
                 " last=" + (it == g_last.end() ? std::string("-") : it->second.text);
            return s;
        }
        return "ok available=" + std::string(Available() ? "1" : "0") + " due=" + std::to_string(g_client.seq) +
               " waiting=" + std::to_string(g_client.items.size()) + " last=" + (g_client.last.empty() ? "-" : g_client.last);
    }
    if (cmd == "resforce") {  // TEST ONLY
        int K = atoi(arg(1, "-1").c_str());
        if (K < 0 || K > 8 || !game::Nation(K)) return "error resforce <nation> [area|-1] [as]";
        bool as = arg(2) == "as" || arg(3) == "as";
        int area = args.size() > 2 && arg(2) != "as" ? atoi(arg(2).c_str()) : -1;
        return Force(K, area, as);
    }
    if (cmd == "resplayer") {  // TEST ONLY
        if (args.size() > 1) g_testOff = arg(1) == "0";
        Log("TEST: research: joined nations' research as the player's: %s", g_testOff ? "off" : "on");
        return std::string("ok on=") + (g_testOff ? "0" : "1");
    }
    return "error unknown research command";
}

}  // namespace research
