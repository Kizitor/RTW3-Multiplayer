#pragma once
#include "common.h"

// 0.9.0: each joined player's own aircraft (docs/ARCHITECTURE.md "Each joined player's aircraft (0.9.0)"). The game
// keeps one global list of aircraft types and one of air units (each entry names its nation by index) and gives its
// player, nation 0, the aircraft decisions: the Aircraft types screen (its Request, licences), the prototypes, private
// ventures and old-type reminders of the month, the air group screen, the Turn's air checks. A joined player's PC plays
// them in its nation's view (the nation list's entries 0 and K and every type's and unit's nation index swapped for the
// time), its turn carries the nation's types, units and request (aircraft.ini), and the host leaves the computer's
// aircraft decisions for that nation out of its month.
namespace acft {

bool Resolve();       // the game's routines, fields and cells by name and code (logs what is missing)
void InstallHooks();  // the screens' hooks (a joined player's PC) and the air month's two sites (host)
bool Available();     // this PC can play a joined nation's aircraft (hello `acft`)
bool HostAvailable();  // ... and keep them as a host (the air month's sites found as well)
bool InView();        // the aircraft view is in place

// ---- a joined player's PC
// A month package was loaded (hostAircraft: header `acft`, the host keeps this nation's aircraft).
void ClientSetMonth(int K, bool hostAircraft);
void ClientReset();
bool ClientActive();        // the host keeps this nation's aircraft and the nation's copy is open (PlayerNation = K)
bool ClientMonthRunning();  // the month's routines run now (game.cpp mutes the computer's development meanwhile)
// The month's start, in the nation's view: the request's countdown (the game's ResolveAircraftRequest at 0), the private
// venture (its chance as the game's month computes it for its player), the old types' reminders (CheckOldAircraftTypes).
// Returns what ran ("" when not active).
std::string ClientMonth(int K);
// The Turn's air checks at Submit (CheckAirBaseCapacity, CheckPlayerCarriersHaveAircraft): false = not submitted (why).
bool ClientSubmitChecks(std::string& why);

// ---- host
// After the month's reload of the merged turns: the joined nations' types and units, for the month's news.
void HostMonthStart();
// After DoEndOfTurn: the month's news of each joined nation's types and units (entering service, delays, improved
// models, reliability, re-equipped squadrons), kept until HostTakeNews.
void HostMonthEnd();
std::vector<std::wstring> HostTakeNews(int K);
// The air month's counters since the last call (" nation K: n reserve units kept, m units left as they are;").
std::string HostTakeAirMonth();

// ---- tests (bridge)
std::string Info(int nation);  // `aircraft [nation]`
// TEST ONLY: `acset req <months> [role]` (this PC's form: the request), `acset dev <type id> <months>` (a type's
// DevelopmentTime), `acset aidev <nation>` (the game's AIDevelopAircraftType for it, through the skip hook), `acset cap
// 0|1` (a joined PC's hello announces acft), `acset host 0|1` (the host keeps nations' aircraft).
std::string TestSet(const std::vector<std::string>& args);
bool TestCapOff();   // `acset cap 0` in force (session.cpp: the hello's `acft`)
bool TestHostOff();  // `acset host 0` in force (session.cpp: AircraftByPlayer)

}  // namespace acft
