#include "web_runes_live.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "cube_live.hpp"
#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

constexpr std::size_t kMinRunes = 80;
const char kColorLead[] = "\xC3\xBF" "c";  // the colour-code lead character, then 'c'

struct Group {
    const char* key;
    const char* title;
};
// The groups, in the order they are shown.
const Group kGroups[] = {{"Kanji", "Kanji Runes"}, {"LoD", "LoD Runes"}, {"Elemental", "Elemental Runes"}};

// The note after the runes. Its text is fixed (it is not on the website page); the table under it is built from the
// elemental rune recipes of the cube recipes page. \037 separates table cells.
const char* const kHowToAcquire[] = {
    "ENTRY\tHow to Acquire Elemental Runes\t",
    "BLOCK\t0\t\t\t\t\t\t0",
    "LINE\tT\tElemental Runes cannot be found as monster drops - they must be crafted using the Horadric Cube.",
    "LINE\tT\t1. Cube a Wo Rune together with a Full Rejuvenation Potion to create a Null Rune.",
    "LINE\tT\t2. Hell difficulty Treasure Goblins drop Crystal Shards. Cube 5 matching Crystal Shards to upgrade them into a Flawed Crystal, then cube 5 matching Flawed Crystals to upgrade into a named Catalyst Stone.",
    "LINE\tT\t3. Cube the Catalyst Stone together with a Null Rune to create the matching Elemental Rune.",
    "LINE\tT\tHeaven Rune and God Rune instead require four different Catalyst Stones together with a Null Rune.",
    "LINE\tC\tIngredients\037Result",
};


struct Rune {
    std::string name, level, color, group;
    std::vector<std::string> labels;
    std::vector<std::vector<Line>> cells;
};

const char* RuneGroup(const std::string& color) {
    if (color.empty()) return "LoD";
    return color[0] == '#' ? "Elemental" : "Kanji";
}

bool SameLines(const std::vector<Line>& a, const std::vector<Line>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i].color != b[i].color || a[i].text != b[i].text) return false;
    return true;
}

std::vector<Rune> ParseRunes(const std::string& html) {
    std::vector<Rune> runes;
    Rune current;
    bool have = false;
    bool haveLabels = false;
    std::vector<std::string> labels;
    for (const Span& rowSpan : Elements(html, "<tr", "</tr>")) {
        const std::string row = html.substr(rowSpan.begin, rowSpan.end - rowSpan.begin);
        std::vector<std::string> cells;
        for (const Span& cell : Elements(row, "<td", "</td>")) cells.push_back(row.substr(cell.begin, cell.end - cell.begin));
        std::size_t at = 0;
        std::string level;
        const std::string plain = cells.size() == 1 ? TextOf(cells[0]) : std::string();
        if (cells.size() == 1 && FindReqLvl(plain, at, level)) {
            have = false;
            std::string name = Trim(plain.substr(0, at));
            if (EndsWith(name, " Rune") && !level.empty()) {
                current = Rune();
                current.name = name;
                current.level = level;
                current.color = NameColor(cells[0]);
                have = true;
                haveLabels = false;
                labels.clear();
            }
            continue;
        }
        if (!have || cells.size() != 3) continue;
        if (HasHeightAttribute(row)) {
            for (const std::string& cell : cells) current.cells.push_back(PropertyLines(cell));
            if (haveLabels) current.labels = labels;
            else current.labels = {"Weapons / Gloves", "Helms / Boots", "Armor / Shields / Belts"};
            current.group = RuneGroup(current.color);
            runes.push_back(std::move(current));
            have = false;
        } else {
            labels.clear();
            for (const std::string& cell : cells) labels.push_back(TextOf(cell));
            haveLabels = true;
        }
    }
    return runes;
}

std::string ImageKey(const Rune& rune, const std::set<std::string>& taken) {
    std::string base = "items_misc_rune_";
    std::string word;
    bool pendingUnderscore = false;
    std::string slug;
    for (char ch : Lower(rune.name)) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            if (pendingUnderscore && !slug.empty()) slug += '_';
            pendingUnderscore = false;
            slug += ch;
        } else {
            pendingUnderscore = true;
        }
    }
    base += slug;
    if (rune.group == "LoD" && taken.count(base) != 0) base = base.substr(0, base.size() - 5) + "_lod_rune";
    return base;
}

// Equal neighbouring cells are shown once under their joined labels; three equal cells are "All Items".
std::vector<std::pair<std::string, std::vector<Line>>> Sections(const Rune& rune) {
    std::vector<std::pair<std::string, std::vector<Line>>> out;
    for (std::size_t i = 0; i < rune.cells.size() && i < rune.labels.size(); ++i) {
        if (rune.cells[i].empty()) continue;
        if (!out.empty() && SameLines(out.back().second, rune.cells[i])) out.back().first += " / " + rune.labels[i];
        else out.emplace_back(rune.labels[i], rune.cells[i]);
    }
    bool allSame = true;
    for (const auto& cell : rune.cells) if (!SameLines(cell, rune.cells[0])) allSame = false;
    if (out.size() == 1 && allSame) out[0].first = "All Items";
    return out;
}

// Table rows, as in the Cube Recipes window, for the cube recipes whose result is one of `names`, in the cube page's order
// (the Python twin reads the same rows from cube-wiki-data.txt).
std::vector<std::string> ElementalRecipes(const std::string& cubeWiki, const std::set<std::string>& names) {
    std::vector<std::string> rows;
    for (const std::string& line : SplitLines(cubeWiki)) {
        const std::vector<std::string> parts = SplitTabs(line);
        if (parts.size() < 3 || parts[0] != "LINE" || parts[1] != "R") continue;
        // The text after "LINE\tR\t": the original may hold more tabs, so take everything past the second one.
        const std::string text = line.substr(line.find('\t', line.find('\t') + 1) + 1);
        const std::size_t cut = text.find('\x1f');
        if (cut == std::string::npos || text.find('\x1f', cut + 1) != std::string::npos) continue;
        std::string result = text.substr(cut + 1);
        if (result.compare(0, 2, "\xC3\xBF") == 0 && result.size() >= 4) result = result.substr(4);
        if (names.count(result) == 0) continue;
        // The cube page's own cells: one ingredient per line, the result in blue.
        const std::string row = "LINE\tR\t" + text;
        if (std::find(rows.begin(), rows.end(), row) == rows.end()) rows.push_back(row);
    }
    return rows;
}

}  // namespace

bool BuildWebRunesWikiData(const std::string& current, const std::string& gemsPage, const std::string& cubePage,
                           std::string& out, std::string& error) {
    if (FindCI(gemsPage, "</html") == std::string::npos) {
        error = std::string(kWebRunesPageNames[0]) + " is incomplete";
        return false;
    }
    std::string cubeWiki;
    if (!BuildCubeWikiData(cubePage, cubeWiki, error)) return false;
    const std::vector<Rune> runes = ParseRunes(gemsPage);
    if (runes.size() < kMinRunes) {
        error = "only " + std::to_string(runes.size()) + " runes found; the website page looks broken";
        return false;
    }
    std::set<std::string> present;
    for (const Rune& rune : runes) present.insert(rune.group);
    if (present.size() != sizeof(kGroups) / sizeof(kGroups[0])) {
        error = "the website runes do not fall into the three groups";
        return false;
    }

    std::vector<std::vector<std::vector<std::string>>> entries(sizeof(kGroups) / sizeof(kGroups[0]));
    std::set<std::string> taken;
    for (const Rune& rune : runes) {
        const std::string key = ImageKey(rune, taken);
        taken.insert(key);
        std::vector<std::string> block;
        block.push_back("BLOCK\t1\t" + rune.name + "\t\t\t\t\t0");
        block.push_back("FIELD\tRequired Level\t" + rune.level);
        block.push_back("IMG\t" + key);
        for (const auto& section : Sections(rune)) {
            block.push_back(std::string("LINE\tS\t") + kColorLead + "0" + section.first + ":");
            for (const Line& line : section.second) block.push_back(std::string("LINE\tS\t") + kColorLead + line.color + line.text);
        }
        for (std::size_t g = 0; g < entries.size(); ++g) {
            if (rune.group == kGroups[g].key) entries[g].push_back(std::move(block));
        }
    }
    std::vector<std::string> page;
    page.push_back("PAGE\trunes\tRunes\tWiki\tcompact");
    for (std::size_t g = 0; g < entries.size(); ++g) {
        if (entries[g].empty()) continue;
        page.push_back(std::string("ENTRY\t") + kGroups[g].title + "\t");
        for (const auto& block : entries[g]) page.insert(page.end(), block.begin(), block.end());
    }
    std::set<std::string> elemental;
    for (const Rune& rune : runes) if (rune.group == "Elemental") elemental.insert(rune.name);
    const std::vector<std::string> recipes = ElementalRecipes(cubeWiki, elemental);
    if (recipes.empty()) {
        error = "the cube recipes have no elemental rune recipes";
        return false;
    }
    for (const char* line : kHowToAcquire) page.push_back(line);
    page.insert(page.end(), recipes.begin(), recipes.end());

    std::vector<std::string> lines = SplitLines(current);
    const bool trailing = !lines.empty() && lines.back().empty();
    if (trailing) lines.pop_back();
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < lines.size(); ++i) if (lines[i].compare(0, 5, "PAGE\t") == 0) starts.push_back(i);
    auto spanOf = [&](const std::string& id, std::size_t& begin, std::size_t& end) {
        for (std::size_t n = 0; n < starts.size(); ++n) {
            const std::vector<std::string> parts = SplitTabs(lines[starts[n]]);
            if (parts.size() > 1 && parts[1] == id) {
                begin = starts[n];
                end = n + 1 < starts.size() ? starts[n + 1] : lines.size();
                return true;
            }
        }
        return false;
    };
    std::size_t begin = 0, end = 0;
    std::vector<std::string> kept;
    if (spanOf("runes", begin, end)) {
        kept.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(begin));
        kept.insert(kept.end(), page.begin(), page.end());
        kept.insert(kept.end(), lines.begin() + static_cast<std::ptrdiff_t>(end), lines.end());
    } else {
        // A file without a Runes page gets it after "ascendancy" (else first).
        std::size_t at = starts.empty() ? lines.size() : starts[0];
        std::size_t b2 = 0, e2 = 0;
        if (spanOf("ascendancy", b2, e2)) at = e2;
        kept.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(at));
        kept.insert(kept.end(), page.begin(), page.end());
        kept.insert(kept.end(), lines.begin() + static_cast<std::ptrdiff_t>(at), lines.end());
    }
    std::string text;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (i != 0) text += '\n';
        text += kept[i];
    }
    if (trailing) text += '\n';
    out = std::move(text);
    return true;
}

}  // namespace esrwiki
