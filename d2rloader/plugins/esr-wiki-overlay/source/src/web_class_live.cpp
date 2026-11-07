#include "web_class_live.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "cube_live.hpp"

namespace esrwiki {
namespace {

// The colour code the page's stat lines start with: the UTF-8 for ÿ followed by "c3".
const char kStatColour[] = "\xC3\xBF" "c3";

// The item types a crafted item name can end with, most specific first.
const std::vector<std::string> kItemTypes = {"Helm", "Boots", "Gloves", "Belt", "Shield", "Torso", "Amulet", "Ring", "Weapon"};

std::string ItemType(const std::string& name) {
    for (const std::string& type : kItemTypes) {
        if (name.find(type) != std::string::npos) return type;
    }
    return std::string();
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        parts.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return parts;
}

// A stat as the page writes it: no colour code of the website's own, no leading space, and the page's colour.
std::string StatLine(const std::string& stat) {
    std::size_t at = 0;
    if (stat.compare(0, 2, "\xC3\xBF") == 0) {  // ÿ
        at = 2;
        if (stat.size() >= at + 3 && stat[at] == 'c' && stat[at + 1] >= '0' && stat[at + 1] <= '9') at += 2;
    }
    while (at < stat.size() && stat[at] == ' ') ++at;
    return std::string("LINE\tS\t") + kStatColour + stat.substr(at);
}

// The Class Crafting page as the wiki data holds it now: its span, its intro entry, each class's lead lines
// (the recipe line), and the art each item type uses.
struct ShippedPage {
    std::size_t begin = 0;  // the PAGE line
    std::size_t end = 0;    // the line after the page
    std::vector<std::string> intro;                    // the "Class Crafting" entry, verbatim
    std::map<std::string, std::vector<std::string>> lead;  // class -> its lines before the first item
    std::map<std::string, std::string> artByType;      // item type -> the image key its items use
    std::map<std::string, std::string> imageByTitle;   // block title -> its own IMG line (empty when it has none)
};

bool ReadShippedPage(const std::vector<std::string>& lines, ShippedPage& page, std::string& error) {
    page.begin = lines.size();
    page.end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].compare(0, 5, "PAGE\t") != 0) continue;
        const std::vector<std::string> parts = SplitTabs(lines[i]);
        if (page.begin == lines.size()) {
            if (parts.size() > 1 && parts[1] == "class-crafting") page.begin = i;
        } else {
            page.end = i;
            break;
        }
    }
    if (page.begin == lines.size()) {
        error = "the wiki data has no class-crafting page";
        return false;
    }
    std::string entry;  // the ENTRY the lines belong to
    bool leadDone = false;
    for (std::size_t i = page.begin + 1; i < page.end; ++i) {
        const std::string& line = lines[i];
        if (line.compare(0, 6, "ENTRY\t") == 0) {
            entry = SplitTabs(line)[1];
            leadDone = false;
            if (entry == "Class Crafting") page.intro.push_back(line);
            continue;
        }
        if (entry == "Class Crafting") {
            page.intro.push_back(line);
            continue;
        }
        if (line.compare(0, 8, "BLOCK\t1\t") == 0) {
            leadDone = true;
            const std::vector<std::string> parts = SplitTabs(line);
            const std::string type = ItemType(parts.size() > 2 ? parts[2] : std::string());
            const bool hasImage = i + 1 < page.end && lines[i + 1].compare(0, 4, "IMG\t") == 0;
            if (hasImage) page.imageByTitle[parts.size() > 2 ? parts[2] : std::string()] = lines[i + 1].substr(4);
            else page.imageByTitle.emplace(parts.size() > 2 ? parts[2] : std::string(), std::string());
            if (!type.empty() && hasImage) {
                page.artByType.emplace(type, lines[i + 1].substr(4));
            }
            continue;
        }
        if (!leadDone && !entry.empty()) page.lead[entry].push_back(line);
    }
    if (page.intro.empty()) {
        error = "the class-crafting page has no intro";
        return false;
    }
    return true;
}

}  // namespace

std::size_t CountClassCraftRecipes(const std::string& cubePage) {
    std::vector<ClassCraftSection> sections;
    std::string error;
    if (!ParseClassCraftSections(cubePage, sections, error)) return 0;
    std::size_t total = 0;
    for (const ClassCraftSection& section : sections) total += section.recipes.size();
    return total;
}

bool BuildWebClassCraftingWikiData(const std::string& current, const std::string& cubePage, std::string& out,
                                   std::string& error) {
    std::vector<ClassCraftSection> sections;
    if (!ParseClassCraftSections(cubePage, sections, error)) return false;
    const std::size_t recipes = CountClassCraftRecipes(cubePage);
    if (recipes < 40) {
        error = "the cube page has " + std::to_string(recipes) + " class craft recipes, which does not look right";
        return false;
    }

    const std::vector<std::string> lines = SplitLines(current);
    ShippedPage shipped;
    if (!ReadShippedPage(lines, shipped, error)) return false;

    // A class without its own lead lines uses Amazon's (the recipe line is the same for every class).
    const std::vector<std::string> fallbackLead =
        shipped.lead.count("Amazon") != 0 ? shipped.lead["Amazon"] : std::vector<std::string>{"BLOCK\t0\t\t\t\t\t\t0"};

    std::vector<std::string> page = {lines[shipped.begin]};
    page.insert(page.end(), shipped.intro.begin(), shipped.intro.end());
    for (const ClassCraftSection& section : sections) {
        std::string className = section.heading;
        const std::string suffix = " Craft";
        if (className.size() > suffix.size() && className.compare(className.size() - suffix.size(), suffix.size(), suffix) == 0) {
            className.resize(className.size() - suffix.size());
        }
        page.push_back("ENTRY\t" + className + "\t");
        const auto lead = shipped.lead.find(className);
        const std::vector<std::string>& leadLines = lead != shipped.lead.end() ? lead->second : fallbackLead;
        page.insert(page.end(), leadLines.begin(), leadLines.end());

        for (const ClassCraftRecipe& recipe : section.recipes) {
            if (recipe.output.empty()) continue;
            const std::string& crafted = recipe.output[0];
            // A weapon made from another class's base is named as the page has always named it.
            const bool otherBase = !recipe.input.empty() && recipe.input[0].find("other than") != std::string::npos;
            const std::string title = className + " " + crafted + (otherBase ? " (Other-Class Base)" : "");
            std::string type = ItemType(crafted);
            if (type.empty() && !recipe.input.empty()) type = ItemType(recipe.input[0]);
            page.push_back("BLOCK\t1\t" + title + "\t\t" + className + "\t\t0");
            // A card that already has a shipped image keeps its own; only a new card takes the art of its item type.
            const auto own = shipped.imageByTitle.find(title);
            if (own != shipped.imageByTitle.end()) {
                if (!own->second.empty()) page.push_back("IMG\t" + own->second);
            } else {
                const auto art = shipped.artByType.find(type);
                if (!type.empty() && art != shipped.artByType.end()) page.push_back("IMG\t" + art->second);
            }
            for (std::size_t i = 1; i < recipe.output.size(); ++i) page.push_back(StatLine(recipe.output[i]));
        }
    }

    std::vector<std::string> result(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(shipped.begin));
    result.insert(result.end(), page.begin(), page.end());
    result.insert(result.end(), lines.begin() + static_cast<std::ptrdiff_t>(shipped.end), lines.end());
    out.clear();
    for (const std::string& line : result) {
        out += line;
        out += '\n';
    }
    return true;
}

}  // namespace esrwiki
