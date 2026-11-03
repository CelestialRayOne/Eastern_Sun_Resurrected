#include "web_vessel_live.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

const char kPageId[] = "vessel-of-souls";
const char kPageTitle[] = "Vessel of Souls";
const char kPageGroup[] = "Endgame";
const char kOverview[] = "Overview";
const char kVessel[] = "Vessel of Souls";
const char kSameItem[] = "Same Item";
const char kCompletion[] = "Completion";
const char kTierHeadings[] = "Organs\x1f" "Effect\x1f" "Soul cost";
const char kCompletionHeadings[] = "Blemished ingredients\x1f" "Mythical base\x1f" "Bonus";
const char kSeparator[] = "\x1f";
const char kBlockLine[] = "BLOCK\t0\t\t\t\t\t\t0";
const char kMarker[] = "\xc3\xbf" "c";  // the colour-code marker (U+00FF then 'c')
const std::size_t kNone = std::string::npos;
const std::size_t kMinOverview = 5;

struct Expected {
    const char* title;
    std::size_t minimum;  // rows the section must have at least
};
const Expected kSections[] = {
    {"Tier 1", 100},
    {"Tier 2", 20},
    {"Tier 3", 15},
    {"Tier 4", 60},
    {kCompletion, 10},
};
const std::size_t kSectionCount = sizeof(kSections) / sizeof(kSections[0]);

struct Run {
    std::string code;  // the overlay colour letter, "" for the default colour
    std::string text;
};
using Line = std::vector<Run>;

struct Section {
    std::string title;
    std::vector<std::string> intro;
    std::vector<std::string> rows;  // cells joined with the separator
    std::string defaultCost;        // the soul cost the intro states, "" when it states none
};

bool Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

// The website's colour names and hex values, as the overlay's colour letters.
std::string ColorCode(const std::string& name) {
    static const std::map<std::string, std::string> codes = {
        {"red", "R"}, {"orange", "O"}, {"gold", "K"}, {"yellow", "9"}, {"lightgray", "N"}, {"green", "G"},
        {"blue", "L"}, {"purple", "P"}, {"white", "W"}, {"gray", "Y"}, {"#bdb76b", "K"}};
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
        std::size_t close = tdClose == kNone ? thClose : (thClose == kNone ? tdClose : std::min(tdClose, thClose));
        if (close == kNone) break;
        out.push_back(row.substr(gt + 1, close - gt - 1));
        at = close + 5;
    }
    return out;
}

// Organs or ingredients: the vessel itself is dropped. A line in brackets belongs to the item beside it: it follows the
// item before it, or comes before the next item when there is none.
std::string Ingredients(const std::string& cell) {
    std::vector<std::string> parts;
    std::vector<std::string> waiting;
    for (const Line& runs : CellLines(cell)) {
        const std::string text = Plain(runs);
        if (parts.empty() && waiting.empty() && text == kVessel) continue;
        if (!text.empty() && text[0] == '(') {
            if (!parts.empty()) {
                parts.back() += " " + Rendered(runs);
            } else {
                waiting.push_back(Rendered(runs));
            }
        } else {
            std::string item = Rendered(runs);
            if (!waiting.empty()) {
                item += " " + JoinWith(waiting, " ");
                waiting.clear();
            }
            parts.push_back(item);
        }
    }
    return JoinWith(parts, "\x1e");  // one organ per line, as the cube recipes show their inputs
}

// The effect (first line after "Same Item") and the notes after it, or false when the cell has no outcome.
bool Outcome(const std::string& cell, std::string& effect, std::string& notes) {
    std::vector<Line> lines = CellLines(cell);
    if (!lines.empty() && Plain(lines[0]) == kSameItem) lines.erase(lines.begin());
    if (lines.empty()) return false;
    effect = Rendered(lines[0]);
    std::vector<std::string> rest;
    for (std::size_t i = 1; i < lines.size(); ++i) rest.push_back(Rendered(lines[i]));
    notes = JoinWith(rest, " ");
    return true;
}

bool IsSectionTitle(const std::string& text, std::size_t& index) {
    for (std::size_t i = 0; i < kSectionCount; ++i) {
        if (text == kSections[i].title) {
            index = i;
            return true;
        }
    }
    return false;
}

// "consumes N souls" in the text: N as "N Souls", or "" when the text states none.
std::string DefaultCost(const std::string& text) {
    const std::string lower = Lower(text);
    const std::string key = "consumes ";
    std::size_t at = lower.find(key);
    if (at == kNone) return "";
    at += key.size();
    std::size_t end = at;
    while (end < lower.size() && lower[end] >= '0' && lower[end] <= '9') ++end;
    if (end == at || lower.compare(end, 6, " souls") != 0) return "";
    return lower.substr(at, end - at) + " Souls";
}

bool Parse(const std::string& html, std::vector<std::string>& overview, std::vector<Section>& sections, std::string& error) {
    if (FindCI(html, "</html") == kNone) return Fail(error, "the saved page is incomplete");
    const std::vector<std::string> tables = ContentsOf(html, "table");
    if (tables.empty()) return Fail(error, "the vessel tables are missing");
    const std::vector<std::string> firstRows = ContentsOf(tables[0], "tr");
    if (firstRows.empty()) return Fail(error, "the overview is missing");
    const std::vector<std::string> firstCells = CellsOf(firstRows[0]);
    if (firstCells.empty()) return Fail(error, "the overview is missing");
    for (const Line& runs : CellLines(firstCells[0])) overview.push_back(Rendered(runs));

    for (std::size_t t = 1; t < tables.size(); ++t) {
        const std::vector<std::string> rows = ContentsOf(tables[t], "tr");
        if (rows.empty()) continue;
        const std::vector<std::string> first = CellsOf(rows[0]);
        if (first.size() != 1) continue;
        const std::vector<Line> heading = CellLines(first[0]);
        if (heading.empty()) continue;
        std::size_t index = 0;
        if (!IsSectionTitle(Plain(heading[0]), index)) continue;

        Section section;
        section.title = Plain(heading[0]);
        std::vector<std::string> introTexts;
        for (std::size_t i = 1; i < heading.size(); ++i) {
            section.intro.push_back(Rendered(heading[i]));
            introTexts.push_back(Plain(heading[i]));
        }
        section.defaultCost = DefaultCost(JoinWith(introTexts, " "));
        const bool completion = section.title == kCompletion;

        for (std::size_t r = 1; r < rows.size(); ++r) {
            const std::vector<std::string> cells = CellsOf(rows[r]);
            if (cells.size() == 1) {
                // A line of intro under the heading; an input with no outcome after the rows is skipped.
                if (section.rows.empty()) {
                    for (const Line& runs : CellLines(cells[0])) section.intro.push_back(Rendered(runs));
                }
                continue;
            }
            if (cells.size() != 2) return Fail(error, "a " + section.title + " row does not have an input and an outcome");
            if (completion) {
                const std::vector<Line> lines = CellLines(cells[1]);
                if (lines.empty()) return Fail(error, "a completion row has no outcome");
                const std::size_t start = Plain(lines[0]) == kSameItem ? 1 : 0;
                if (start >= lines.size()) return Fail(error, "a completion row has no outcome");
                std::vector<std::string> bonus;
                for (std::size_t i = start + 1; i < lines.size(); ++i) bonus.push_back(Rendered(lines[i]));
                const std::string base = Rendered(lines[start]);
                section.rows.push_back(Ingredients(cells[0]) + kSeparator + base + kSeparator + JoinWith(bonus, " "));
            } else {
                std::string effect, notes;
                if (!Outcome(cells[1], effect, notes)) return Fail(error, "a " + section.title + " row has no outcome");
                const std::string cost = notes.empty() ? section.defaultCost : notes;
                section.rows.push_back(Ingredients(cells[0]) + kSeparator + effect + kSeparator + cost);
            }
        }
        sections.push_back(section);
    }

    if (sections.size() != kSectionCount) return Fail(error, "the vessel sections are missing");
    for (std::size_t i = 0; i < kSectionCount; ++i) {
        if (sections[i].title != kSections[i].title) return Fail(error, "the vessel sections are missing");
    }
    if (overview.size() < kMinOverview) return Fail(error, "the overview is missing");
    for (std::size_t i = 0; i < kSectionCount; ++i) {
        if (sections[i].intro.empty()) return Fail(error, sections[i].title + " has no intro");
        if (sections[i].rows.size() < kSections[i].minimum) return Fail(error, sections[i].title + " has too few rows");
    }
    return true;
}

void BuildPage(const std::vector<std::string>& overview, const std::vector<Section>& sections, std::vector<std::string>& out) {
    out.clear();
    out.push_back(std::string("PAGE\t") + kPageId + "\t" + kPageTitle + "\t" + kPageGroup);
    out.push_back(std::string("ENTRY\t") + kOverview + "\t");
    out.push_back(kBlockLine);
    for (const std::string& line : overview) out.push_back("LINE\tT\t" + line);
    for (const Section& section : sections) {
        out.push_back("ENTRY\t" + section.title + "\t");
        out.push_back(kBlockLine);
        for (const std::string& line : section.intro) out.push_back("LINE\tT\t" + line);
        out.push_back(std::string("LINE\tC\t") + (section.title == kCompletion ? kCompletionHeadings : kTierHeadings));
        for (const std::string& row : section.rows) out.push_back("LINE\tR\t" + row);
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

bool BuildWebVesselWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error) {
    std::vector<std::string> overview;
    std::vector<Section> sections;
    if (!Parse(page, overview, sections, error)) return false;
    if (!HasPage(SplitLines(current))) return Fail(error, "wiki-data.txt has no vessel-of-souls page");
    std::vector<std::string> built;
    BuildPage(overview, sections, built);
    out = Splice(current, built, kPageId, "");
    return true;
}

}  // namespace esrwiki
