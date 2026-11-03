// Builds the Item Bases page from armors.htm and weapons.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_bases.py does: the result is
// the wiki-data.txt it is given with its "item-bases" page replaced, ready for ParseDatabase. Any change to the rules
// must be made in both places; tests/web_bases_live_test.cpp checks that they agree on saved copies of the pages.
#pragma once

#include <string>

namespace esrwiki {

// The website pages the page is built from, in the order the builder expects them.
inline constexpr const char* kWebBasesPageNames[2] = {"armors.htm", "weapons.htm"};

// `current` is the wiki data into which the page is merged. `artTable` is the text of web-base-art.txt: the pictures,
// tiers and weapon families the website does not give (empty is allowed; cards then have no pictures or tiers). On
// success `out` is the whole wiki data with the page replaced. Fails, leaving `out` untouched, when a page is incomplete
// or does not look like the expected one (too few items), so a changed site never replaces good data with a broken page.
bool BuildWebBasesWikiData(const std::string& current, const std::string& armorsPage, const std::string& weaponsPage,
                           const std::string& artTable, std::string& out, std::string& error);

}  // namespace esrwiki
