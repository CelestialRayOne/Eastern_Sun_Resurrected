// Builds the Special Craft page from special_crafting_outcomes.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_special.py does: the result is
// the wiki-data.txt it is given with its "corruption" page rebuilt, ready for ParseDatabase. The four sections
// (Corruption, Anointment, Sanctification and Standard of Heroes Outcomes) each get their intro and one block per
// input, with one "Same Item: ..." line per outcome. Any change to the rules must be made in both places;
// tests/web_special_live_test.cpp checks that they agree on a saved copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website page the page is built from.
inline constexpr const char* kWebSpecialPageNames[1] = {"special_crafting_outcomes.htm"};

// `current` is the wiki data holding the existing "corruption" page. On success `out` is the whole wiki data with the
// page rebuilt. Fails, leaving `out` untouched, when the page is incomplete or does not look like the expected one (not
// the four sections, or too few outcomes in one), so a changed site never replaces good data with a broken page.
bool BuildWebSpecialWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error);

}  // namespace esrwiki
