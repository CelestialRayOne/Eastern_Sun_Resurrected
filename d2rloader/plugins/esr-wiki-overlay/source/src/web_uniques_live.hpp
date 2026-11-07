// Builds the "Uniques (Website)" page from the unique item pages of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_uniques.py does:
// the result is the shipped wiki-data.txt with its "uniques-web" page replaced, ready for ParseDatabase.
// Any change to the rules must be made in both places; tests/web_uniques_live_test.cpp checks that they
// agree on the saved copies of the pages.
#pragma once

#include <string>
#include <vector>

namespace esrwiki {

// The website pages, in the order BuildWebUniquesWikiData expects them.
inline constexpr const char* kWebUniquePageNames[4] = {"unique_others.htm", "unique_weapons.htm", "unique_armors.htm",
                                                      "unique_mythicals.htm"};

// `shipped` is the wiki-data.txt shipped with the plugin, into which the page is merged. `pages` holds the four
// website pages as downloaded (UTF-8), in kWebUniquePageNames order. `artTable` is the text of
// web-unique-art.txt: the art, tier and weapon family the website does not give, written from the mod's own
// tables by the extractor (empty is allowed; cards then have no art). On success `out` is the whole
// wiki data with the website page replaced. Fails, leaving `out` untouched, when a page is incomplete or the
// layout is not the one expected (too few items, an unknown table heading), so a changed site never replaces
// good data with a broken page.
bool BuildWebUniquesWikiData(const std::string& shipped, const std::vector<std::string>& pages,
                             const std::string& artTable, std::string& out, std::string& error);

}  // namespace esrwiki
