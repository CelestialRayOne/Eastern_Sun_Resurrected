// Builds the Weapon Mastery page from weapon_mastery.html of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_mastery.py does: the result is
// the wiki-data.txt it is given with its "weapon-mastery" page rebuilt, ready for ParseDatabase. The overview, the
// general recipes and one entry per specialization variant come from the website's tables. Any change to the rules
// must be made in both places; tests/web_mastery_live_test.cpp checks that they agree on a saved copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website page the page is built from.
inline constexpr const char* kWebMasteryPageNames[1] = {"weapon_mastery.html"};

// `current` is the wiki data holding the existing "weapon-mastery" page. On success `out` is the whole wiki data with the
// page rebuilt. Fails, leaving `out` untouched, when the page is incomplete or does not look like the expected one (not
// the two tables, too few recipes or variants), so a changed site never replaces good data with a broken page.
bool BuildWebMasteryWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error);

}  // namespace esrwiki
