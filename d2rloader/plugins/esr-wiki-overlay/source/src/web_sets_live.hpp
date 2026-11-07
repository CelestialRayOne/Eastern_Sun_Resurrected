// Builds the Set Items page from sets.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_sets.py does: the result is the
// wiki-data.txt it is given with its "set-items" page rebuilt, ready for ParseDatabase. Pictures are not on the website,
// so each card keeps the picture the existing page gives it (matched by title and difficulty). Any change to the rules
// must be made in both places; tests/web_sets_live_test.cpp checks that they agree on a saved copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website page the page is built from.
inline constexpr const char* kWebSetsPageNames[1] = {"sets.htm"};

// `current` is the wiki data holding the existing "set-items" page. On success `out` is the whole wiki data with the page
// rebuilt. Fails, leaving `out` untouched, when the page is incomplete or does not look like the expected one (fewer
// than 200 pieces or 60 sets, or a missing class section), so a changed site never replaces good data with a broken page.
bool BuildWebSetsWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error);

}  // namespace esrwiki
