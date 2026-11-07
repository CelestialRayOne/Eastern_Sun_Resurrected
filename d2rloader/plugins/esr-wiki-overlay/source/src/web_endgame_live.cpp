#include "web_endgame_live.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

const char kPageId[] = "endgame-maps";
const char kPageTitle[] = "Endgame Maps";
const char kPageGroup[] = "Endgame";
const char kOverview[] = "Overview";
const std::size_t kBosses = 18;
const std::size_t kMinOverview = 5;
const char kBlockLine[] = "BLOCK\t0\t\t\t\t\t\t0";
const char kColor[] = "\xc3\xbf" "c";  // the colour-code marker (U+00FF then 'c')
const std::size_t kNone = std::string::npos;

struct Run {
    std::string code;  // the overlay colour letter, "" for the default colour
    bool italic = false;
    std::string text;
};
using Line = std::vector<Run>;

struct Boss {
    std::string title;
    std::vector<std::string> lines;
};

// The website's colour names and hex values, as the overlay's colour letters.
std::string ColorCode(const std::string& name) {
    static const std::map<std::string, std::string> codes = {
        {"red", "R"}, {"green", "G"}, {"purple", "P"}, {"orange", "O"}, {"#8b0000", "D"}, {"#4850b8", "B"},
        {"#0000ff", "L"}, {"#706850", "H"}, {"#808080", "Y"}, {"white", "W"}, {"#f1e5ac", "K"}};
    const auto found = codes.find(name);
    return found == codes.end() ? std::string() : found->second;
}

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

std::string TextOf(const std::string& fragment) {
    return Clean(StripTags(fragment));
}

// The runs of a cell, one list per line (split at <br>): colour, italic and text, as the website writes them.
std::vector<Line> RunsOf(const std::string& fragment) {
    std::vector<Line> lines;
    Line current;
    std::vector<std::string> stack;  // the colour name of each open <font>, "" when it has none
    int italic = 0;
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
            } else if (token == "<i>") {
                ++italic;
            } else if (token == "</i>") {
                if (italic > 0) --italic;
            }
            i = end + 1;
            continue;
        }
        const std::size_t end = fragment.find('<', i);
        const std::string piece = fragment.substr(i, (end == kNone ? fragment.size() : end) - i);
        std::string colour;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (!it->empty()) {
                colour = ColorCode(*it);
                break;
            }
        }
        current.push_back({colour, italic > 0, Unescape(piece)});
        i = end == kNone ? fragment.size() : end;
    }
    lines.push_back(current);
    return lines;
}

std::string JoinRuns(const Line& runs, std::size_t from, std::size_t to) {
    std::string raw;
    for (std::size_t i = from; i < to; ++i) raw += runs[i].text;
    return Clean(raw);
}

// The LINE lines of one boss's description and rewards cell.
std::vector<std::string> BossLines(const std::string& cell) {
    std::vector<std::string> out;
    bool rewards = false;
    bool abilities = false;
    for (const Line& runs : RunsOf(cell)) {
        std::vector<std::size_t> visible;  // runs with text
        for (std::size_t i = 0; i < runs.size(); ++i) {
            if (!Clean(runs[i].text).empty()) visible.push_back(i);
        }
        const std::string text = JoinRuns(runs, 0, runs.size());
        if (visible.empty() || text.empty()) continue;
        std::set<std::string> codes;
        bool allItalic = true;
        for (std::size_t i : visible) {
            codes.insert(runs[i].code);
            if (!runs[i].italic) allItalic = false;
        }
        if (codes == std::set<std::string>{"H"}) {
            rewards = StartsWith(text, "Rewards");
            out.push_back("LINE\tH\t" + std::string(kColor) + "H" + text);
        } else if (codes == std::set<std::string>{""} && allItalic) {
            out.push_back("LINE\tT\t" + text);
        } else if (!runs[visible[0]].code.empty() && runs[visible[0]].code != "H") {
            const std::string first = runs[visible[0]].code;
            const std::size_t start = visible[0];
            std::size_t i = start;
            while (i < runs.size() && runs[i].code == first) ++i;
            const std::string name = JoinRuns(runs, start, i);
            std::string restRaw;
            for (std::size_t j = i; j < runs.size(); ++j) restRaw += runs[j].text;
            const std::string rest = Clean(restRaw);
            if (!rewards && !abilities) {
                out.push_back("LINE\tH\t" + std::string(kColor) + "HAbilities");
                abilities = true;
            }
            if (!rest.empty()) {
                const std::string lead = (!restRaw.empty() && SpaceLength(restRaw, 0) > 0) ? " " : "";
                out.push_back("LINE\tT\t" + std::string(kColor) + first + name + kColor + "N" + lead + rest);
            } else {
                out.push_back("LINE\tT\t" + std::string(kColor) + first + name);
            }
        } else {
            out.push_back((rewards ? "LINE\tT\t" + std::string(kColor) + "N" : std::string("LINE\tT\t")) + text);
        }
    }
    return out;
}

// (attributes, content) of each <td>.
std::vector<std::pair<std::string, std::string>> CellsOf(const std::string& html) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = FindCI(html, "<td", at);
        if (open == kNone) break;
        const std::size_t gt = html.find('>', open + 3);
        if (gt == kNone) break;
        const std::size_t close = FindCI(html, "</td>", gt + 1);
        if (close == kNone) break;
        out.emplace_back(html.substr(open + 3, gt - open - 3), html.substr(gt + 1, close - gt - 1));
        at = close + 5;
    }
    return out;
}

// (content) of each <tr>.
std::vector<std::string> RowsOf(const std::string& html) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = FindCI(html, "<tr", at);
        if (open == kNone) break;
        const std::size_t gt = html.find('>', open + 3);
        if (gt == kNone) break;
        const std::size_t close = FindCI(html, "</tr>", gt + 1);
        if (close == kNone) break;
        out.push_back(html.substr(gt + 1, close - gt - 1));
        at = close + 5;
    }
    return out;
}

bool Parse(const std::string& html, std::vector<std::string>& overview, std::vector<Boss>& bosses, std::string& error) {
    if (FindCI(html, "</html") == kNone) return Fail(error, "the saved page is incomplete");
    for (const std::string& row : RowsOf(html)) {
        const auto cells = CellsOf(row);
        if (cells.size() == 1 && cells[0].second.find("Warning:") != kNone) {
            overview.clear();
            for (const Line& line : RunsOf(cells[0].second)) {
                const std::string text = JoinRuns(line, 0, line.size());
                if (!text.empty()) overview.push_back(text);
            }
        } else if (cells.size() == 2 && cells[0].second.find("map-bosses/") != kNone) {
            const std::string title = TextOf(cells[0].second);
            if (title.empty()) return Fail(error, "a map boss has no name");
            bosses.push_back({title, BossLines(cells[1].second)});
        }
    }
    if (overview.size() < kMinOverview) return Fail(error, "the overview is missing");
    if (bosses.size() != kBosses) {
        return Fail(error, "expected " + std::to_string(kBosses) + " map bosses, found " + std::to_string(bosses.size()));
    }
    return true;
}

// Title key: lower case letters and digits only, as the sets page compares them.
std::string KeyOf(const std::string& title) {
    std::string key;
    for (char c : title) {
        const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) key += lower;
    }
    return key;
}

std::vector<std::string> PageLines(const std::vector<std::string>& lines) {
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].compare(0, 5, "PAGE\t") == 0) starts.push_back(i);
    }
    for (std::size_t n = 0; n < starts.size(); ++n) {
        const std::vector<std::string> parts = SplitTabs(lines[starts[n]]);
        if (parts.size() > 1 && parts[1] == kPageId) {
            const std::size_t end = n + 1 < starts.size() ? starts[n + 1] : lines.size();
            return std::vector<std::string>(lines.begin() + static_cast<std::ptrdiff_t>(starts[n]),
                                            lines.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }
    return {};
}

// The IMG lines (without the word IMG) the existing page gives each entry, by the key of its title (the first with any).
std::map<std::string, std::vector<std::string>> ExistingPictures(const std::vector<std::string>& lines) {
    std::vector<std::pair<std::string, std::vector<std::string>>> entries;
    for (const std::string& line : lines) {
        const std::vector<std::string> parts = SplitTabs(line);
        if (parts.empty()) continue;
        if (parts[0] == "ENTRY" && parts.size() > 1) {
            entries.push_back({KeyOf(parts[1]), {}});
        } else if (parts[0] == "IMG" && !entries.empty() && parts.size() > 1) {
            std::string rest;
            for (std::size_t i = 1; i < parts.size(); ++i) rest += (i > 1 ? "\t" : "") + parts[i];
            entries.back().second.push_back(rest);
        }
    }
    std::map<std::string, std::vector<std::string>> pictures;
    for (const auto& entry : entries) {
        if (!entry.second.empty()) pictures.insert({entry.first, entry.second});  // insert keeps the first
    }
    return pictures;
}

void BuildPage(const std::vector<std::string>& overview, const std::vector<Boss>& bosses,
               const std::map<std::string, std::vector<std::string>>& pictures, std::vector<std::string>& out) {
    out.clear();
    out.push_back(std::string("PAGE\t") + kPageId + "\t" + kPageTitle + "\t" + kPageGroup);
    out.push_back(std::string("ENTRY\t") + kOverview + "\t");
    out.push_back(kBlockLine);
    for (const std::string& text : overview) out.push_back("LINE\tT\t" + text);
    for (const Boss& boss : bosses) {
        out.push_back("ENTRY\t" + boss.title + "\t");
        out.push_back(kBlockLine);
        const auto found = pictures.find(KeyOf(boss.title));
        if (found != pictures.end()) {
            for (const std::string& image : found->second) out.push_back("IMG\t" + image);
        }
        out.insert(out.end(), boss.lines.begin(), boss.lines.end());
    }
}

}  // namespace

bool BuildWebEndgameWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error) {
    std::vector<std::string> overview;
    std::vector<Boss> bosses;
    if (!Parse(page, overview, bosses, error)) return false;
    const std::vector<std::string> lines = SplitLines(current);
    const std::vector<std::string> existing = PageLines(lines);
    if (existing.empty()) return Fail(error, "wiki-data.txt has no endgame-maps page");
    std::vector<std::string> built;
    BuildPage(overview, bosses, ExistingPictures(existing), built);
    out = Splice(current, built, kPageId, "");
    return true;
}

}  // namespace esrwiki
