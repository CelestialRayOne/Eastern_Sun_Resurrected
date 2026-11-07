// Builds the Runes page from the gems page (gems.htm) of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_runes.py does: the result is
// the wiki-data.txt it is given with its "runes" page replaced, ready for ParseDatabase. Any change to the rules
// must be made in both places; tests/web_runes_live_test.cpp checks that they agree on the saved copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website pages the Runes page is built from, in the order BuildWebRunesWikiData expects them: the gems page
// (the runes) and the cube recipes page (the recipes of the "How to Acquire Elemental Runes" table).
inline constexpr const char* kWebRunesPageNames[2] = {"gems.htm", "Eastern%20Sun%20Resurrected%20Cube%20Recipes.html"};

// `current` is the wiki data (the shipped wiki-data.txt, or what it has become through other website updates) into
// which the page is merged. `gemsPage` and `cubePage` are the two website pages as downloaded (UTF-8). On success
// `out` is the whole wiki data with the Runes page replaced. Fails, leaving `out` untouched, when a page is incomplete
// or does not look like the expected one (too few runes, a group missing, no elemental recipes on the cube page), so
// a changed site never replaces good data with a broken page.
bool BuildWebRunesWikiData(const std::string& current, const std::string& gemsPage, const std::string& cubePage,
                           std::string& out, std::string& error);

}  // namespace esrwiki
