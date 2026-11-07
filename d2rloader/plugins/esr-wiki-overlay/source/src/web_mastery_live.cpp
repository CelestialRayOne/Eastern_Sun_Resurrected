#include "web_mastery_live.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

const char kPageId[] = "weapon-mastery";
const char kPageTitle[] = "Weapon Mastery";
const char kPageGroup[] = "Endgame";
const char kOverview[] = "Overview";
const char kGeneral[] = "General Recipes";
const char kSpecializations[] = "Specializations";
const char kRecipeHeadings[] = "Ingredients\x1f" "Result";
const char kSkillHeadings[] = "Ingredients\x1f" "Skill\x1f" "Effect";
const char kSeparator[] = "\x1f";
const char kLineBreak[] = "\x1e";  // inside a table cell: one ingredient or result per line
const char kBlockLine[] = "BLOCK\t0\t\t\t\t\t\t0";
const char kMarker[] = "\xc3\xbf" "c";  // the colour-code marker (U+00FF then 'c')
const std::size_t kNone = std::string::npos;
const std::size_t kMinOverview = 4;
const std::size_t kMinRecipes = 2;
const std::size_t kMinVariants = 10;

struct Run {
    std::string code;  // the overlay colour letter, "" for the default colour
    std::string text;
};
using Line = std::vector<Run>;

struct Variant {
    std::string name;
    std::vector<std::string> rows;  // ingredients, skill and effect, joined with the separator
};

bool Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

// The website's colour names and hex values, as the overlay's colour letters.
std::string ColorCode(const std::string& name) {
    static const std::map<std::string, std::string> codes = {
        {"red", "R"}, {"orange", "O"}, {"gold", "K"}, {"yellow", "9"}, {"lightgray", "N"}, {"green", "G"},
        {"blue", "L"}, {"purple", "P"}, {"white", "W"}, {"gray", "Y"}, {"#bdb76b", "K"}, {"#928a70", "N"},
        {"#706850", "H"}};
    const auto found = codes.find(name);
    return found == codes.end() ? std::string() : found->second;
}

// The <br>-separated lines of a cell, each a list of runs (colour code and text).
std::vector<Line> RunsOf(const std::string& fragment) {
    std::vector<Line> lines;
    Line current;
    std::vector<std::string> stack;  // the colour name of each open <font>, "" when it has none
    std::size_t i = 0;
    while (i < fragment.size()) {
        if (fragment[i] == '<') {
            const std::size_t end = fragment.find('>', i);
            if (end == kNone) {
                ++i;
                continue;
            }
            const std::string token = Lower(fragment.substr(i, end - i + 1));
            if (StartsWith(token, "<br")) {
                lines.push_back(current);
                current.clear();
            } else if (StartsWith(token, "<font")) {
                std::string name;
                const std::size_t attr = token.find("color=\"");
                if (attr != kNone) {
                    const std::size_t close = token.find('"', attr + 7);
                    if (close != kNone) name = token.substr(attr + 7, close - attr - 7);
                }
                stack.push_back(name);
            } else if (StartsWith(token, "</font")) {
                if (!stack.empty()) stack.pop_back();
            }
            i = end + 1;
            continue;
        }
        const std::size_t end = fragment.find('<', i);
        const std::string piece = fragment.substr(i, (end == kNone ? fragment.size() : end) - i);
        std::string code;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (!it->empty()) {
                code = ColorCode(*it);
                break;
            }
        }
        current.push_back({code, Unescape(piece)});
        i = end == kNone ? fragment.size() : end;
    }
    lines.push_back(current);
    return lines;
}

// The text of a line, cleaned.
std::string Plain(const Line& runs) {
    std::string raw;
    for (const Run& run : runs) raw += run.text;
    return Clean(raw);
}

// A line as LINE text: colour runs wrapped in the overlay's colour markers.
std::string Rendered(const Line& runs) {
    std::string raw;
    for (const Run& run : runs) {
        if (run.code.empty()) {
            raw += run.text;
        } else {
            raw += kMarker + run.code + run.text + kMarker + "N";
        }
    }
    return Clean(raw);
}

// The lines of a cell that have text.
std::vector<Line> CellLines(const std::string& fragment) {
    std::vector<Line> out;
    for (const Line& runs : RunsOf(fragment)) {
        if (!Plain(runs).empty()) out.push_back(runs);
    }
    return out;
}

std::string JoinWith(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += separator;
        out += parts[i];
    }
    return out;
}

// (content) of each <tag>, in order.
std::vector<std::string> ContentsOf(const std::string& html, const std::string& tag) {
    std::vector<std::string> out;
    const std::string close = "</" + tag + ">";
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = FindCI(html, "<" + tag, at);
        if (open == kNone) break;
        const std::size_t gt = html.find('>', open);
        if (gt == kNone) break;
        const std::size_t end = FindCI(html, close, gt + 1);
        if (end == kNone) break;
        out.push_back(html.substr(gt + 1, end - gt - 1));
        at = end + close.size();
    }
    return out;
}

// (content) of each <td> or <th>, in order.
std::vector<std::string> CellsOf(const std::string& row) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t td = FindCI(row, "<td", at);
        const std::size_t th = FindCI(row, "<th", at);
        const std::size_t open = td == kNone ? th : (th == kNone ? td : std::min(td, th));
        if (open == kNone) break;
        const std::size_t gt = row.find('>', open);
        if (gt == kNone) break;
        const std::size_t tdClose = FindCI(row, "</td>", gt + 1);
        const std::size_t thClose = FindCI(row, "</th>", gt + 1);
        const std::size_t close = tdClose == kNone ? thClose : (thClose == kNone ? tdClose : std::min(tdClose, thClose));
        if (close == kNone) break;
        out.push_back(row.substr(gt + 1, close - gt - 1));
        at = close + 5;
    }
    return out;
}

// The rows of a table after its heading row, each as its cells. Fails unless the heading names `expected`.
bool HeadingAndRows(const std::string& table, const std::string& expected, std::vector<std::vector<std::string>>& rows,
                    std::string& error) {
    const std::vector<std::string> all = ContentsOf(table, "tr");
    const std::vector<std::string> head = all.empty() ? std::vector<std::string>() : CellsOf(all[0]);
    if (head.empty()) return Fail(error, "a table has no heading");
    const std::vector<Line> heading = CellLines(head[0]);
    const std::string title = heading.empty() ? std::string() : Plain(heading[0]);
    if (title != expected) return Fail(error, "the table " + title + " is not " + expected);
    rows.clear();
    for (std::size_t r = 1; r < all.size(); ++r) rows.push_back(CellsOf(all[r]));
    return true;
}

bool Blank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
}

// (skill, effect) pairs of a specialization's outcome cell. An all-orange line names a skill; the lines after it, up to
// the next skill, are its effect.
bool SkillsOf(const std::string& cell, std::vector<std::pair<std::string, std::string>>& pairs, std::string& error) {
    pairs.clear();
    std::vector<std::vector<std::string>> effects;
    for (const Line& runs : CellLines(cell)) {
        bool significant = false;
        bool allOrange = true;
        for (const Run& run : runs) {
            if (Blank(run.text)) continue;
            significant = true;
            if (run.code != "O") allOrange = false;
        }
        if (significant && allOrange) {
            pairs.push_back({Rendered(runs), std::string()});
            effects.emplace_back();
        } else if (!pairs.empty()) {
            effects.back().push_back(Rendered(runs));
        } else {
            return Fail(error, "a specialization has an effect with no skill");
        }
    }
    for (std::size_t i = 0; i < pairs.size(); ++i) pairs[i].second = JoinWith(effects[i], " ");
    return true;
}

bool Parse(const std::string& html, std::vector<std::string>& overview, std::vector<std::string>& recipes,
           std::vector<Variant>& variants, std::string& error) {
    if (FindCI(html, "</html") == kNone) return Fail(error, "the saved page is incomplete");
    const std::vector<std::string> tables = ContentsOf(html, "table");
    if (tables.size() < 3) return Fail(error, "the weapon mastery tables are missing");
    const std::vector<std::string> overviewRows = ContentsOf(tables[0], "tr");
    if (overviewRows.empty()) return Fail(error, "the overview is missing");
    const std::vector<std::string> overviewCells = CellsOf(overviewRows[0]);
    if (overviewCells.empty()) return Fail(error, "the overview is missing");
    for (const Line& runs : CellLines(overviewCells[0])) overview.push_back(Rendered(runs));
    if (overview.size() < kMinOverview) return Fail(error, "the overview is missing");

    std::vector<std::vector<std::string>> rows;
    if (!HeadingAndRows(tables[1], kGeneral, rows, error)) return false;
    for (const std::vector<std::string>& cells : rows) {
        if (cells.size() != 2) return Fail(error, "a general recipe does not have an ingredients and a result");
        std::vector<std::string> ingredients, results;
        for (const Line& runs : CellLines(cells[0])) ingredients.push_back(Rendered(runs));
        for (const Line& runs : CellLines(cells[1])) results.push_back(Rendered(runs));
        if (ingredients.empty() || results.empty()) {
            return Fail(error, "a general recipe is missing its ingredients or its result");
        }
        recipes.push_back(JoinWith(ingredients, kLineBreak) + kSeparator + JoinWith(results, kLineBreak));
    }

    if (!HeadingAndRows(tables[2], kSpecializations, rows, error)) return false;
    for (const std::vector<std::string>& cells : rows) {
        if (cells.size() != 2) return Fail(error, "a specialization does not have a name and an outcome");
        const std::vector<Line> lines = CellLines(cells[0]);
        if (lines.size() < 2) return Fail(error, "a specialization has no name and ingredients");
        std::vector<std::string> ingredients;
        for (std::size_t i = 1; i < lines.size(); ++i) ingredients.push_back(Rendered(lines[i]));
        const std::string name = Plain(lines[0]);
        std::vector<std::pair<std::string, std::string>> skills;
        if (!SkillsOf(cells[1], skills, error)) return false;
        if (skills.empty()) return Fail(error, name + " has no skills");
        Variant variant;
        variant.name = name;
        for (const auto& skill : skills) {
            variant.rows.push_back(JoinWith(ingredients, kLineBreak) + kSeparator + skill.first + kSeparator + skill.second);
        }
        variants.push_back(variant);
    }

    if (recipes.size() < kMinRecipes) return Fail(error, "the general recipes have too few rows");
    if (variants.size() < kMinVariants) return Fail(error, "the specializations have too few variants");
    return true;
}

void BuildPage(const std::vector<std::string>& overview, const std::vector<std::string>& recipes,
               const std::vector<Variant>& variants, std::vector<std::string>& out) {
    out.clear();
    out.push_back(std::string("PAGE\t") + kPageId + "\t" + kPageTitle + "\t" + kPageGroup);
    out.push_back(std::string("ENTRY\t") + kOverview + "\t");
    out.push_back(kBlockLine);
    for (const std::string& line : overview) out.push_back("LINE\tT\t" + line);
    out.push_back(std::string("ENTRY\t") + kGeneral + "\t");
    out.push_back(kBlockLine);
    out.push_back(std::string("LINE\tC\t") + kRecipeHeadings);
    for (const std::string& row : recipes) out.push_back("LINE\tR\t" + row);
    for (const Variant& variant : variants) {
        out.push_back("ENTRY\t" + variant.name + "\t");
        out.push_back(kBlockLine);
        out.push_back(std::string("LINE\tC\t") + kSkillHeadings);
        for (const std::string& row : variant.rows) out.push_back("LINE\tR\t" + row);
    }
}

bool HasPage(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) {
        if (line.compare(0, 5, "PAGE\t") != 0) continue;
        const std::vector<std::string> parts = SplitTabs(line);
        if (parts.size() > 1 && parts[1] == kPageId) return true;
    }
    return false;
}

}  // namespace

bool BuildWebMasteryWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error) {
    std::vector<std::string> overview, recipes;
    std::vector<Variant> variants;
    if (!Parse(page, overview, recipes, variants, error)) return false;
    if (!HasPage(SplitLines(current))) return Fail(error, "wiki-data.txt has no weapon-mastery page");
    std::vector<std::string> built;
    BuildPage(overview, recipes, variants, built);
    out = Splice(current, built, kPageId, "");
    return true;
}

}  // namespace esrwiki
