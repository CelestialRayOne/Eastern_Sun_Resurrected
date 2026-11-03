// Builds the Endgame Maps page from endgame_maps.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_endgame.py does: the result is the
// wiki-data.txt it is given with its "endgame-maps" page rebuilt, ready for ParseDatabase. The website's colours become
// the overlay's colour letters; each boss keeps the picture the existing page gives it (matched by title).
// Any change to the rules must be made in both places; tests/web_endgame_live_test.cpp checks that they agree on a saved
// copy of the page.
#pragma once

#include <string>

namespace esrwiki {

// The website page the page is built from.
inline constexpr const char* kWebEndgamePageNames[1] = {"endgame_maps.htm"};

// `current` is the wiki data holding the existing "endgame-maps" page. On success `out` is the whole wiki data with the
// page rebuilt. Fails, leaving `out` untouched, when the page is incomplete or does not look like the expected one (not
// 18 map bosses, or no overview), so a changed site never replaces good data with a broken page.
bool BuildWebEndgameWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error);

}  // namespace esrwiki
