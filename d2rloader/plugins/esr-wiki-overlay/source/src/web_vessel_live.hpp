// Builds the Vessel of Souls page from vessel_of_souls.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_vessel.py does: the result is the
// wiki-data.txt it is given with its "vessel-of-souls" page rebuilt, ready for ParseDatabase. The overview, the four tier
// tables and the completion table each become an entry. Any change to the rules must be made in both places;
// tests/web_vessel_live_test.cpp checks that they agree on a saved copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website page the page is built from.
inline constexpr const char* kWebVesselPageNames[1] = {"vessel_of_souls.htm"};

// `current` is the wiki data holding the existing "vessel-of-souls" page. On success `out` is the whole wiki data with the
// page rebuilt. Fails, leaving `out` untouched, when the page is incomplete or does not look like the expected one (not
// the five sections, or too few rows in one), so a changed site never replaces good data with a broken page.
bool BuildWebVesselWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error);

}  // namespace esrwiki
