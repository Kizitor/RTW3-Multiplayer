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
bool ExtractBattleResult(const std::string& beforeBcs, const std::string& afterBcs, int K, int J, Bundle& out,
                         std::string& summary, std::string& err);
bool ApplyBattleResult(IniDoc& host, const std::string& sentBcs, int K, int J, const Bundle& result,
                       const std::vector<int>& allowed, std::string& summary, std::string& err, bool keepUsed = false);

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

}  // namespace saveio
