// Builds the Class Crafting wiki page from the class craft sections of the cube recipes web page.
//
// Platform-neutral (no Win32), so it can be tested on any host. The shipped page supplies what the website
// does not: the intro text, each class's recipe line, and the item art. A new item gets the art already used
// for items of the same type (helm, boots, ...), and no art when no item of that type has any.
#pragma once

#include <string>

namespace esrwiki {

// `current` is the wiki data in use (shipped or live) and `cubePage` is the cube recipes page as downloaded.
// On success `out` holds the whole wiki data with only the Class Crafting page replaced. Fails, leaving `out`
// untouched, when the page has too few class craft recipes, so a changed layout never empties the page.
bool BuildWebClassCraftingWikiData(const std::string& current, const std::string& cubePage, std::string& out,
                                   std::string& error);

// How many class craft recipes the cube page lists, for the updater's sanity check. Zero when the page has none.
std::size_t CountClassCraftRecipes(const std::string& cubePage);

}  // namespace esrwiki
