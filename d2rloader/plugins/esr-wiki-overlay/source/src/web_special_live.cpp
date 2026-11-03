#include "web_special_live.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

const char kPageId[] = "corruption";
const char kPageTitle[] = "Special Craft";
const char kPageGroup[] = "Endgame";
const char kBlockLine[] = "BLOCK\t0\t\t\t\t\t\t0";
const char kOutcomeColor[] = "\xc3\xbf" "c3";  // the colour marker (U+00FF then 'c') and colour 3
const char kSameItem[] = "Same Item";
const std::size_t kNone = std::string::npos;

struct ExpectedSection {
    const char* title;
    std::size_t minimum;  // outcome rows the section must have at least
};
const ExpectedSection kSections[] = {
    {"Corruption Outcomes", 100},
    {"Anointment Outcomes", 10},
    {"Sanctification Outcomes", 50},
    {"Standard of Heroes Outcomes", 6},
};

struct Group {
    std::string name;
    std::vector<std::string> lines;
};

struct Section {
    std::string title;
    std::vector<std::string> intro;
    std::vector<Group> groups;  // in the order their first input appears
    std::size_t rows = 0;
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

// re.match(r"^(?:[A-Za-z]+/)+[A-Za-z]+ ", ...): letters separated by at least one slash.
bool IsSlashList(const std::string& text) {
    std::size_t slashes = 0;
    bool afterLetter = false;
    for (char c : text) {
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (letter) {
            afterLetter = true;
        } else if (c == '/' && afterLetter) {
            ++slashes;
            afterLetter = false;
        } else {
            return false;
        }
    }
    return slashes > 0 && afterLetter;
}

// "Unique/Set/Crafted/Rare/Magic/Normal Weapon" is Weapon, "Unique Charm" is Charm, "Hellfire Torch" and "Any Jewel"
// are themselves.
std::string GroupName(const std::string& first) {
    const std::size_t space = first.find(' ');
    if (space != kNone && space + 1 < first.size() && IsSlashList(first.substr(0, space))) {
        return first.substr(space + 1);
    }
    if (StartsWith(first, "Unique ")) return first.substr(std::string("Unique ").size());
    return first;
}

bool IsSection(const std::string& text) {
    for (const ExpectedSection& section : kSections) {
        if (text == section.title) return true;
    }
    return false;
}

std::string JoinWith(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += separator;
        out += parts[i];
    }
    return out;
}

// The "Same Item: ..." line of one outcome cell, or false when it has no outcome.
bool OutcomeLine(const std::string& cell, std::string& line) {
    std::vector<std::string> parts = LinesOf(cell);
    if (!parts.empty() && parts[0] == kSameItem) parts.erase(parts.begin());
    if (parts.empty()) return false;
    line = std::string("LINE\tR\t") + kOutcomeColor + kSameItem + ": " + JoinWith(parts, "; ");
    return true;
}

void AddOutcome(Section& section, const std::string& name, const std::string& line) {
    for (Group& group : section.groups) {
        if (group.name == name) {
            group.lines.push_back(line);
            return;
        }
    }
    section.groups.push_back({name, {line}});
}

bool Parse(const std::string& html, std::vector<Section>& sections, std::string& error) {
    if (FindCI(html, "</html") == kNone) return Fail(error, "the saved page is incomplete");
    std::vector<std::string> intro;
    for (const std::string& table : ContentsOf(html, "table")) {
        const std::vector<std::string> rows = ContentsOf(table, "tr");
        if (rows.empty()) continue;
        const std::vector<std::string> cells = ContentsOf(rows[0], "td");
        if (rows.size() == 1 && cells.size() == 1) {
            const std::vector<std::string> lines = LinesOf(cells[0]);
            const std::string text = JoinWith(lines, " ");
            if (IsSection(text)) {
                sections.push_back({text, intro, {}, 0});
                intro.clear();
            } else {
                intro = lines;
            }
        } else if (!sections.empty() && JoinWith(LinesOf(rows[0]), " ").find("Input(s)") != kNone) {
            Section& section = sections.back();
            for (std::size_t r = 1; r < rows.size(); ++r) {
                const std::vector<std::string> columns = ContentsOf(rows[r], "td");
                if (columns.size() != 2) return Fail(error, "an outcome row does not have an input and an outcome");
                const std::vector<std::string> inputs = LinesOf(columns[0]);
                if (inputs.empty()) return Fail(error, "an outcome row has no input");
                std::string line;
                if (!OutcomeLine(columns[1], line)) return Fail(error, "an outcome row has no outcome");
                AddOutcome(section, GroupName(inputs[0]), line);
                ++section.rows;
            }
        }
    }
    if (sections.size() != sizeof(kSections) / sizeof(kSections[0])) {
        return Fail(error, "the four outcome sections are missing");
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].title != kSections[i].title) return Fail(error, "the four outcome sections are missing");
        if (sections[i].intro.empty()) return Fail(error, sections[i].title + " has no intro");
        if (sections[i].rows < kSections[i].minimum) {
            return Fail(error, sections[i].title + " has too few outcomes");
        }
    }
    return true;
}

void BuildPage(const std::vector<Section>& sections, std::vector<std::string>& out) {
    out.clear();
    out.push_back(std::string("PAGE\t") + kPageId + "\t" + kPageTitle + "\t" + kPageGroup);
    for (const Section& section : sections) {
        out.push_back("ENTRY\t" + section.title + "\t");
        out.push_back(kBlockLine);
        for (const std::string& line : section.intro) out.push_back("LINE\tT\t" + line);
        const bool many = section.groups.size() > 1;
        for (const Group& group : section.groups) {
            out.push_back("BLOCK\t0\t\t\t" + group.name + "\t\t" + (many ? group.name : std::string()) + "\t0");
            out.push_back("LINE\tC\tPossible Outcome(s)");
            out.insert(out.end(), group.lines.begin(), group.lines.end());
        }
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

bool BuildWebSpecialWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error) {
    std::vector<Section> sections;
    if (!Parse(page, sections, error)) return false;
    if (!HasPage(SplitLines(current))) return Fail(error, "wiki-data.txt has no special craft page");
    std::vector<std::string> built;
    BuildPage(sections, built);
    out = Splice(current, built, kPageId, "");
    return true;
}

}  // namespace esrwiki
