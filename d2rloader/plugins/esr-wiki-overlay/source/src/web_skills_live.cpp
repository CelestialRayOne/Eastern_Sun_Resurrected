#include "web_skills_live.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

const char kPageId[] = "merc-oskill";
const char kPageTitle[] = "Mercenary/O-Skill";
const char kPageGroup[] = "Endgame";
const char kBlockLine[] = "BLOCK\t0\t\t\t\t\t\t0";
const char kSeparator[] = "\x1f";  // the unit separator the overlay splits table cells on
const std::size_t kNone = std::string::npos;

struct ExpectedSection {
    const char* title;
    std::size_t columns;
    std::size_t minimum;  // rows the table must have at least
};
const ExpectedSection kSections[] = {
    {"Mercenaries", 3, 10},
    {"Oskills", 2, 20},
};

struct Section {
    std::string title;
    std::vector<std::string> headings;
    std::vector<std::string> rows;  // cells joined with the separator
};

bool Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

// re.sub(r"<[^>]+>", "", fragment)
std::string StripTags(const std::string& fragment) {
    std::string out;
    for (std::size_t i = 0; i < fragment.size(); ++i) {
        if (fragment[i] == '<') {
            const std::size_t gt = fragment.find('>', i + 1);
            if (gt != kNone && gt > i + 1) {
                i = gt;
                continue;
            }
        }
        out += fragment[i];
    }
    return out;
}

// The <br>-separated lines of a cell, cleaned, without empty ones.
std::vector<std::string> LinesOf(const std::string& cell) {
    std::vector<std::string> out;
    for (const std::string& part : SplitBr(cell)) {
        const std::string text = Clean(StripTags(part));
        if (!text.empty()) out.push_back(text);
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

std::string CellText(const std::string& cell) {
    return JoinWith(LinesOf(cell), " ");
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

bool IsSection(const std::string& text, std::size_t& index) {
    for (std::size_t i = 0; i < sizeof(kSections) / sizeof(kSections[0]); ++i) {
        if (text == kSections[i].title) {
            index = i;
            return true;
        }
    }
    return false;
}

bool Parse(const std::string& html, std::vector<std::string>& intro, std::vector<Section>& sections, std::string& error) {
    if (FindCI(html, "</html") == kNone) return Fail(error, "the saved page is incomplete");
    for (const std::string& table : ContentsOf(html, "table")) {
        const std::vector<std::string> rows = ContentsOf(table, "tr");
        if (rows.empty()) continue;
        const std::vector<std::string> first = ContentsOf(rows[0], "td");
        if (first.size() != 1) continue;
        const std::string text = CellText(first[0]);
        std::size_t index = 0;
        if (!IsSection(text, index)) {
            if (rows.size() == 1) intro = LinesOf(first[0]);
            continue;
        }
        // A section table: its title row, then the column headings, then one row per entry.
        Section section{text, {}, {}};
        for (std::size_t r = 1; r < rows.size(); ++r) {
            const std::vector<std::string> headings = ContentsOf(rows[r], "th");
            if (!headings.empty()) {
                section.headings.clear();
                for (const std::string& heading : headings) section.headings.push_back(CellText(heading));
                continue;
            }
            const std::vector<std::string> cells = ContentsOf(rows[r], "td");
            if (cells.size() != kSections[index].columns) {
                return Fail(error, "a " + text + " row does not have the expected columns");
            }
            std::vector<std::string> values;
            for (const std::string& cell : cells) values.push_back(CellText(cell));
            if (values[0].empty()) return Fail(error, "a " + text + " row has no name");
            section.rows.push_back(JoinWith(values, kSeparator));
        }
        sections.push_back(section);
    }
    if (sections.size() != sizeof(kSections) / sizeof(kSections[0])) {
        return Fail(error, "the mercenary and oskill tables are missing");
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].title != kSections[i].title) return Fail(error, "the mercenary and oskill tables are missing");
    }
    if (intro.empty()) return Fail(error, "the intro is missing");
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].headings.size() != kSections[i].columns) {
            return Fail(error, sections[i].title + " has no column headings");
        }
        if (sections[i].rows.size() < kSections[i].minimum) {
            return Fail(error, sections[i].title + " has too few rows");
        }
    }
    return true;
}

void BuildPage(const std::vector<std::string>& intro, const std::vector<Section>& sections, std::vector<std::string>& out) {
    out.clear();
    out.push_back(std::string("PAGE\t") + kPageId + "\t" + kPageTitle + "\t" + kPageGroup);
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const Section& section = sections[index];
        out.push_back("ENTRY\t" + section.title + "\t");
        out.push_back(kBlockLine);
        if (index == 0) {
            for (const std::string& line : intro) out.push_back("LINE\tT\t" + line);
        }
        out.push_back(std::string("LINE\tC\t") + JoinWith(section.headings, kSeparator));
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

bool BuildWebSkillsWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error) {
    std::vector<std::string> intro;
    std::vector<Section> sections;
    if (!Parse(page, intro, sections, error)) return false;
    if (!HasPage(SplitLines(current))) return Fail(error, "wiki-data.txt has no merc-oskill page");
    std::vector<std::string> built;
    BuildPage(intro, sections, built);
    out = Splice(current, built, kPageId, "");
    return true;
}

}  // namespace esrwiki
