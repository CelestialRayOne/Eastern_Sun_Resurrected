// Builds the Kill Ledger page from kill_ledger.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_kill.py does: the result is the
// wiki-data.txt it is given with its "kill-ledger" page rebuilt, ready for ParseDatabase. The overview, the kill bonuses
// and the boss bonuses each become an entry. Any change to the rules must be made in both places;
// tests/web_kill_live_test.cpp checks that they agree on a saved copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website page the page is built from.
inline constexpr const char* kWebKillPageNames[1] = {"kill_ledger.htm"};

// `current` is the wiki data holding the existing "kill-ledger" page. On success `out` is the whole wiki data with the
// page rebuilt. Fails, leaving `out` untouched, when the page is incomplete or does not look like the expected one (not
// the two bonus tables, or too few rows in one), so a changed site never replaces good data with a broken page.
bool BuildWebKillWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error);

}  // namespace esrwiki
