// Offline test of the battle-result merge (saveio::ApplyBattleResult), 0.8.0 simultaneous battles: the rules for the
// ship fields that both the battle and the host's save changed since the battle was sent (CrewQuality and BattleStars
// add the battle's change to the host's value, the battle's log entries follow the host's, RepairTime the larger, Used
// the host's for a battle fought during the month, any other field the battle's), ships sunk or gone, new ships, the
// losses' deltas; and today's merge of a battle sent from the month-end save (the host unchanged since the send). The
// officers' merge (saveio::MergeOfficers, 0.8.0 integration): a battle's officers per officer id into the host's record.
// Built and run by tools\merge_test.bat (no game needed). Optional: merge_test.exe <snapshot.bcs> <month-end.bcs> runs
// a made-up battle on a real snapshot of a month and its month-end save (the sb80 POC's files) and checks the rules on
// every ship of both nations.
#include "../src/mod/saveio.h"

#include <algorithm>
#include <cstdio>

static int g_fail = 0, g_pass = 0;

static void Check(const char* what, bool ok, const std::string& detail = "") {
    (ok ? g_pass : g_fail)++;
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
}

// A ship block's lines for section Nation<n>Ships: Ship<i><Field>=<value>.
struct Ship {
    std::string id;
    std::vector<std::pair<std::string, std::string>> f;
};

static std::string Section(const std::vector<Ship>& ships) {
    std::string s;
    for (size_t i = 0; i < ships.size(); i++) {
        s += "Ship" + std::to_string(i) + "Id=" + ships[i].id + "\n";
        for (auto& kv : ships[i].f) s += "Ship" + std::to_string(i) + kv.first + "=" + kv.second + "\n";
    }
    return s;
}

static std::string Bcs(const std::vector<Ship>& k1, const std::vector<Ship>& j3, int lossK = 0) {
    std::string s = "[General]\nYear=1900\nMonth=3\n";
    for (int n = 0; n < 9; n++) s += "[Nation" + std::to_string(n) + "]\nVP=10\nPrestige=5\nShipCount=0\n";
    s += "[Nation1Ships]\n" + Section(k1) + "[Nation3Ships]\n" + Section(j3);
    s += "[Nation1Losses]\nBB=" + std::to_string(lossK) + "\n[VERSION]\nVersion=1\n";
    return s;
}

static Bundle Result(const std::vector<Ship>& k1, const std::vector<Ship>& j3, int lossK, int dVP) {
    Bundle b;
    b.Add("sec:Nation1Ships", Section(k1));
    b.Add("sec:Nation3Ships", Section(j3));
    b.Add("sec:Nation1Losses", "BB=" + std::to_string(lossK) + "\n");
    KV d;
    d["dVP_K"] = std::to_string(dVP);
    d["dVP_J"] = "0";
    d["dPrestige_K"] = "0";
    d["dMorale_K"] = "0";
    b.Add("deltas", KVEncode(d));
    return b;
}

// The merged ship's fields by id (section Nation<n>Ships).
static std::map<std::string, std::string> Fields(const IniDoc& d, int n, const std::string& id) {
    std::map<std::string, std::string> out;
    const IniSection* s = d.Find("Nation" + std::to_string(n) + "Ships");
    if (!s) return out;
    std::map<int, std::map<std::string, std::string>> by;
    for (auto& l : s->lines) {
        if (l.compare(0, 4, "Ship") != 0) continue;
        size_t p = 4;
        while (p < l.size() && isdigit((unsigned char)l[p])) p++;
        size_t eq = l.find('=');
        if (p == 4 || eq == std::string::npos) continue;
        by[atoi(l.c_str() + 4)][l.substr(p, eq - p)] = l.substr(eq + 1);
    }
    for (auto& kv : by)
        if (kv.second["Id"] == id) return kv.second;
    return out;
}

static Ship S(const std::string& id, int cq, int stars, int repair, int used, const std::string& status,
              const std::vector<std::string>& log) {
    Ship s{id, {{"Name", "Ship " + id},
                {"CrewQuality", std::to_string(cq)},
                {"RepairTime", std::to_string(repair)},
                {"Used", std::to_string(used)},
                {"Status", status},
                {"BattleStars", std::to_string(stars)},
                {"NumberOfLogEntries", std::to_string(log.size())}}};
    for (size_t i = 0; i < log.size(); i++) s.f.push_back({"LogEntry" + std::to_string(i), log[i]});
    return s;
}

static void Synthetic() {
    std::vector<std::string> l0 = {"January 1899 Laid down.", ""};
    std::vector<std::string> lm = {"January 1899 Laid down.", "", "March 1900 The host's battle.", ""};
    std::vector<std::string> lb = {"January 1899 Laid down.", "", "March 1900 Took part in a battle.", "In action.", ""};
    // sent (the snapshot), the month's end (host), the battle's result
    std::vector<Ship> sentK = {S("101", 6, 0, 0, 0, "0", l0), S("102", 6, 0, 2, 0, "0", l0), S("103", 24, 0, 0, 0, "0", l0),
                               S("104", 5, 0, 0, 0, "0", l0), S("105", 5, 0, 0, 0, "0", l0), S("106", -3, 1, 4, 0, "0", l0)};
    std::vector<Ship> sentJ = {S("301", 6, 0, 0, 0, "0", l0), S("302", 7, 0, 0, 0, "0", l0)};
    std::vector<Ship> hostK = {S("101", 7, 1, 0, 0, "1", lm), S("102", 7, 0, 1, 0, "0", l0), S("103", 25, 0, 0, 0, "0", l0),
                               S("106", -2, 1, 6, 0, "0", l0), S("107", 0, 0, 0, 0, "0", {"March 1900 Laid down.", ""})};
    // (104 sunk in the month, 105 too; 107 new in the month)
    std::vector<Ship> hostJ = {S("301", 7, 0, 0, 0, "0", l0), S("302", 8, 0, 0, 0, "0", l0)};
    std::vector<Ship> resK = {S("101", 7, 2, 3, 1, "2", lb), S("102", 5, 0, 5, 0, "0", l0), S("103", 26, 0, 0, 0, "0", l0),
                              S("104", 6, 0, 0, 0, "0", l0), S("106", -3, 1, 3, 0, "0", l0)};
    // (105 sunk in the battle too)
    std::vector<Ship> resJ = {S("302", 8, 0, 0, 0, "0", l0)};  // 301 sunk in the battle
    std::string sent = Bcs(sentK, sentJ, 1);
    IniDoc host = ParseIni(Bcs(hostK, hostJ, 3));
    Bundle res = Result(resK, resJ, 2, 4);
    std::string summary, err;
    saveio::MergeNotes notes;
    bool ok = saveio::ApplyBattleResult(host, sent, 1, 3, res, {1, 3}, summary, err, true, nullptr, &notes);
    Check("a battle fought during the month: merged", ok, ok ? summary : err);
    auto s101 = Fields(host, 1, "101"), s102 = Fields(host, 1, "102"), s103 = Fields(host, 1, "103");
    auto s106 = Fields(host, 1, "106"), s107 = Fields(host, 1, "107");
    Check("CrewQuality both +1 (6 -> month 7, battle 7): the month's + the battle's change = 8",
          s101["CrewQuality"] == "8", s101["CrewQuality"]);
    Check("CrewQuality month +1, battle -1 (6 -> 7, 5): 6", s102["CrewQuality"] == "6", s102["CrewQuality"]);
    Check("CrewQuality at the game's bound (24 -> 25, 26): 25", s103["CrewQuality"] == "25", s103["CrewQuality"]);
    Check("CrewQuality the battle did not change (-3 -> -2, -3): the month's -2", s106["CrewQuality"] == "-2",
          s106["CrewQuality"]);
    Check("BattleStars (0 -> month 1, battle 2): 3", s101["BattleStars"] == "3", s101["BattleStars"]);
    Check("RepairTime both (2 -> month 1, battle 5): the larger 5", s102["RepairTime"] == "5", s102["RepairTime"]);
    Check("RepairTime both (4 -> month 6, battle 3): the larger 6", s106["RepairTime"] == "6", s106["RepairTime"]);
    Check("RepairTime only the battle (0 -> 0, 3): 3", s101["RepairTime"] == "3", s101["RepairTime"]);
    Check("Used of a battle fought during the month (0 -> month 0, battle 1): the month's 0 (keepUsed)", s101["Used"] == "0", s101["Used"]);
    Check("another field both changed (Status 0 -> month 1, battle 2): the battle's 2", s101["Status"] == "2",
          s101["Status"]);
    Check("the log: the month's 4 entries, then the battle's 3 new ones (7)",
          s101["NumberOfLogEntries"] == "7" && s101["LogEntry2"] == "March 1900 The host's battle." &&
              s101["LogEntry3"] == "" && s101["LogEntry4"] == "March 1900 Took part in a battle." &&
              s101["LogEntry5"] == "In action." && s101["LogEntry6"] == "",
          "n=" + s101["NumberOfLogEntries"] + " [2]=" + s101["LogEntry2"] + " [4]=" + s101["LogEntry4"]);
    Check("a ship sunk in the month stays gone (104), also one sunk in both (105)",
          Fields(host, 1, "104").empty() && Fields(host, 1, "105").empty());
    Check("a ship new in the month stays (107)", s107["CrewQuality"] == "0", s107["Name"]);
    Check("a ship sunk in the battle is removed (J's 301); J's 302 merged (7 -> 8, 8: 9)",
          Fields(host, 3, "301").empty() && Fields(host, 3, "302")["CrewQuality"] == "9",
          Fields(host, 3, "302")["CrewQuality"]);
    Check("ShipCount follows (nation 1: 101, 102, 103, 106, 107)", host.Get("Nation1", "ShipCount", "") == "5",
          host.Get("Nation1", "ShipCount", ""));
    Check("Losses: the host's 3 + the battle's 1 = 4; VP 10 + 4", host.Get("Nation1Losses", "BB", "") == "4" &&
                                                                   host.Get("Nation1", "VP", "") == "14",
          host.Get("Nation1Losses", "BB", "") + " / " + host.Get("Nation1", "VP", ""));
    Check("the notes count the rules (CrewQuality 4 ships with one at the bound, stars 1, logs 1, repair 2, Status 1, "
          "2 gone in the month, 1 of them sunk in the battle too)",
          notes.added["CrewQuality"] == 4 && notes.clamped == 1 && notes.added["BattleStars"] == 1 && notes.logs == 1 &&
              notes.repair == 2 && notes.other["Status"] == 1 && notes.monthGone == 2 && notes.bothGone == 1,
          notes.Text());

    // Today's battle: sent from the month-end save, the host unchanged since: the battle's values, as before 0.8.0.
    IniDoc host2 = ParseIni(sent);
    saveio::MergeNotes n2;
    ok = saveio::ApplyBattleResult(host2, sent, 1, 3, res, {1, 3}, summary, err, false, nullptr, &n2);
    auto t101 = Fields(host2, 1, "101");
    Check("a battle sent from the month-end save (the host unchanged): the battle's values (CrewQuality 7, stars 2, "
          "Status 2, the battle's 5 log entries)",
          ok && t101["CrewQuality"] == "7" && t101["BattleStars"] == "2" && t101["Status"] == "2" &&
              t101["NumberOfLogEntries"] == "5" && t101["LogEntry2"] == "March 1900 Took part in a battle." &&
              Fields(host2, 1, "103")["CrewQuality"] == "26" && !n2.Any(),
          ok ? summary + " | " + n2.Text() : err);

    // Used without keepUsed (a battle sent after the month): the battle's value.
    IniDoc host3 = ParseIni(Bcs(hostK, hostJ, 3));
    ok = saveio::ApplyBattleResult(host3, sent, 1, 3, res, {1, 3}, summary, err, false);
    Check("Used without keepUsed (a battle sent after the month): the battle's 1", ok && Fields(host3, 1, "101")["Used"] == "1",
          Fields(host3, 1, "101")["Used"]);
}

static std::string ReadAll(const char* path) {
    std::string s;
    FILE* f = fopen(path, "rb");
    if (!f) return s;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

// A made-up battle on a real snapshot: every ship of nations K and J that is in both saves gets CrewQuality +1,
// BattleStars +1, RepairTime 7 and one log entry in the battle; the merge must give the month-end value + 1 for both
// counters, the larger RepairTime and the month's log followed by the battle's entry.
static void RealFiles(const char* snapPath, const char* endPath, int K, int J) {
    std::string snap = ReadAll(snapPath), end = ReadAll(endPath);
    if (snap.empty() || end.empty()) {
        Check("the snapshot and the month-end save read", false, std::string(snapPath) + ", " + endPath);
        return;
    }
    IniDoc s = ParseIni(snap);
    Bundle res;
    int changed = 0;
    for (int n : {K, J}) {
        const IniSection* sec = s.Find("Nation" + std::to_string(n) + "Ships");
        if (!sec) continue;
        std::map<int, std::map<std::string, std::string>> by;
        std::vector<std::pair<int, std::string>> order;
        for (auto& l : sec->lines) {
            size_t p = 4, eq = l.find('=');
            while (p < l.size() && isdigit((unsigned char)l[p])) p++;
            if (l.compare(0, 4, "Ship") != 0 || p == 4 || eq == std::string::npos) continue;
            int i = atoi(l.c_str() + 4);
            if (!by.count(i)) order.push_back({i, ""});
            by[i][l.substr(p, eq - p)] = l.substr(eq + 1);
        }
        std::string out;
        for (auto& [i, unused] : order) {
            auto& f = by[i];
            f["CrewQuality"] = std::to_string(atoi(f["CrewQuality"].c_str()) + 1);
            f["BattleStars"] = std::to_string(atoi(f["BattleStars"].c_str()) + 1);
            f["RepairTime"] = "7";
            int nl = atoi(f["NumberOfLogEntries"].c_str());
            f["LogEntry" + std::to_string(nl)] = "March 1900 The test battle.";
            f["LogEntry" + std::to_string(nl + 1)] = "";
            f["NumberOfLogEntries"] = std::to_string(nl + 2);
            for (auto& kv : f) out += "Ship" + std::to_string(i) + kv.first + "=" + kv.second + "\n";
            changed++;
        }
        res.Add("sec:Nation" + std::to_string(n) + "Ships", out);
    }
    KV d;
    d["dVP_K"] = d["dVP_J"] = d["dPrestige_K"] = d["dMorale_K"] = "0";
    res.Add("deltas", KVEncode(d));
    IniDoc host = ParseIni(end), month = ParseIni(end);
    std::string summary, err;
    saveio::MergeNotes notes;
    bool ok = saveio::ApplyBattleResult(host, snap, K, J, res, {K, J}, summary, err, true, nullptr, &notes);
    Check("the snapshot's battle merged into the month-end save", ok, ok ? summary + " | " + notes.Text() : err);
    if (!ok) return;
    int ships = 0, bad = 0, monthCq = 0;
    std::string first;
    for (int n : {K, J}) {
        const IniSection* sec = month.Find("Nation" + std::to_string(n) + "Ships");
        if (!sec) continue;
        std::set<std::string> ids;
        for (auto& l : sec->lines) {
            size_t p = 4;
            while (p < l.size() && isdigit((unsigned char)l[p])) p++;
            if (l.compare(0, 4, "Ship") == 0 && p > 4 && l.compare(p, 3, "Id=") == 0) ids.insert(l.substr(p + 3));
        }
        for (auto& id : ids) {
            auto m = Fields(month, n, id), o = Fields(s, n, id), h = Fields(host, n, id);
            if (o.empty()) continue;  // new in the month: kept as it was
            ships++;
            if (m["CrewQuality"] != o["CrewQuality"]) monthCq++;
            int cq = atoi(m["CrewQuality"].c_str()) + 1;
            if (m["CrewQuality"] != o["CrewQuality"]) cq = std::max(-25, std::min(25, cq));  // (both changed)
            int nl = atoi(m["NumberOfLogEntries"].c_str());
            int rt = m["RepairTime"] == o["RepairTime"] ? 7 : std::max(atoi(m["RepairTime"].c_str()), 7);
            bool good = h["CrewQuality"] == std::to_string(cq) &&
                        h["BattleStars"] == std::to_string(atoi(m["BattleStars"].c_str()) + 1) &&
                        h["RepairTime"] == std::to_string(rt) && h["NumberOfLogEntries"] == std::to_string(nl + 2) &&
                        h["LogEntry" + std::to_string(nl)] == "March 1900 The test battle." &&
                        h["LogEntry" + std::to_string(nl - 1)] == m["LogEntry" + std::to_string(nl - 1)] &&
                        h["Used"] == m["Used"] && h["Status"] == m["Status"];
            if (!good && first.empty())
                first = "nation " + std::to_string(n) + " ship " + id + ": CrewQuality " + o["CrewQuality"] + "/" +
                        m["CrewQuality"] + " -> " + h["CrewQuality"];
            bad += !good;
        }
    }
    Check("every ship of both nations: the month-end's CrewQuality and BattleStars + 1, RepairTime the larger, the "
          "month's log then the battle's entry, Used and Status the month's",
          ships > 0 && bad == 0,
          std::to_string(ships) + " ships (" + std::to_string(monthCq) + " with the month's own CrewQuality change), " +
              std::to_string(bad) + " wrong" + (first.empty() ? "" : "; first: " + first));
}

// 0.8.0 (officers with simultaneous battles): saveio::MergeOfficers, a battle's officers merged per officer id into the
// host's record.
static std::vector<saveio::OfficerRec> Officers(const std::vector<std::vector<std::string>>& rows) {
    // rows: {Id, Status, BattleStars, Fate, Rank}
    std::vector<std::string> lines = {"OfficerNo=" + std::to_string(rows.size())};
    for (size_t i = 0; i < rows.size(); i++) {
        std::string p = "Officer" + std::to_string(i);
        lines.push_back(p + "Name=Officer " + rows[i][0]);
        lines.push_back(p + "Ability=1");
        lines.push_back(p + "AbilityKnown=0");
        lines.push_back(p + "Rank=" + rows[i][4]);
        lines.push_back(p + "YearsInRank=2");
        lines.push_back(p + "Special=0");
        lines.push_back(p + "Special2=0");
        lines.push_back(p + "BattleStars=" + rows[i][2]);
        lines.push_back(p + "Status=" + rows[i][1]);
        lines.push_back(p + "Fate=" + rows[i][3]);
        lines.push_back(p + "Id=" + rows[i][0]);
    }
    std::vector<saveio::OfficerRec> out;
    std::string err;
    saveio::ParseOfficers(lines, out, true, err);
    return out;
}

static const saveio::OfficerRec* OfficerById(const std::vector<saveio::OfficerRec>& v, const std::string& id) {
    for (auto& o : v)
        if (o.Get("Id") == id) return &o;
    return nullptr;
}

static void OfficerMerge() {
    // sent: what the package carried; host: the record at the merge (the host's month since the send); battle: the result
    auto sent = Officers({{"11", "0", "1", "", "1"}, {"12", "0", "0", "", "2"}, {"13", "0", "2", "", "0"},
                          {"14", "0", "0", "", "1"}, {"15", "0", "3", "", "3"}, {"16", "0", "0", "", "0"},
                          {"19", "0", "0", "", "1"}});
    auto host = Officers({{"11", "0", "1", "", "1"}, {"12", "1", "0", "Killed when his ship struck a mine", "2"},
                          {"13", "0", "3", "", "0"}, {"14", "0", "0", "", "1"}, {"15", "0", "3", "", "3"},
                          {"16", "0", "0", "", "1"}, {"17", "0", "0", "", "0"}, {"19", "1", "0", "Lost at sea", "1"}});
    auto battle = Officers({{"11", "0", "2", "", "1"}, {"12", "0", "1", "", "2"}, {"13", "0", "4", "", "0"},
                            {"14", "3", "0", "Captured", "1"}, {"16", "0", "0", "", "0"}, {"18", "0", "0", "", "0"},
                            {"19", "3", "0", "Captured", "1"}});
    saveio::OfficerMergeNotes notes;
    auto m = saveio::MergeOfficers(sent, host, battle, &notes);
    auto get = [&](const std::string& id, const char* f) {
        const saveio::OfficerRec* o = OfficerById(m, id);
        return o ? o->Get(f) : std::string("(gone)");
    };
    Check("officers: a star of the battle (11: 1 -> 2) taken", get("11", "BattleStars") == "2", get("11", "BattleStars"));
    Check("officers: killed in the host's month after the send (12), a star in the battle: dead, his fate and the star",
          get("12", "Status") == "1" && get("12", "Fate") == "Killed when his ship struck a mine" &&
              get("12", "BattleStars") == "1",
          get("12", "Status") + " " + get("12", "Fate") + " " + get("12", "BattleStars"));
    Check("officers: stars both changed (13: 2 -> month 3, battle 4): 3 + 2 = 5", get("13", "BattleStars") == "5",
          get("13", "BattleStars"));
    Check("officers: captured in the battle (14): POW, the battle's fate", get("14", "Status") == "3" &&
          get("14", "Fate") == "Captured", get("14", "Status") + " " + get("14", "Fate"));
    Check("officers: one the battle's list dropped, unchanged in the record since the send (15): gone",
          OfficerById(m, "15") == nullptr);
    Check("officers: promoted in the record since the send (16: rank 0 -> 1), the battle unchanged: the record's 1",
          get("16", "Rank") == "1", get("16", "Rank"));
    Check("officers: new in the record since the send (17) kept, new in the battle (18) added",
          OfficerById(m, "17") && OfficerById(m, "18"), std::to_string(m.size()) + " officers");
    Check("officers: dead in the host's month (19), captured in the battle: dead, the month's fate",
          get("19", "Status") == "1" && get("19", "Fate") == "Lost at sea", get("19", "Status") + " " + get("19", "Fate"));
    Check("officers: the notes (5 changed by the battle, 3 of them in the month too, stars 1, fate 1, added 1, dropped "
          "1, kept 1)",
          notes.battle == 5 && notes.month == 3 && notes.stars == 1 && notes.fate == 1 && notes.added == 1 &&
              notes.dropped == 1 && notes.kept == 1,
          notes.Text());
    // a battle of the battle phase: the record is what the package carried: exactly the battle's list
    auto m2 = saveio::MergeOfficers(sent, sent, battle, nullptr);
    Check("officers: a battle sent from the record as it is (host == sent): the battle's list, in its order",
          saveio::OfficerLines(m2) == saveio::OfficerLines(battle),
          std::to_string(m2.size()) + " vs " + std::to_string(battle.size()));
    // dead in both (the month: a mine; the battle: his ship sunk): the month's fate
    auto b3 = Officers({{"12", "1", "0", "Went down with his ship", "2"}});
    auto m3 = saveio::MergeOfficers(Officers({{"12", "0", "0", "", "2"}}),
                                    Officers({{"12", "1", "0", "Killed when his ship struck a mine", "2"}}), b3, nullptr);
    Check("officers: dead in the month and in the battle: the month's fate",
          m3.size() == 1 && m3[0].Get("Fate") == "Killed when his ship struck a mine", m3.empty() ? "" : m3[0].Get("Fate"));
}

// 0.8.1 (fix-sunktwice): a ship the host's month lost since the send (sunk in the host's battle, torpedoed, ...) that
// the player's battle lost too is counted once: the host's ship kept as it is, the battle's losses counter of its type
// and its points (the player's PC's per-ship points, else estimated with the game's GetApproxVP) taken out; a ship the
// month lost that the battle only damaged keeps the month's state; the battle's own losses counted as before.
static Ship T(const std::string& id, const std::string& type, int disp, int repair, const std::string& fate, int cq,
              int stars, const std::vector<std::string>& log) {
    Ship s{id, {{"Name", "Ship " + id},
                {"ShipType", type},
                {"Displacement", std::to_string(disp)},
                {"CrewQuality", std::to_string(cq)},
                {"RepairTime", std::to_string(repair)},
                {"Fate", fate},
                {"BattleStars", std::to_string(stars)},
                {"Status", "0"},
                {"Used", "0"},
                {"NumberOfLogEntries", std::to_string(log.size())}}};
    for (size_t i = 0; i < log.size(); i++) s.f.push_back({"LogEntry" + std::to_string(i), log[i]});
    return s;
}

static std::string Losses(const std::map<std::string, int>& m) {
    std::string s;
    for (const char* k : {"BBLost", "BCLost", "BLost", "CALost", "CLLost", "CVLost", "CVLLost", "DDLost", "MSLost",
                          "AUXLost", "LTLost", "SSLost"}) {
        auto it = m.find(k);
        s += std::string(k) + "=" + std::to_string(it == m.end() ? 0 : it->second) + "\n";
    }
    return s;
}

// Nation 1 (K), 3 (J) and 5 (J's ally in the battle): their ships, losses and VP.
struct Save3 {
    std::vector<Ship> k1, j3, a5;
    std::map<std::string, int> l1, l3, l5;
    int vp1 = 0, vp3 = 0;
};

static std::string Bcs3(const Save3& s) {
    std::string out = "[General]\nYear=1915\nMonth=6\n";
    for (int n = 0; n < 9; n++)
        out += "[Nation" + std::to_string(n) + "]\nVP=" + std::to_string(n == 1 ? s.vp1 : n == 3 ? s.vp3 : 10) +
               "\nPrestige=5\nShipCount=0\n";
    out += "[Nation1Ships]\n" + Section(s.k1) + "[Nation1Losses]\n" + Losses(s.l1);
    out += "[Nation3Ships]\n" + Section(s.j3) + "[Nation3Losses]\n" + Losses(s.l3);
    out += "[Nation5Ships]\n" + Section(s.a5) + "[Nation5Losses]\n" + Losses(s.l5);
    return out + "[VERSION]\nVersion=1\n";
}

static Bundle Result3(const Save3& s, const KV& deltas) {
    Bundle b;
    b.Add("sec:Nation1Ships", Section(s.k1));
    b.Add("sec:Nation1Losses", Losses(s.l1));
    b.Add("sec:Nation3Ships", Section(s.j3));
    b.Add("sec:Nation3Losses", Losses(s.l3));
    b.Add("sec:Nation5Ships", Section(s.a5));
    b.Add("sec:Nation5Losses", Losses(s.l5));
    b.Add("deltas", KVEncode(deltas));
    return b;
}

static bool SameShip(const IniDoc& a, const IniDoc& b, int n, const std::string& id) {
    auto x = Fields(a, n, id), y = Fields(b, n, id);
    return !x.empty() && x == y;
}

static void SunkTwice() {
    using saveio::LogAdjustedVP;
    // the game's formulas
    Check("LogAdjustedVP as GetLogAdjustedVP: 0 -> 0, -5 -> 0, 1 -> Round(0.8 x 3) = 2, 25000 -> Round(0.8 x (474 + "
          "1666)) = 1712, 80000 -> Round(0.8 x (849 + 5333)) = 4946",
          LogAdjustedVP(0) == 0 && LogAdjustedVP(-5) == 0 && LogAdjustedVP(1) == 2 && LogAdjustedVP(25000) == 1712 &&
              LogAdjustedVP(80000) == 4946,
          std::to_string(LogAdjustedVP(1)) + " " + std::to_string(LogAdjustedVP(25000)) + " " +
              std::to_string(LogAdjustedVP(80000)));
    Check("ApproxShipVP as GetApproxVP: CA 12500 t 25000, KE 1800 t 3600, CV 20000 t 20000, TR 3000 t 3000",
          saveio::ApproxShipVP("CA", 12500) == 25000 && saveio::ApproxShipVP("KE", 1800) == 3600 &&
              saveio::ApproxShipVP("CV", 20000) == 20000 && saveio::ApproxShipVP("TR", 3000) == 3000);
    Check("LossCounterOf: CA CALost, KE MSLost, LT LTLost, AMC/TR AUXLost, MTB none",
          saveio::LossCounterOf("CA") == "CALost" && saveio::LossCounterOf("KE") == "MSLost" &&
              saveio::LossCounterOf("LT") == "LTLost" && saveio::LossCounterOf("AMC") == "AUXLost" &&
              saveio::LossCounterOf("TR") == "AUXLost" && saveio::LossCounterOf("MTB").empty());
    Check("BattlePointsCut: measured (raw 80000, a ship of 45100: 4946 - LogAdjustedVP(34900)); estimated alone (all "
          "the battle's 1712); within the battle's points (100); no ship 0; quick (each alone)",
          saveio::BattlePointsCut(4946, 80000, 45100) == 4946 - LogAdjustedVP(34900) &&
              saveio::BattlePointsCut(1712, -1, 25000) == 1712 && saveio::BattlePointsCut(100, 80000, 45100) == 100 &&
              saveio::BattlePointsCut(4946, 80000, 0) == 0 &&
              saveio::BattlePointsCut(5000, -1, 0, true, {25000, 2400}) == 1712 + LogAdjustedVP(2400),
          std::to_string(saveio::BattlePointsCut(4946, 80000, 45100)));

    std::vector<std::string> l0 = {"January 1910 Laid down.", ""};
    std::vector<std::string> lh = {"January 1910 Laid down.", "", "June 1915 Took part in Battle off Sicily.", "In action. Sunk.", ""};
    std::vector<std::string> lb = {"January 1910 Laid down.", "", "June 1915 Took part in Battle in the Ionian Sea.",
                                   "In action. Sunk.", ""};
    std::vector<std::string> ld = {"January 1910 Laid down.", "", "June 1915 Took part in Battle in the Ionian Sea.",
                                   "In action. Light damage.", ""};
    const std::string hostFate = "Sunk in battle  off Sicily with British ships.",
                      subFate = "Torpedoed and sunk by British submarine SS H 1",
                      batFate = "Sunk in battle  in the Ionian Sea with German ships.";
    Save3 sent;  // the snapshot the battle was sent from
    sent.k1 = {T("101", "DD", 1000, 0, "XXX", 3, 0, l0), T("102", "CL", 4000, 0, "XXX", 4, 0, l0)};
    sent.j3 = {T("301", "CA", 12500, 0, "XXX", 5, 0, l0), T("302", "DD", 1200, 0, "XXX", 5, 0, l0),
               T("303", "CL", 5000, 0, "XXX", 5, 0, l0),  T("304", "TR", 3000, 0, "XXX", 0, 0, l0),
               T("305", "DD", 1100, 0, "XXX", 5, 0, l0),  T("306", "B", 9000, 0, "XXX", 5, 0, l0)};
    sent.a5 = {T("501", "CL", 4500, 0, "XXX", 2, 0, l0), T("502", "AMC", 8000, 0, "XXX", 0, 0, l0)};
    sent.l3 = {{"CALost", 2}, {"DDLost", 1}};
    sent.vp1 = 100;
    sent.vp3 = 50;
    // The host's month since: 301 and 303 sunk in the host's battle, 302 torpedoed, nation 1's 101 struck a mine, 305
    // gone (handed over), the ally's 501 and 502 sunk; 306 damaged by the host's battle.
    Save3 host = sent;
    host.k1[0] = T("101", "DD", 1000, 100, "Struck a mine and sank", 3, 0, l0);
    host.j3[0] = T("301", "CA", 12500, 100, hostFate, 7, 1, lh);
    host.j3[1] = T("302", "DD", 1200, 100, subFate, 5, 0, l0);
    host.j3[2] = T("303", "CL", 5000, 100, hostFate, 7, 1, lh);
    host.j3[5] = T("306", "B", 9000, 4, "XXX", 7, 1, lh);
    host.j3.erase(host.j3.begin() + 4);  // (305)
    host.a5[0] = T("501", "CL", 4500, 100, hostFate, 4, 1, lh);
    host.a5[1] = T("502", "AMC", 8000, 100, hostFate, 0, 0, lh);
    host.l1 = {{"DDLost", 1}};
    host.l3 = {{"CALost", 3}, {"DDLost", 2}, {"CLLost", 1}};
    host.l5 = {{"CLLost", 1}};
    host.vp1 = 130;
    host.vp3 = 70;
    // The player's battle: 301, 302, 305, 101, 501, 502 sunk (lost twice), 303 damaged (lost in the month only), 102,
    // 304 and 306 sunk in the battle only (306 damaged in the host's month). The losses counters as the game counts
    // them: J's CA, DD x2, B, TR (AUXLost: one of the two main sides); K's DD, CL; the ally's CL (its AMC: none).
    Save3 res = sent;
    res.k1[0] = T("101", "DD", 1000, 100, batFate, 5, 1, lb);
    res.k1[1] = T("102", "CL", 4000, 100, batFate, 6, 1, lb);
    res.j3[0] = T("301", "CA", 12500, 100, batFate, 7, 1, lb);
    res.j3[1] = T("302", "DD", 1200, 100, batFate, 7, 1, lb);
    res.j3[2] = T("303", "CL", 5000, 3, "XXX", 7, 1, ld);
    res.j3[3] = T("304", "TR", 3000, 100, batFate, 0, 0, lb);
    res.j3[4] = T("305", "DD", 1100, 100, batFate, 7, 1, lb);
    res.j3[5] = T("306", "B", 9000, 100, batFate, 7, 1, lb);
    res.a5[0] = T("501", "CL", 4500, 100, batFate, 4, 1, lb);
    res.a5[1] = T("502", "AMC", 8000, 100, batFate, 0, 0, lb);
    res.l1 = {{"DDLost", 1}, {"CLLost", 1}};
    res.l3 = {{"CALost", 3}, {"DDLost", 3}, {"BLost", 1}, {"AUXLost", 1}};
    res.l5 = {{"CLLost", 1}};
    const std::string sentBcs = Bcs3(sent);
    const IniDoc hostDoc = ParseIni(Bcs3(host));
    const int dK = (int)LogAdjustedVP(80000), dJ = (int)LogAdjustedVP(12000);
    auto run = [&](const KV& extra, saveio::MergeNotes& notes, std::string& summary) {
        KV d;
        d["dVP_K"] = std::to_string(dK);
        d["dVP_J"] = std::to_string(dJ);
        d["dPrestige_K"] = "1";
        d["dMorale_K"] = "0";
        for (auto& kv : extra) d[kv.first] = kv.second;
        IniDoc h = hostDoc;
        std::string err;
        bool ok = saveio::ApplyBattleResult(h, sentBcs, 1, 3, Result3(res, d), {1, 3, 5}, summary, err, true, nullptr,
                                            &notes);
        if (!ok) summary = "error " + err;
        return h;
    };

    // 1) a 0.8.1 player's result: the battle's own per-ship points
    KV m;
    m["vp0"] = "80000";
    m["vp1"] = "12000";
    m["sunk"] = "101:0:2100,102:0:8100,301:1:27000,302:1:2600,304:1:3000,305:1:2300,306:1:19000,501:1:9200,502:1:4000";
    saveio::MergeNotes n1;
    std::string s1;
    IniDoc h1 = run(m, n1, s1);
    Check("sunk twice: merged", s1.compare(0, 5, "error") != 0, s1);
    Check("sunk twice: the ships the host's month lost stay as the host has them (the host's battle's 301 and 303, the "
          "torpedoed 302, nation 1's 101, the ally's 501 and 502: Fate, RepairTime, stars, crew, log)",
          SameShip(h1, hostDoc, 3, "301") && SameShip(h1, hostDoc, 3, "302") && SameShip(h1, hostDoc, 3, "303") &&
              SameShip(h1, hostDoc, 1, "101") && SameShip(h1, hostDoc, 5, "501") && SameShip(h1, hostDoc, 5, "502"),
          Fields(h1, 3, "301")["Fate"] + " | " + Fields(h1, 3, "301")["NumberOfLogEntries"]);
    Check("sunk twice: a ship gone from the host's save stays gone (305)", Fields(h1, 3, "305").empty());
    Check("sunk twice: the battle's own losses applied (102, 304; 306, which the host's month only damaged: sunk, "
          "RepairTime 100)",
          Fields(h1, 1, "102")["RepairTime"] == "100" && Fields(h1, 1, "102")["Fate"] == batFate &&
              Fields(h1, 3, "304")["RepairTime"] == "100" && Fields(h1, 3, "306")["RepairTime"] == "100" &&
              Fields(h1, 3, "306")["Fate"] == batFate,
          Fields(h1, 3, "306")["RepairTime"] + " " + Fields(h1, 3, "306")["Fate"]);
    auto L = [](const IniDoc& d, int n, const char* k) { return d.Get("Nation" + std::to_string(n) + "Losses", k, "?"); };
    Check("sunk twice: the losses counted once: J's CALost 3 (host 3, the battle's +1 for 301 not again), DDLost 2 (the "
          "battle's +2 for 302 and 305 not again), BLost 1 (306), AUXLost 1 (304), CLLost 1 (the host's)",
          L(h1, 3, "CALost") == "3" && L(h1, 3, "DDLost") == "2" && L(h1, 3, "BLost") == "1" &&
              L(h1, 3, "AUXLost") == "1" && L(h1, 3, "CLLost") == "1",
          L(h1, 3, "CALost") + " " + L(h1, 3, "DDLost") + " " + L(h1, 3, "BLost") + " " + L(h1, 3, "AUXLost"));
    Check("sunk twice: K's DDLost 1 (101 not again), CLLost 1 (102); the ally's CLLost 1 (501 not again), AUXLost 0 "
          "(the battle counted no loss of the ally's AMC 502)",
          L(h1, 1, "DDLost") == "1" && L(h1, 1, "CLLost") == "1" && L(h1, 5, "CLLost") == "1" && L(h1, 5, "AUXLost") == "0",
          L(h1, 1, "DDLost") + " " + L(h1, 1, "CLLost") + " " + L(h1, 5, "CLLost") + " " + L(h1, 5, "AUXLost"));
    long long cutK = LogAdjustedVP(80000) - LogAdjustedVP(80000 - 27000 - 2600 - 2300 - 9200 - 4000),
              cutJ = LogAdjustedVP(12000) - LogAdjustedVP(12000 - 2100);
    Check("sunk twice: the points of the ships sunk twice off the side that got them, from the player's PC's per-ship "
          "points (K: GetLogAdjustedVP(80000) - GetLogAdjustedVP(80000 - 45100); J: for K's 101)",
          n1.cutK == cutK && n1.cutJ == cutJ && hostDoc.Get("Nation1", "VP", "") == "130" &&
              h1.Get("Nation1", "VP", "") == std::to_string(130 + dK - cutK) &&
              h1.Get("Nation3", "VP", "") == std::to_string(70 + dJ - cutJ) && h1.Get("Nation1", "Prestige", "") == "6",
          std::to_string(n1.cutK) + "/" + std::to_string(n1.cutJ) + " VP " + h1.Get("Nation1", "VP", "") + "/" +
              h1.Get("Nation3", "VP", "") + " | " + s1);
    std::string counters;
    for (auto& t : n1.twice) counters += t.id + ":" + (t.counter.empty() ? "-" : t.counter) + ":" + std::to_string(t.side) + " ";
    Check("sunk twice: the notes (6 lost twice: 101 DDLost side 0, 305 (gone) DDLost, 301 CALost, 302 DDLost, 501 "
          "CLLost side 1 from the per-ship points, 502 no counter; 6 lost in the month; 1 gone; the battle's own 102, "
          "304, 306)",
          n1.twice.size() == 6 && counters == "101:DDLost:0 305:DDLost:1 301:CALost:1 302:DDLost:1 501:CLLost:1 502:-:1 " &&
              n1.monthLost == 6 && n1.monthGone == 1 && n1.bothGone == 1 && n1.lostNow.size() == 3,
          counters + "| " + n1.Text());

    // 2) a 0.8.0 player's result (no per-ship points): estimated with the game's GetApproxVP for the ships the battle
    // sank (its Fate "Sunk in ..."), J's and K's own; the ally's: its side not known, no points
    saveio::MergeNotes n2;
    std::string s2;
    IniDoc h2 = run(KV(), n2, s2);
    long long eK = saveio::BattlePointsCut(dK, -1, 25000 + 2400 + 2200), eJ = saveio::BattlePointsCut(dJ, -1, 2000);
    Check("sunk twice, a 0.8.0 player's result: the points estimated (K: CA 301 25000 + DD 302 2400 + DD 305 2200 raw "
          "off the raw total that gives the battle's points; J: K's DD 101 2000); the ally's 501/502 no points; the "
          "losses as with per-ship points",
          n2.cutK == eK && n2.cutJ == eJ && eK > 0 && eJ > 0 && h2.Get("Nation1", "VP", "") == std::to_string(130 + dK - eK) &&
              h2.Get("Nation3", "VP", "") == std::to_string(70 + dJ - eJ) && L(h2, 3, "DDLost") == "2" &&
              L(h2, 3, "CALost") == "3" && n2.twice.size() == 6 && n2.twice[4].points == 0 && n2.twice[4].side == -1 &&
              n2.twice[2].estimated && n2.twice[2].points == 25000,
          std::to_string(n2.cutK) + "/" + std::to_string(n2.cutJ) + " (expected " + std::to_string(eK) + "/" +
              std::to_string(eJ) + ") " + n2.cutHow);

    // 3) a raider battle auto-resolved (bQuick: the game's QuickResolveRaiderBattle gives each sunk ship
    // GetLogAdjustedVP(GetApproxVP) alone)
    KV q;
    q["bQuick"] = "1";
    saveio::MergeNotes n3;
    std::string s3;
    IniDoc h3 = run(q, n3, s3);
    long long qK = LogAdjustedVP(25000) + LogAdjustedVP(2400) + LogAdjustedVP(2200), qJ = LogAdjustedVP(2000);
    Check("sunk twice, a raider battle auto-resolved: each ship's GetLogAdjustedVP(GetApproxVP) alone",
          n3.cutK == std::min<long long>(qK, dK) && n3.cutJ == std::min<long long>(qJ, dJ),
          std::to_string(n3.cutK) + "/" + std::to_string(n3.cutJ) + " (expected " + std::to_string(qK) + "/" +
              std::to_string(qJ) + ")");

    // 4) encounters before the battle (the host's bVP_K/bVP_J: the battle's own points): the estimate from those
    KV e;
    e["bVP_K"] = std::to_string(dK - 600);
    e["bVP_J"] = std::to_string(dJ);
    saveio::MergeNotes n4;
    std::string s4;
    IniDoc h4 = run(e, n4, s4);
    Check("sunk twice after encounters: the estimate from the battle's own points (bVP_K), not the request's",
          n4.cutK == saveio::BattlePointsCut(dK - 600, -1, 29600) && n4.cutK != eK,
          std::to_string(n4.cutK) + " vs " + std::to_string(eK));

    // 5) the per-ship points without a ship lost twice: scuttled after a raider battle (no points of it in the battle,
    // a loss all the same): its loss counted once, no points taken off for it
    Save3 res5 = res;
    res5.j3[0] = T("301", "CA", 12500, 100, "Scuttled after battle damage", 7, 1, lb);
    KV m5 = m;
    m5["sunk"] = "102:0:8100,302:1:2600,304:1:3000,305:1:2300,306:1:19000";
    saveio::MergeNotes n5;
    std::string s5, err5;
    KV d5 = m5;
    d5["dVP_K"] = std::to_string(dK);
    d5["dVP_J"] = std::to_string(dJ);
    IniDoc h5 = hostDoc;
    bool ok5 = saveio::ApplyBattleResult(h5, sentBcs, 1, 3, Result3(res5, d5), {1, 3, 5}, s5, err5, true, nullptr, &n5);
    long long c5 = LogAdjustedVP(80000) - LogAdjustedVP(80000 - 2600 - 2300);
    Check("sunk twice, one of them scuttled after a raider battle (not in the per-ship points): its loss counted once "
          "(CALost 3), no points for it; 101, 501, 502 not in the points either (side or points unknown): only 302 and "
          "305",
          ok5 && L(h5, 3, "CALost") == "3" && n5.cutK == c5 && n5.cutJ == 0 && SameShip(h5, hostDoc, 3, "301"),
          std::to_string(n5.cutK) + "/" + std::to_string(n5.cutJ) + " (expected " + std::to_string(c5) + "/0)");

    // 6) a battle of the phase sent after the host's month (the host unchanged since the send): nothing lost twice, the
    // battle's losses and points as before 0.8.1
    IniDoc h6 = ParseIni(sentBcs);
    saveio::MergeNotes n6;
    std::string s6, err6;
    KV d6 = m;
    d6["dVP_K"] = std::to_string(dK);
    d6["dVP_J"] = std::to_string(dJ);
    bool ok6 = saveio::ApplyBattleResult(h6, sentBcs, 1, 3, Result3(res, d6), {1, 3, 5}, s6, err6, false, nullptr, &n6);
    Check("a battle sent from the month-end save (nothing lost in the month since): every loss and point of the battle "
          "counted (J's CALost 3, DDLost 3, K's VP 100 + dK)",
          ok6 && n6.twice.empty() && n6.cutK == 0 && L(h6, 3, "CALost") == "3" && L(h6, 3, "DDLost") == "3" &&
              h6.Get("Nation1", "VP", "") == std::to_string(100 + dK) && Fields(h6, 3, "301")["Fate"] == batFate,
          s6);
}

// 0.8.1 (both sides' commanders in a live battle between two players): the second player's nation's officers come
// back from the first player's PC; only what a battle changes is taken (saveio::BattleOnlyOfficers), then the merge.
static void OfficerBattleOnly() {
    auto sent = Officers({{"21", "0", "1", "", "1"}, {"22", "0", "0", "", "2"}, {"23", "0", "0", "", "0"},
                          {"24", "0", "2", "", "1"}, {"25", "0", "0", "", "3"}, {"26", "2", "0", "Retired", "1"}});
    // the other PC's list: 21 a star, 22 captured, 23 promoted and renamed (not a battle's change), 24 five stars more,
    // 25 dropped, 26 made active again, 27 added
    auto other = Officers({{"21", "0", "2", "", "1"}, {"22", "3", "0", " POW after sinking of HMS Test.", "2"},
                           {"23", "0", "0", "", "2"}, {"24", "0", "7", "", "1"}, {"26", "0", "0", "", "1"},
                           {"27", "0", "0", "", "0"}});
    int changed = 0;
    auto only = saveio::BattleOnlyOfficers(sent, other, &changed);
    auto get = [&](const std::string& id, const char* f) {
        const saveio::OfficerRec* o = OfficerById(only, id);
        return o ? o->Get(f) : std::string("(gone)");
    };
    Check("battle only: a star (21: 1 -> 2) taken", get("21", "BattleStars") == "2", get("21", "BattleStars"));
    Check("battle only: captured (22): POW with the battle's fate", get("22", "Status") == "3" &&
          get("22", "Fate") == " POW after sinking of HMS Test.", get("22", "Status") + get("22", "Fate"));
    Check("battle only: a promotion is not a battle's (23: rank stays 0)", get("23", "Rank") == "0", get("23", "Rank"));
    Check("battle only: at most two stars a battle (24: 2 -> 7 becomes 4)", get("24", "BattleStars") == "4",
          get("24", "BattleStars"));
    Check("battle only: an officer the other list dropped stays (25), one it added is not taken (27)",
          OfficerById(only, "25") && !OfficerById(only, "27"), std::to_string(only.size()) + " officers");
    Check("battle only: a retired officer stays retired (26)", get("26", "Status") == "2", get("26", "Status"));
    Check("battle only: three officers took something (21, 22, 24)", changed == 3, std::to_string(changed));
    // the record changed since the send (the host's month: 21 killed by a mine): the merge keeps that
    auto host = Officers({{"21", "1", "1", "Killed when his ship struck a mine", "1"}, {"22", "0", "0", "", "2"},
                          {"23", "0", "0", "", "0"}, {"24", "0", "2", "", "1"}, {"25", "0", "0", "", "3"},
                          {"26", "2", "0", "Retired", "1"}});
    auto m = saveio::MergeOfficers(sent, host, only, nullptr);
    const saveio::OfficerRec* o21 = OfficerById(m, "21");
    Check("battle only + merge: dead in the host's month after the send (21): dead, the month's fate, the battle's star",
          o21 && o21->Get("Status") == "1" && o21->Get("Fate") == "Killed when his ship struck a mine" &&
              o21->Get("BattleStars") == "2",
          o21 ? o21->Get("Status") + " " + o21->Get("Fate") + " " + o21->Get("BattleStars") : "(gone)");
    Check("battle only + merge: every officer of the record kept (6)", m.size() == 6, std::to_string(m.size()));
}

int main(int argc, char** argv) {
    Synthetic();
    OfficerMerge();
    SunkTwice();
    OfficerBattleOnly();
    if (argc >= 3) RealFiles(argv[1], argv[2], argc > 3 ? atoi(argv[3]) : 1, argc > 4 ? atoi(argv[4]) : 7);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
