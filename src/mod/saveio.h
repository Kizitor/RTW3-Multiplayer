#pragma once
#include "common.h"
#include <climits>

// Line-preserving INI document (the .bcs/.dat save files are UTF-8 INI with CRLF).
struct IniSection {
    std::string name;  // without brackets
    std::vector<std::string> lines;
};

struct IniDoc {
    bool bom = false;
    std::vector<std::string> head;  // lines before the first section
    std::vector<IniSection> sections;

    IniSection* Find(const std::string& name);
    const IniSection* Find(const std::string& name) const;
    std::string Get(const std::string& sec, const std::string& key, const std::string& def = "") const;
    void Set(const std::string& sec, const std::string& key, const std::string& value);
};

IniDoc ParseIni(const std::string& bytes);
std::string WriteIni(const IniDoc& doc);

// A set of named files sent over the network.
struct Bundle {
    std::vector<std::pair<std::string, std::string>> files;
    void Add(const std::string& name, const std::string& data) { files.emplace_back(name, data); }
    const std::string* Get(const std::string& name) const;
    std::string Pack() const;
    bool Unpack(const std::string& data);
    size_t TotalBytes() const;
};

namespace saveio {

// Host: pack the slot's save files. File names have the slot number replaced by "{S}".
// idOffset is added to [General] IDNo so each client allocates ids from its own range.
bool BuildStateBundle(const std::wstring& slotDir, int slot, int idOffset, Bundle& out, std::string& err);
// Client: write a state bundle into the slot directory (old save files there are removed first).
bool ApplyStateBundle(const Bundle& b, const std::wstring& slotDir, int slot, std::string& err);
// Client: extract this nation's data from its own saved slot (idStart = IDNo received with the month); 0.6.1: also its
// campaign divisions ("divisions.ini": the [CampaignDivisions] section of the slot's .off; older hosts ignore it).
// 0.7.0, legacy: the turn holds the legacy fleet the player built by hand ([General] MPLegacy=1, see ClearNationFleet).
bool BuildSubmission(const std::wstring& slotDir, int slot, int nationIdx, long long idStart, Bundle& out,
                     std::string& err, bool legacy = false);
// Host: merge a client's submission into the host slot files. A legacy turn also drops the nation's air units based on
// ships its new ship list no longer holds.
bool MergeSubmission(const std::wstring& slotDir, int slot, int nationIdx, const Bundle& sub, std::string& err);
bool IsLegacySubmission(const Bundle& sub);

// The legacy fleet of a joined player (0.7.0): in the campaign's first month the player builds the nation's starting
// fleet by hand, as the game's "Manual build of legacy fleet" does for nation 0. ClearNationFleet puts nation K of a
// slot's files into the state the game's manual build starts from: no ships ([NationKShips] empty, ShipCount=0), no
// designs (DesignFilesK.des empty, DesignIDCount kept), none of K's air units based on those ships and no campaign
// divisions (the slot's .off holds K's own list since 0.6.1, naming those ships: CampDivNo=0).
bool ClearNationFleet(const std::wstring& slotDir, int slot, int K, std::string& summary, std::string& err);
// Drops nation K's air units ([AirUnits] AU<n>...) whose HomeBase is one of `ids`, renumbering the rest. Returns the
// count dropped.
int DropAirUnitsOn(IniDoc& d, int K, const std::set<std::string>& ids);
// Read [General] Year/Month from a slot's .bcs.
bool ReadDate(const std::wstring& slotDir, int slot, int& year, int& month);
// Read [General] IDNo from a slot's .bcs (-1 if unavailable).
long long ReadIdNo(const std::wstring& slotDir, int slot);

// A joined player's own copy of the campaign (the game's "Copy game" of slot 77): nation K becomes the player nation
// (index 0) and the host's nation an AI nation at index K. eventCooldowns: K's own [EUT] values (100), or empty.
// kWar: K's own war counter (months at war > 0, at peace <= 0) when the host sent it, else INT_MIN (approximated).
struct PlayerCopyFiles {
    std::string bcs, map, off;
    bool hasMap = false, hasOff = false;
};
bool TransformNationPlayer(PlayerCopyFiles& f, int K, const std::vector<int>& eventCooldowns, std::string& summary,
                           std::string& err, int kWar = INT_MIN);
bool MakeNationPlayer(const std::wstring& slotDir, int slot, int K, const std::vector<int>& eventCooldowns,
                      std::string& summary, std::string& err, int kWar = INT_MIN);

// Joined players' battles. The battle is fought in a battle slot made with MakeNationPlayer(K): K is nation 0 there and
// the opponent J keeps its index (an AI nation, J != 0 and != K). ExtractBattleResult reads the battle slot before and
// after the battle and returns the ship/loss sections of K, J and the nations allied to them in the battle under their
// host names (nation numbers in them mapped back), plus the changes of K's VP/prestige/fleet morale and of J's VP.
// ApplyBattleResult merges that into the host's save field by field (only what the battle changed relative to what was
// sent, never the player's intel fields; ships no longer listed were sunk), for the nations in `allowed` only. It
// changes `host` only if everything applies. keepUsed (0.7.0, raider battles): the ships' Used marks stay the host's
// (the raider check's thwarts mark ships Used; the month's raider sinkings on the host came before the battle phase).
// 0.7.2 (fix-battlesearch): a request whose search went on after encounters without a battle: `more` (Extract) are its
// other opponents (real indices), whose sections go in too; `otherVP` (Apply) their points (nation -> VP added), the
// deltas' dVP_J being J's own share then.
// 0.8.0 (simultaneous battles): a field of a ship that both the battle (result != sent) and the host's save since the
// send (host != sent: the rest of the month for a battle fought during it, another player's merged battle) changed:
// CrewQuality and BattleStars take the host's value plus the battle's change (CrewQuality within the game's own bounds
// -25..25, AdjustCrewQuality; stars >= 0), the log entries the battle added (from the sent NumberOfLogEntries on) follow
// the host's, RepairTime takes the larger, any other field the battle's value (as before). Ships gone from the host's
// save stay gone (the battle's changes dropped), new ones stay. `notes` (optional) counts what these rules did.
// 0.8.1 (fix-sunktwice): a lost ship stays in its nation's list (the game's sunk or scrapped ships: RepairTime >= 100,
// with its Fate). A ship the host's save lost since the send (sunk in the host's battle, by a raider, a submarine, an AI
// battle, scrapped) stays as the host has it: none of the battle's changes to it are taken. If the battle lost it too (a
// ship can be sunk only once: the host's month, merged first, keeps it), the battle's loss of it is not counted again:
// its nation's losses counter of its type (the battle's delta, while it has one) and the battle's points for it
// (LossTwice; BattlePointsCut) are taken out of the merge; the same for a ship gone from the host's save that the battle
// lost. The rest of the battle is merged as before.
struct LossTwice {
    int nation = 0;               // the ship's nation (host numbering)
    std::string id, name, type;   // Id, Name, ShipType of the sent save
    std::string host;             // the host's Fate of it ("" = gone from the host's save)
    std::string fate;             // the battle's Fate of it ("" = gone from the result)
    int side = -1;                // its side in the battle: 0 = K's (its points were J's), 1 = J's (K's), -1 unknown
    long long points = 0;         // the battle's raw points for it (the player's PC's, or the estimate), 0 = none
    bool estimated = false;       // points estimated on the host (GetApproxVP: no per-ship points in the result)
    std::string counter;          // the losses counter taken back ("" = the battle counted none of its type)
};
struct MergeNotes {
    std::map<std::string, int> added;  // CrewQuality, BattleStars: host + the battle's change (field -> ships)
    std::map<std::string, int> other;  // other fields both changed to different values: the battle's (field -> ships)
    int logs = 0;       // ships whose log got the battle's entries after the host's
    int repair = 0;     // ships whose RepairTime both changed (the larger kept)
    int clamped = 0;    // CrewQuality sums the game's bounds cut
    int monthGone = 0;  // ships of the sent save no longer in the host's (their battle changes dropped)
    int bothGone = 0;   // ... of them also lost in the battle (0.8.1: in `twice`, counted once)
    int monthLost = 0;  // 0.8.1: ships the host's save lost since the send (RepairTime >= 100): kept as the host has them
    std::vector<LossTwice> twice;  // 0.8.1: ships lost (or gone) in the host's month and lost in the battle too
    std::vector<std::string> lostNow;  // 0.8.1: ids of the ships the battle lost that the merge took as lost
    int cutK = 0, cutJ = 0;        // 0.8.1: the points taken off dVP_K / dVP_J for them
    std::string cutHow;            // 0.8.1: how the points were found ("the player's PC's per-ship points", ...)
    bool Any() const {
        return !added.empty() || !other.empty() || logs || repair || monthGone || monthLost || !twice.empty();
    }
    std::string Text() const;
    std::string TwiceText() const;  // 0.8.1: one line on the ships lost twice (empty if none)
};
// 0.8.1: the game's TfrmBuildCamp.GetLogAdjustedVP (0xcc07d8), what HandleBattleAftermath adds to a nation's VP for a
// battle's raw points: v <= 0 -> 0, else Round(0.8 x (Round(3 x Sqrt(v)) + v div 15)) (Round: to even, the FPU's).
long long LogAdjustedVP(long long v);
// 0.8.1: TCampaignShip.GetApproxVP (0xd583fc), the game's estimate of a lost ship's points outside a battle screen
// (QuickResolveRaiderBattle): BB, BC, B, CA, CL, DD, KE: 2 x Displacement, other types: Displacement.
long long ApproxShipVP(const std::string& shipType, long long displacement);
// 0.8.1: the points to take off a battle's points `battle` (what its side gained, log-adjusted) for ships of the other
// side worth `raw` raw points. raw0 >= 0: the side's raw total (the player's PC measured it):
// LogAdjustedVP(raw0) - LogAdjustedVP(raw0 - raw); raw0 < 0: the raw total estimated as the smallest r with
// LogAdjustedVP(r) >= battle. quick (a raider battle auto-resolved: each ship's points log-adjusted alone): the sum of
// LogAdjustedVP of each ship's (`each`). The result is within 0..max(0, battle).
long long BattlePointsCut(long long battle, long long raw0, long long raw, bool quick = false,
                          const std::vector<long long>& each = {});
// The losses counter of [Nation<n>Losses] that a battle adds a lost ship of this ShipType to ("" for none): BB BBLost,
// BC BCLost, B BLost, CA CALost, CL CLLost, CV CVLost, CVL CVLLost, DD DDLost, KE MSLost, LT LTLost, SS SSLost, TR/AMC/AV
// AUXLost (ShowResultScreen 0xdc4154 -> 0xdc40b8 and its merchants' table; AddShipToTotalLosses 0xd0ae10).
std::string LossCounterOf(const std::string& shipType);
bool ExtractBattleResult(const std::string& beforeBcs, const std::string& afterBcs, int K, int J, Bundle& out,
                         std::string& summary, std::string& err, const std::vector<int>& more = {});
// 0.8.1: the result's `deltas` may carry the battle's own points as the player's PC measured them (a 0.8.1 player's PC:
// `vp0`/`vp1` = HandleBattleAftermath's raw VP0/VP1, `sunk` = "id:side:points,..." = GetShipDamagePointValue of each
// ship sunk in it at the battle's end, side 0 = the player's); the host adds `bVP_K`/`bVP_J` (the battle's own points
// when the request had encounters before it) and `bQuick` (1: a raider battle auto-resolved). Older hosts ignore them.
bool ApplyBattleResult(IniDoc& host, const std::string& sentBcs, int K, int J, const Bundle& result,
                       const std::vector<int>& allowed, std::string& summary, std::string& err, bool keepUsed = false,
                       const std::map<int, int>* otherVP = nullptr, MergeNotes* notes = nullptr);

// Campaign divisions (the strategic screen's Fleet organization): the [CampaignDivisions] section of a slot's .off as
// TCampaignDivision.SaveMe writes it: CampDivNo=<n>, then for each division i < n the keys CampDiv<i><Field> with the
// fields CDivId, Name, Abbreviation, DivisionType, DivisionExperience, Training, Commander (officer id), LeadDivision
// (a CDivId), Role, ShipCount and ShipId<j> (j < ShipCount: CShipId values, Ship<n>Id of the .bcs). The game keeps one
// list, its player's; LoadMe looks its ships and commander up in the player's nation.
struct CampDiv {
    std::vector<std::pair<std::string, std::string>> fields;  // in file order, without ShipCount and ShipId<j>
    std::vector<std::string> ships;                           // ShipId<j> in j order
    std::string Get(const std::string& field, const std::string& def = "") const;
    void Set(const std::string& field, const std::string& value);
};
constexpr int kMaxDivisions = 200, kMaxDivisionShips = 40;
// The section's lines -> divisions 0..CampDivNo-1. strict (a player's list on the host): only the game's own fields,
// integers except Name and Abbreviation (text of at most 200 bytes without CR/LF/NUL), no key twice, at most
// kMaxDivisions divisions of at most kMaxDivisionShips ships; anything else fails (err) and nothing is taken.
// Not strict: what the game would not read is ignored.
bool ParseDivisions(const std::vector<std::string>& lines, std::vector<CampDiv>& out, bool strict, std::string& err);
// The section's lines for `divs`: divisions numbered 0..n-1, ShipCount recomputed.
std::vector<std::string> DivisionLines(const std::vector<CampDiv>& divs);
// Drops a division whose CDivId repeats an earlier one's and a ship id an earlier division holds; with shipIds also
// every ship id not in it. LeadDivision becomes -1 where it names no division of the list.
struct DivPrune {
    int divisions = 0, ships = 0, leads = 0;  // repeated divisions dropped, ship ids removed, LeadDivision reset
};
DivPrune PruneDivisions(std::vector<CampDiv>& divs, const std::set<std::string>* shipIds);
// Ship<n>Id values of the .bcs section [Nation<K>Ships].
std::set<std::string> NationShipIds(const IniDoc& bcs, int K);
// The [CampaignDivisions] lines of an .off file's bytes (false: the file has no such section).
bool OffDivisionLines(const std::string& off, std::vector<std::string>& lines);
// `off` with the lines of its [CampaignDivisions] section replaced (the section appended if missing); every other
// byte of the file stays as it is ([Officers], BOM, line ends).
std::string WithDivisionLines(const std::string& off, const std::vector<std::string>& lines);
// A package's .off gets `divs` (nation K's divisions; empty: an empty list) pruned to K's ships in `bcs`
// (PruneDivisions). Returns the divisions written; summary: what was pruned.
int PutDivisions(std::string& off, const std::string& bcs, int K, std::vector<CampDiv> divs, std::string& summary);
// The same for a slot on disk (a joined player's PC): ownLines (the section's lines; nullptr: an empty list) into the
// slot's .off, pruned to nation K's ships of the slot's .bcs.
bool PutSlotDivisions(const std::wstring& slotDir, int slot, int K, const std::vector<std::string>* ownLines,
                      int& written, std::string& summary, std::string& err);
// Nation K's own divisions in a slot it plays (the slot's .off as the player's game saved it), renumbered: a division
// holding a ship that is not K's in the slot's .bcs is not K's (e.g. the host's, from an older host's month) and is
// left out (`foreign` counts them). False if the slot has no .off/.bcs.
bool OwnDivisionLines(const std::wstring& slotDir, int slot, int K, std::vector<std::string>& lines, int& foreign);

// Officers (0.8.0): the [Officers] section of a slot's .off as the game's writer makes it (the list of PlayerNation):
// OfficerNo=<n>, then for each officer i < n the keys Officer<i><Field> with the fields Name, Ability, AbilityKnown,
// Rank, YearsInRank, Special, Special2, BattleStars, Status, Fate and Id. Map areas name their commander by officer id
// (MapData [MapAreas] MapArea<i>Commander, -1 = none); ships by Ship<n>CommanderId of their nation's ships section.
struct OfficerRec {
    std::vector<std::pair<std::string, std::string>> fields;  // in file order, without "Officer<i>"
    std::string Get(const std::string& field, const std::string& def = "") const;
};
constexpr int kMaxOfficers = 5000, kMaxMapAreas = 1000;
// The section's lines -> officers 0..OfficerNo-1. strict (a player's list on the host): only the game's fields, each
// officer with an Id (a positive integer, no id twice), integers in the game's ranges except Name and Fate (text of at
// most 200 bytes without CR/LF/NUL), no key twice, at most kMaxOfficers officers; anything else fails (err) and nothing
// is taken. Not strict: what the game would not read is ignored.
bool ParseOfficers(const std::vector<std::string>& lines, std::vector<OfficerRec>& out, bool strict, std::string& err);
std::vector<std::string> OfficerLines(const std::vector<OfficerRec>& officers);  // renumbered 0..n-1
std::set<int> OfficerIds(const std::vector<OfficerRec>& officers);
// "MapArea<i>Commander=<id>" lines -> area index -> officer id (ids other than -1 only). strict: the key shape, an
// integer, an area index below kMaxMapAreas, no area twice, every id one of `ids`.
bool ParseAreaCommands(const std::vector<std::string>& lines, const std::set<int>& ids, std::map<int, int>& out,
                       bool strict, std::string& err);
std::vector<std::string> AreaCommandLines(const std::map<int, int>& cmd);
// A MapData file's area commanders (ids other than -1; with `ids`, only those in it).
std::map<int, int> MapAreaCommanders(const std::string& map, const std::set<int>* ids);
// `map` with every MapArea<i>Commander of [MapAreas] set to cmd[i], -1 for the areas cmd doesn't name; `changed`: the
// lines whose value changed.
std::string WithAreaCommanders(const std::string& map, const std::map<int, int>& cmd, int* changed = nullptr);
bool OffOfficerLines(const std::string& off, std::vector<std::string>& lines);  // false: no [Officers]
// `off` with the lines of its [Officers] section replaced (appended if missing); every other byte stays.
std::string WithOfficerLines(const std::string& off, const std::vector<std::string>& lines);
// Nation K's ships' commanders in a .bcs: Ship<n>Id -> Ship<n>CommanderId.
std::map<std::string, int> ShipCommanderIds(const IniDoc& bcs, int K);
// Puts back the commander ids of K's ships that `byShipId` names (ships no longer there are skipped); returns the
// lines changed.
int SetShipCommanderIds(IniDoc& bcs, int K, const std::map<std::string, int>& byShipId);
// K's ships whose commander id is not -1 and not one of `keep` get -1; returns how many.
int ClearShipCommanders(IniDoc& bcs, int K, const std::set<int>& keep);
// A joined player's turn and battle result: "officers.ini" = [Officers] of the slot's .off (the game saved its
// PlayerNation's list there) and [AreaCmd], the MapData's area commanders that are officers of that list.
bool BuildOfficersFile(const std::wstring& slotDir, int slot, std::string& out, int& officers, int& areas,
                       std::string& err);
// 0.8.0 (officers with simultaneous battles): a battle's officers merged per officer id into the host's record, as the
// battle's ships are (ApplyBattleResult): `sent` = the list the battle's package carried, `host` = the record now (a
// battle fought during the host's month: what the month did to the corps since the send), `battle` = the result's. A
// field the battle did not change keeps the record's value, one only the battle changed takes the battle's; one both
// changed: BattleStars the record's + the battle's change (>= 0), Status and Fate the record's when the record's
// officer is not active (the month killed, captured or retired him: that stands), any other field the battle's. An
// officer new in the battle's list is added; one it dropped goes unless the record changed him since the send; the
// record's officers that were not sent stay. With host == sent (a battle of the battle phase) the battle's list.
struct OfficerMergeNotes {
    int battle = 0;  // officers with a field the battle changed
    int month = 0;   // ... whose record (the host's month) had changed since the send as well
    int stars = 0;   // officers whose stars both changed: the record's + the battle's
    int fate = 0;    // officers the month made dead, captured or retired whose battle status was not taken
    int added = 0, dropped = 0, kept = 0;  // new in the battle; dropped by it; the record's alone, kept
    bool Any() const { return month || stars || fate || added || dropped || kept; }
    std::string Text() const;
};
std::vector<OfficerRec> MergeOfficers(const std::vector<OfficerRec>& sent, const std::vector<OfficerRec>& host,
                                      const std::vector<OfficerRec>& battle, OfficerMergeNotes* notes = nullptr);
// 0.8.1 (a live battle between two players with both sides' commanders): the second player's nation's officers come
// back from the first player's PC. Only what a battle changes is taken from `battle` into each officer `sent` holds:
// BattleStars up by at most 2, Status Active -> Dead (1) or POW (3) with its Fate; no officer is added or dropped, every
// other field stays `sent`'s. `changed`: the officers that took something. The result goes into MergeOfficers as the
// battle's list.
std::vector<OfficerRec> BattleOnlyOfficers(const std::vector<OfficerRec>& sent, const std::vector<OfficerRec>& battle,
                                           int* changed = nullptr);

}  // namespace saveio
