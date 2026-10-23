// The game's x87 transcendental routines: the same bits on every CPU in live battles (x87.cpp).
#pragma once
#include <string>

namespace x87 {

bool Install();            // finds and hooks the game's 11 transcendental routines (once; the first live battle)
bool Installed();          // all of them hooked: live battles can use the software maths
void SetSoft(bool on);     // the software versions (live battles) or the game's own (this CPU's) routines
bool Soft();
std::string Counts();      // calls per routine since ResetCounts ("Sin=12,Cos=3"; "none")
void ResetCounts();
long long TotalCalls();
std::string Bench();       // ns per call of this CPU's instructions and of the software (about 0.1 s)
std::string SelfTest();    // the software against this CPU's instructions on fixed arguments (max ulps)
std::string StubCheck();   // the hooks called as the game calls them: registers, stack and results (at Install)
std::string CpuVendor();   // "Intel", "AMD", ... (TEST ONLY: RTW3MP_TEST_CPU)
std::string TestSwitch();  // RTW3MP_TEST_X87 in force ("" none) and the results it nudged
void SetPerturb(const std::string& mode, long every);  // TEST ONLY (`ls x87 pert ext|dbl|off [N]`): as RTW3MP_TEST_X87

}  // namespace x87
