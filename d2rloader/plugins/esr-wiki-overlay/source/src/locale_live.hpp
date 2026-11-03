// Builds a localized wiki from the website's localized pages.
//
// Platform-neutral (no Win32). The English builders (web_*_live) turn the English pages into wiki data; locale_align
// lines the localized pages up with the English ones and translates that data. Only the pages of the wiki that the
// website supplies are rebuilt this way; every other page (and the page titles) stays as the shipped localized
// wiki-data.txt has it. Text the localized pages do not translate stays as the English page has it.
#pragma once

#include <string>
#include <vector>

namespace esrwiki {

// Every website page the builders read, once each. A localized build is given the English and the localized copy of
// each, in this order.
inline constexpr const char* kLocalizedPageNames[] = {
    "unique_others.htm", "unique_weapons.htm", "unique_armors.htm", "unique_mythicals.htm", "gems.htm",
    "Eastern%20Sun%20Resurrected%20Cube%20Recipes.html", "runewords.htm", "gemwords.htm", "armors.htm", "weapons.htm",
    "prefixes.htm", "suffixes.htm", "quality_affixes.htm", "ascendancies.htm", "sets.htm", "endgame_maps.htm",
    "special_crafting_outcomes.htm", "skill_information.htm", "vessel_of_souls.htm", "kill_ledger.htm", "weapon_mastery.html"};
inline constexpr std::size_t kLocalizedPageCount = sizeof(kLocalizedPageNames) / sizeof(kLocalizedPageNames[0]);

struct LocalizedBuildInput {
    std::string shippedEnglish;    // wiki-data.txt
    std::string shippedLocalized;  // locales/<code>/wiki-data.txt
    std::vector<std::string> englishPages;    // kLocalizedPageNames order
    std::vector<std::string> localizedPages;  // the same pages from the <code>/ folder of the website
    std::string shippedCubeLocalized;  // locales/<code>/cube-wiki-data.txt (empty: no cube window data wanted)
    std::string uiStrings;         // locales/<code>/ui-strings.txt: English TAB translation, for the labels the website does not carry
    std::string uniqueArtTable, wordArtTable, baseArtTable;  // web-unique-art.txt, web-word-art.txt, web-base-art.txt
};

// `out` is the shipped localized wiki data with each page the website supplies replaced. A page whose builder rejects
// its pages keeps the shipped version and is named in `report` (one line each, also for the pages that worked).
// Fails only when the pages are unusable as a set.
// With `cubeOut` and shippedCubeLocalized, also builds the Horadric Cube window's data the same way (the English cube
// builder on the English cube recipes page, translated from the localized page); cubeOut stays empty if that fails.
bool BuildLocalizedWikiData(const LocalizedBuildInput& input, std::string& out, std::string& report, std::string& error,
                            std::string* cubeOut = nullptr);

}  // namespace esrwiki
