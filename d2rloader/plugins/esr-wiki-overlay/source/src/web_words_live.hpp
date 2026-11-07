// Builds the Runewords and Gemwords pages from runewords.htm, gemwords.htm and gems.htm of the ESR website.
//
// Platform-neutral (no Win32), so it can be tested on any host. It does what tools/web_words.py does: the result is
// the wiki-data.txt it is given with its "runewords" or "gemwords" page replaced, ready for ParseDatabase. Any change
// to the rules must be made in both places; tests/web_words_live_test.cpp checks that they agree on saved copies of
// the pages.
#pragma once

#include <string>

namespace esrwiki {

// The website pages each page is built from, in the order the builders expect them. The runewords' required level is
// the highest level of the runes and gems they use, which the gems page gives.
inline constexpr const char* kWebRunewordsPageNames[2] = {"runewords.htm", "gems.htm"};
inline constexpr const char* kWebGemwordsPageName = "gemwords.htm";

// `current` is the wiki data (the shipped wiki-data.txt, or what it has become through other website updates) into
// which the page is merged. `artTable` is the text of web-word-art.txt: the pictures the website does not give,
// written from the mod's own art by the extractor (empty is allowed; cards then have no pictures). On success `out`
// is the whole wiki data with the page replaced. Fails, leaving `out` untouched, when a page is incomplete or does not
// look like the expected one (too few recipes), so a changed site never replaces good data with a broken page.
bool BuildWebRunewordsWikiData(const std::string& current, const std::string& runewordsPage, const std::string& gemsPage,
                               const std::string& artTable, std::string& out, std::string& error);
bool BuildWebGemwordsWikiData(const std::string& current, const std::string& gemwordsPage, const std::string& artTable,
                              std::string& out, std::string& error);

}  // namespace esrwiki
