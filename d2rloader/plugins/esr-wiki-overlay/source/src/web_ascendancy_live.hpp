// Builds the Ascendancy page from ascendancies.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_ascendancy.py does: the result is
// the wiki-data.txt it is given with its "ascendancy" page rebuilt, ready for ParseDatabase. The Orb of Creation table of
// the existing page is kept, because the website does not have it. Any change to the rules must be made in both places;
// tests/web_ascendancy_live_test.cpp checks that they agree on a saved copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website page the page is built from.
inline constexpr const char* kWebAscendancyPageNames[1] = {"ascendancies.htm"};

// `current` is the wiki data holding the existing "ascendancy" page. On success `out` is the whole wiki data with the page
// rebuilt. Fails, leaving `out` untouched, when the page is incomplete or does not look like the expected one (not 15
// ascendancies with five tiers each), so a changed site never replaces good data with a broken page.
bool BuildWebAscendancyWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error);

}  // namespace esrwiki
