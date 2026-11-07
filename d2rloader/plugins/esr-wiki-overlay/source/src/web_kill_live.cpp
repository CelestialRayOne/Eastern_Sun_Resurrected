#include "web_kill_live.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

const char kPageId[] = "kill-ledger";
const char kPageTitle[] = "Kill Ledger";
const char kPageGroup[] = "Endgame";
const char kOverview[] = "Overview";
const char kKills[] = "Kill Bonuses";
const char kBosses[] = "Boss Bonuses";
const char kLedger[] = "Kill Ledger";
const char kJustKilled[] = " just killed";
const char kKillsHeadings[] = "Kills\x1f" "Gem\x1f" "Bonus";
const char kBossHeadings[] = "Ingredients\x1f" "Boss\x1f" "Bonus";
const char kSeparator[] = "\x1f";
const char kLineBreak[] = "\x1e";  // one ingredient per line
const char kBlockLine[] = "BLOCK\t0\t\t\t\t\t\t0";
const char kMarker[] = "\xc3\xbf" "c";  // the colour-code marker (U+00FF then 'c')
const std::size_t kNone = std::string::npos;
const std::size_t kMinOverview = 5;

struct Expected {
    const char* title;
    std::size_t minimum;  // rows the table must have at least
};
const Expected kSections[] = {
    {kKills, 5},
    {kBosses, 10},
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

// The lines of an input cell without the "Kill Ledger" line that starts it.
std::vector<Line> InputLines(const std::string& cell) {
    std::vector<Line> lines = CellLines(cell);
    if (!lines.empty() && Plain(lines[0]) == kLedger) lines.erase(lines.begin());
    return lines;
}

// The bonus of an outcome cell: its lines after "Kill Ledger", joined with spaces.
bool BonusOf(const std::string& cell, std::string& bonus) {
    std::vector<Line> lines = CellLines(cell);
    if (!lines.empty() && Plain(lines[0]) == kLedger) lines.erase(lines.begin());
    if (lines.empty()) return false;
    std::vector<std::string> parts;
    for (const Line& runs : lines) parts.push_back(Rendered(runs));
    bonus = JoinWith(parts, " ");
    return true;
}

// "(At least N kills)" as "N kills".
bool KillCount(const std::string& text, std::string& kills) {
    const std::string prefix = "(At least ";
    const std::string suffix = " kills)";
    if (!StartsWith(text, prefix) || text.size() <= prefix.size() + suffix.size()) return false;
    const std::size_t digitsEnd = text.size() - suffix.size();
    if (text.compare(digitsEnd, suffix.size(), suffix) != 0) return false;
    const std::string digits = text.substr(prefix.size(), digitsEnd - prefix.size());
    for (char c : digits) {
        if (c < '0' || c > '9') return false;
    }
    kills = digits + " kills";
    return true;
}

bool KillRow(const std::string& inputs, const std::string& outcome, std::string& row, std::string& error) {
    const std::vector<Line> lines = InputLines(inputs);
    if (lines.size() != 2) return Fail(error, "a kill bonus row does not have a count and a gem");
    std::string kills;
    if (!KillCount(Plain(lines[0]), kills)) return Fail(error, "a kill bonus row has no kill count");
    std::string bonus;
    if (!BonusOf(outcome, bonus)) return Fail(error, "a kill bonus row has no bonus");
    row = kills + kSeparator + Rendered(lines[1]) + kSeparator + bonus;
    return true;
}

bool BossRow(const std::string& inputs, const std::string& outcome, std::string& row, std::string& error) {
    const std::vector<Line> lines = InputLines(inputs);
    if (lines.size() < 2) return Fail(error, "a boss bonus row has no ingredients and a boss");
    const std::string last = Plain(lines.back());
    const std::string suffix = kJustKilled;
    if (last.size() <= suffix.size() || last.compare(last.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return Fail(error, "a boss bonus row does not end with a boss that was just killed");
    }
    std::vector<std::string> ingredients;
    for (std::size_t i = 0; i + 1 < lines.size(); ++i) ingredients.push_back(Rendered(lines[i]));
    std::string bonus;
    if (!BonusOf(outcome, bonus)) return Fail(error, "a boss bonus row has no bonus");
    row = JoinWith(ingredients, kLineBreak) + kSeparator + last.substr(0, last.size() - suffix.size()) + kSeparator + bonus;
    return true;
}

bool Parse(const std::string& html, std::vector<std::string>& overview, std::vector<Section>& sections, std::string& error) {
    if (FindCI(html, "</html") == kNone) return Fail(error, "the saved page is incomplete");
    const std::vector<std::string> tables = ContentsOf(html, "table");
    if (tables.size() < 3) return Fail(error, "the kill ledger tables are missing");
    const std::vector<std::string> overviewRows = ContentsOf(tables[0], "tr");
    if (overviewRows.empty()) return Fail(error, "the overview is missing");
    const std::vector<std::string> overviewCells = CellsOf(overviewRows[0]);
    if (overviewCells.empty()) return Fail(error, "the overview is missing");
    for (const Line& runs : CellLines(overviewCells[0])) overview.push_back(Rendered(runs));

    for (std::size_t t = 1; t < 3; ++t) {
        const std::vector<std::string> rows = ContentsOf(tables[t], "tr");
        if (rows.empty()) return Fail(error, "a table has no heading");
        const std::vector<std::string> head = CellsOf(rows[0]);
        if (head.empty()) return Fail(error, "a table has no heading");
        Section section;
        if (head.size() == 1) {
            const std::vector<Line> heading = CellLines(head[0]);
            section.title = heading.empty() ? std::string() : Plain(heading[0]);
            for (std::size_t i = 1; i < heading.size(); ++i) section.intro.push_back(Rendered(heading[i]));
        } else {
            const std::vector<Line> title = CellLines(head[0]);
            section.title = title.empty() ? std::string() : Plain(title[0]);
            for (const Line& runs : CellLines(head[1])) section.intro.push_back(Rendered(runs));
        }
        if (section.title != kSections[t - 1].title) return Fail(error, "the table " + section.title + " is not a kill ledger table");
        for (std::size_t r = 1; r < rows.size(); ++r) {
            const std::vector<std::string> cells = CellsOf(rows[r]);
            if (cells.size() != 2) return Fail(error, "a " + section.title + " row does not have an input and an outcome");
            std::string row;
            const bool ok = t == 1 ? KillRow(cells[0], cells[1], row, error) : BossRow(cells[0], cells[1], row, error);
            if (!ok) return false;
            section.rows.push_back(row);
        }
        sections.push_back(section);
    }

    if (sections.size() != kSectionCount) return Fail(error, "the kill bonus and boss bonus tables are missing");
    for (std::size_t i = 0; i < kSectionCount; ++i) {
        if (sections[i].title != kSections[i].title) return Fail(error, "the kill bonus and boss bonus tables are missing");
    }
    if (overview.size() < kMinOverview) return Fail(error, "the overview is missing");
    for (std::size_t i = 0; i < kSectionCount; ++i) {
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
        out.push_back(std::string("LINE\tC\t") + (section.title == kKills ? kKillsHeadings : kBossHeadings));
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

bool BuildWebKillWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error) {
    std::vector<std::string> overview;
    std::vector<Section> sections;
    if (!Parse(page, overview, sections, error)) return false;
    if (!HasPage(SplitLines(current))) return Fail(error, "wiki-data.txt has no kill-ledger page");
    std::vector<std::string> built;
    BuildPage(overview, sections, built);
    out = Splice(current, built, kPageId, "");
    return true;
}

}  // namespace esrwiki
