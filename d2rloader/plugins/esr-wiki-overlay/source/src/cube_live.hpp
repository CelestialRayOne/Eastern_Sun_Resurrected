// Builds the Horadric Cube recipes data from the cube recipes web page.
//
// Platform-neutral (no Win32), so it can be tested on any host. The result is the same text
// that tools/build_cube.py + tools/build_cube_wiki.py write to cube-wiki-data.txt, and is read
// by ParseDatabase. Any change to the rules must be made in both places; tests/cube_live_test.cpp
// checks that they agree on a saved copy of the page.
#pragma once

#include <string>
#include <vector>

namespace esrwiki {

// `html` is the page as downloaded (UTF-8). On success `out` holds the wiki data. Fails, leaving
// `out` untouched, when the page does not look like the cube recipes page (too few recipes or
// pages), so a changed layout never replaces good data with an empty panel.
bool BuildCubeWikiData(const std::string& html, std::string& out, std::string& error);

// One recipe of a class craft section, as the page lists its Input and Output cells.
struct ClassCraftRecipe {
    std::vector<std::string> input;   // the base item and the cube ingredients
    std::vector<std::string> output;  // the crafted item's name first, then its stat lines
};

// One class craft section of the Legacy Craft tab ("Amazon Craft" and so on), with its recipes in page order.
struct ClassCraftSection {
    std::string heading;
    std::vector<ClassCraftRecipe> recipes;
};

// The class craft sections of the cube recipes page. Fails, leaving `out` empty, when the page has none.
bool ParseClassCraftSections(const std::string& html, std::vector<ClassCraftSection>& out, std::string& error);

}  // namespace esrwiki
