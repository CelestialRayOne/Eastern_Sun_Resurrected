// Builds the Item Affixes page from prefixes.htm, suffixes.htm and quality_affixes.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_affixes.py does: the result is
// the wiki-data.txt it is given with its "item-affixes" page replaced (or added after "item-bases"), ready for
// ParseDatabase. Any change to the rules must be made in both places; tests/web_affixes_live_test.cpp checks that they
// agree on saved copies of the pages.
#pragma once

#include <string>

namespace esrwiki {

// The website pages the page is built from, in the order the builder expects them.
inline constexpr const char* kWebAffixesPageNames[3] = {"prefixes.htm", "suffixes.htm", "quality_affixes.htm"};

// `current` is the wiki data into which the page is merged. On success `out` is the whole wiki data with the page
// replaced. Fails, leaving `out` untouched, when a page is incomplete or does not look like the expected one (too few
// rows), so a changed site never replaces good data with a broken page.
bool BuildWebAffixesWikiData(const std::string& current, const std::string& prefixesPage, const std::string& suffixesPage,
                             const std::string& qualityPage, std::string& out, std::string& error);

}  // namespace esrwiki
