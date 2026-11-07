// Builds the Mercenary/O-Skill page from skill_information.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_skills.py does: the result is the
// wiki-data.txt it is given with its "merc-oskill" page rebuilt, ready for ParseDatabase. The mercenary and O-skill tables
// each become an entry with their column headings and one row per line. Any change to the rules must be made in both
// places; tests/web_skills_live_test.cpp checks that they agree on a saved copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website page the page is built from.
inline constexpr const char* kWebSkillsPageNames[1] = {"skill_information.htm"};

// `current` is the wiki data holding the existing "merc-oskill" page. On success `out` is the whole wiki data with the
// page rebuilt. Fails, leaving `out` untouched, when the page is incomplete or does not look like the expected one (not
// both tables, or too few rows in one), so a changed site never replaces good data with a broken page.
bool BuildWebSkillsWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error);

}  // namespace esrwiki
