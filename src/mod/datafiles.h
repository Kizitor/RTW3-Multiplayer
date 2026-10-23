// The game data files a live battle depends on: their hashes, compared between the two PCs before a live battle
// (datafiles.cpp).
#pragma once
#include <string>
#include <vector>

namespace datafiles {

void StartScan();                       // hashes the files on a background thread (once, when the mod starts)
bool Ready(unsigned waitMs = 0);        // the scan is done (waits up to waitMs)
std::string Combined();                 // the hash of every file's name and content (16 hex digits; "" not ready)
int FileCount();
std::string Encoded();                  // "name:hash|" per file, for the other PC's comparison (one KV value)
// The files whose content differs, that only this PC has and that only the other PC has (their Encoded()).
std::vector<std::string> Differ(const std::string& theirs);
std::string NamesText(const std::vector<std::string>& names, size_t shown = 4);  // "Data\A.dat, Data\B.LYR (+3 more)"

// TEST ONLY (RTW3MP_TEST_FILELOG=1): every file the game opens for reading, logged once with the phase it was first
// opened in (pocfilelog).
void PocFileLog();
void SetPhase(const char* phase);
// TEST ONLY: RTW3MP_TEST_DATAFILE=<name>[;<name>...] (at the scan) or `ls testdatafile <names>` (SetTestDiff): this
// copy hashes those data files as if their content differed (a name that isn't there: as an extra file); `off`: the
// files as scanned again.
void SetTestDiff(const std::string& names);

}  // namespace datafiles
