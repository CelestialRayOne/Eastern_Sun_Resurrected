#include "cube_live.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <regex>
#include <set>
#include <utility>
#include <vector>

namespace esrwiki {
namespace {

// ---- Text helpers -------------------------------------------------------------------------
// The Python tools use str.strip() and \s, which treat the Unicode spaces as whitespace too.

const char kYDiaeresis[] = "\xC3\xBF";  // the colour-code lead character
const char kBlue[] = "\xC3\xBF" "c3";
const char kTableSeparator = '\x1f';
const char kLineBreak = '\x1e';  // inside a table cell: start a new line

std::size_t CharLength(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xF0) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC0) return 2;
    return 1;
}

// Byte length of the whitespace character at `i`, or 0.
std::size_t SpaceLength(const std::string& s, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x1F)) return 1;
    const auto at = [&](std::size_t k) { return i + k < s.size() ? static_cast<unsigned char>(s[i + k]) : 0; };
    if (c == 0xC2 && (at(1) == 0x85 || at(1) == 0xA0)) return 2;
    if (c == 0xE1 && at(1) == 0x9A && at(2) == 0x80) return 3;  // U+1680
    if (c == 0xE2 && at(1) == 0x80 && ((at(2) >= 0x80 && at(2) <= 0x8A) || at(2) == 0xA8 || at(2) == 0xA9 || at(2) == 0xAF)) return 3;
    if (c == 0xE2 && at(1) == 0x81 && at(2) == 0x9F) return 3;  // U+205F
    if (c == 0xE3 && at(1) == 0x80 && at(2) == 0x80) return 3;  // U+3000
    return 0;
}

std::string Strip(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size()) {
        const std::size_t n = SpaceLength(s, begin);
        if (n == 0) break;
        begin += n;
    }
    std::size_t end = begin;
    std::size_t i = begin;
    while (i < s.size()) {
        const std::size_t n = SpaceLength(s, i);
        if (n != 0) {
            i += n;
        } else {
            i = std::min(s.size(), i + CharLength(static_cast<unsigned char>(s[i])));
            end = i;
        }
    }
    return s.substr(begin, end - begin);
}

// re.sub(r"\s+", " ", s)
std::string CollapseSpaces(const std::string& s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const std::size_t n = SpaceLength(s, i);
        if (n != 0) {
            while (i < s.size()) {
                const std::size_t m = SpaceLength(s, i);
                if (m == 0) break;
                i += m;
            }
            out.push_back(' ');
        } else {
            const std::size_t len = std::min(s.size() - i, CharLength(static_cast<unsigned char>(s[i])));
            out.append(s, i, len);
            i += len;
        }
    }
    return out;
}

std::string StripChars(const std::string& s, const std::string& chars) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && chars.find(s[begin]) != std::string::npos) ++begin;
    while (end > begin && chars.find(s[end - 1]) != std::string::npos) --end;
    return s.substr(begin, end - begin);
}

std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Character references: the named ones the page can contain, and numeric ones.
std::string Unescape(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    static const std::map<std::string, std::uint32_t> kNamed = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
        {"copy", 0xA9}, {"reg", 0xAE}, {"deg", 0xB0}, {"plusmn", 0xB1}, {"times", 0xD7},
        {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022}, {"hellip", 0x2026}};
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '&') {
            const std::size_t semi = s.find(';', i + 1);
            if (semi != std::string::npos && semi - i <= 10 && semi > i + 1) {
                const std::string name = s.substr(i + 1, semi - i - 1);
                std::uint32_t cp = 0;
                bool ok = false;
                if (name[0] == '#') {
                    const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
                    const std::string digits = name.substr(hex ? 2 : 1);
                    if (!digits.empty() && digits.size() <= 7) {
                        ok = true;
                        for (const char d : digits) {
                            const int v = d >= '0' && d <= '9' ? d - '0'
                                          : hex && d >= 'a' && d <= 'f' ? d - 'a' + 10
                                          : hex && d >= 'A' && d <= 'F' ? d - 'A' + 10 : -1;
                            if (v < 0) { ok = false; break; }
                            cp = cp * (hex ? 16 : 10) + static_cast<std::uint32_t>(v);
                        }
                        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) ok = false;
                    }
                } else {
                    const auto found = kNamed.find(name);
                    if (found != kNamed.end()) { cp = found->second; ok = true; }
                }
                if (ok) {
                    AppendUtf8(out, cp);
                    i = semi + 1;
                    continue;
                }
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

bool StartsWith(const std::string& s, std::size_t at, const char* prefix) {
    const std::size_t n = std::char_traits<char>::length(prefix);
    return s.compare(at, n, prefix) == 0;
}

bool Replace(std::string& s, const std::string& from, const std::string& to) {
    bool changed = false;
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) {
        s.replace(at, from.size(), to);
        changed = true;
    }
    return changed;
}

// ---- HTML tables ----------------------------------------------------------------------------
// Mirrors tools/build_cube.py: only top-level tables are read. A cell is its text lines, split at
// <br>, with the page's font colours turned into game colour codes.

struct Cell {
    std::string bg;
    std::vector<std::string> lines;
};
using Row = std::vector<Cell>;
struct Table {
    std::string cat;
    std::vector<Row> rows;
};

const std::set<std::string>& SiteTabs() {
    static const std::set<std::string> tabs = {
        "special", "uber", "legendary", "mis", "tyrael", "gem", "rel", "mat", "nor", "mag", "uni",
        "set", "rin", "cha", "jew", "qui", "old", "for", "dst", "tin", "bas", "soc", "sec", "new"};
    return tabs;
}

std::string FontCode(const std::string& colour) {
    if (colour == "4850b8" || colour == "cyan") return "3";
    if (colour == "yellow") return "9";
    if (colour == "orange") return "8";
    if (colour == "red") return "1";
    return "";
}

class TableParser {
public:
    std::vector<Table> tables;

    void Feed(const std::string& html) {
        std::size_t i = 0;
        const std::size_t n = html.size();
        while (i < n) {
            const std::size_t lt = html.find('<', i);
            if (lt == std::string::npos) {
                OnData(html.substr(i));
                break;
            }
            if (lt > i) OnData(html.substr(i, lt - i));
            i = lt;
            if (StartsWith(html, i, "<!--")) {
                const std::size_t close = html.find("-->", i + 4);
                if (close == std::string::npos) break;
                i = close + 3;
            } else if (StartsWith(html, i, "<!") || StartsWith(html, i, "<?")) {
                const std::size_t close = html.find('>', i);
                if (close == std::string::npos) break;
                i = close + 1;
            } else if (StartsWith(html, i, "</") && i + 2 < n && IsAlpha(html[i + 2])) {
                std::size_t j = i + 2;
                while (j < n && !IsSpaceOrEnd(html[j])) ++j;
                const std::string name = Lower(html.substr(i + 2, j - i - 2));
                const std::size_t close = html.find('>', j);
                if (close == std::string::npos) break;
                OnEnd(name);
                i = close + 1;
            } else if (i + 1 < n && IsAlpha(html[i + 1])) {
                i = ParseStartTag(html, i);
            } else {
                OnData("<");
                ++i;
            }
        }
    }

private:
    static bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    static bool IsSpaceChar(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
    static bool IsSpaceOrEnd(char c) { return IsSpaceChar(c) || c == '>' || c == '/'; }

    std::size_t ParseStartTag(const std::string& html, std::size_t i) {
        const std::size_t n = html.size();
        std::size_t j = i + 1;
        while (j < n && !IsSpaceOrEnd(html[j])) ++j;
        const std::string name = Lower(html.substr(i + 1, j - i - 1));
        std::map<std::string, std::string> attrs;
        bool selfClosing = false;
        while (j < n) {
            while (j < n && (IsSpaceChar(html[j]) || html[j] == '/')) {
                if (html[j] == '/' && j + 1 < n && html[j + 1] == '>') selfClosing = true;
                ++j;
            }
            if (j >= n) break;
            if (html[j] == '>') { ++j; break; }
            std::size_t k = j;
            while (k < n && !IsSpaceChar(html[k]) && html[k] != '=' && html[k] != '>' && html[k] != '/') ++k;
            if (k == j) { ++j; continue; }  // stray character
            const std::string attr = Lower(html.substr(j, k - j));
            j = k;
            while (j < n && IsSpaceChar(html[j])) ++j;
            std::string value;
            if (j < n && html[j] == '=') {
                ++j;
                while (j < n && IsSpaceChar(html[j])) ++j;
                if (j < n && (html[j] == '"' || html[j] == '\'')) {
                    const char quote = html[j++];
                    const std::size_t close = html.find(quote, j);
                    const std::size_t stop = close == std::string::npos ? n : close;
                    value = html.substr(j, stop - j);
                    j = close == std::string::npos ? n : close + 1;
                } else {
                    std::size_t v = j;
                    while (v < n && !IsSpaceChar(html[v]) && html[v] != '>') ++v;
                    value = html.substr(j, v - j);
                    j = v;
                }
            }
            attrs[attr] = value;
        }
        OnStart(name, attrs);
        if (selfClosing) OnEnd(name);
        if ((name == "script" || name == "style") && !selfClosing) {
            // Script and style bodies are text, never markup: skip to the closing tag.
            std::size_t close = j;
            const std::string closing = "</" + name;
            for (; close < n; ++close) {
                if (html[close] == '<' && close + closing.size() <= n &&
                    Lower(html.substr(close, closing.size())) == closing) break;
            }
            j = close;
        }
        return j;
    }

    void OnStart(const std::string& tag, const std::map<std::string, std::string>& attrs) {
        const auto attr = [&](const char* key) {
            const auto found = attrs.find(key);
            return found == attrs.end() ? std::string() : found->second;
        };
        if (tag == "a") {
            const std::string name = attr("name");
            if (SiteTabs().count(name) != 0 && name != "special") cat_ = name;
        }
        if (tag == "table") {
            ++depth_;
            if (depth_ == 1) {
                table_ = Table{cat_, {}};
                inTable_ = true;
            }
        } else if (tag == "tr" && depth_ == 1 && inTable_) {
            row_.clear();
            inRow_ = true;
        } else if ((tag == "td" || tag == "th") && depth_ == 1 && inRow_) {
            cell_ = attr("bgcolor");
            cell_ = Lower(cell_);
            parts_.clear();
            inCell_ = true;
            fonts_.clear();
        } else if (tag == "font" && inCell_) {
            const std::string code = FontCode(Lower(attr("color")));
            fonts_.push_back(code);
            if (!code.empty()) parts_ += std::string(kYDiaeresis) + "c" + code;
        } else if (tag == "br" && inCell_) {
            parts_ += "\n";
        }
    }

    void OnEnd(const std::string& tag) {
        if (tag == "table") {
            if (depth_ == 1 && inTable_) {
                tables.push_back(std::move(table_));
                inTable_ = false;
            }
            --depth_;
        } else if (tag == "font" && inCell_ && !fonts_.empty()) {
            const std::string code = fonts_.back();
            fonts_.pop_back();
            if (!code.empty()) parts_ += std::string(kYDiaeresis) + "c0";
        } else if (tag == "tr" && depth_ == 1 && inRow_) {
            table_.rows.push_back(std::move(row_));
            row_.clear();
            inRow_ = false;
        } else if ((tag == "td" || tag == "th") && inCell_ && inRow_) {
            for (auto code = fonts_.rbegin(); code != fonts_.rend(); ++code) {
                if (!code->empty()) parts_ += std::string(kYDiaeresis) + "c0";
            }
            fonts_.clear();
            Cell finished;
            finished.bg = cell_;
            std::size_t start = 0;
            while (start <= parts_.size()) {
                std::size_t stop = parts_.find('\n', start);
                if (stop == std::string::npos) stop = parts_.size();
                std::string line = Strip(CollapseSpaces(parts_.substr(start, stop - start)));
                line = Strip(StripTrailingCodes(line));
                if (!line.empty() && line != std::string(kYDiaeresis) + "c0") finished.lines.push_back(line);
                start = stop + 1;
            }
            row_.push_back(std::move(finished));
            inCell_ = false;
        }
    }

    // re.sub(r"(ÿc\d)+$", "", line)
    static std::string StripTrailingCodes(std::string line) {
        const std::size_t codeLength = 2 + 1 + 1;  // ÿ is two bytes, then c and a digit
        while (line.size() >= codeLength) {
            const std::size_t at = line.size() - codeLength;
            if (line.compare(at, 3, std::string(kYDiaeresis) + "c") == 0 && line.back() >= '0' && line.back() <= '9') {
                line.erase(at);
            } else {
                break;
            }
        }
        return line;
    }

    void OnData(const std::string& text) {
        if (inCell_) parts_ += Unescape(text);
    }

    std::string cat_ = "special";
    int depth_ = 0;
    Table table_;
    bool inTable_ = false;
    Row row_;
    bool inRow_ = false;
    std::string cell_;  // background colour of the cell being read
    std::string parts_;
    bool inCell_ = false;
    std::vector<std::string> fonts_;
};

// ---- Recipe records -------------------------------------------------------------------------
// The same rules as tools/build_cube.py: site tabs merge into the overlay's pages, the class crafting
// sections are left out of Legacy Craft, and notes ahead of the first heading of General go under "Notes".

using Record = std::vector<std::string>;  // kind, page, fields...

std::string SitePage(const std::string& tab) {
    static const std::map<std::string, std::string> pages = {
        {"special", "general"}, {"uber", "general"}, {"legendary", "general"}, {"mis", "general"},
        {"tyrael", "general"}, {"gem", "mat"}, {"rel", "mat"}, {"mat", "mat"}, {"nor", "reroll"},
        {"mag", "reroll"}, {"uni", "reroll"}, {"set", "reroll"}, {"rin", "reroll"}, {"cha", "reroll"},
        {"jew", "reroll"}, {"qui", "reroll"}, {"old", "old"}, {"for", "for"}, {"dst", "dst"},
        {"tin", "tin"}, {"bas", "bas"}, {"soc", "soc"}, {"sec", "sec"}};
    const auto found = pages.find(tab);
    return found == pages.end() ? std::string() : found->second;
}

bool IsClassCraftHeading(const std::string& text) {
    static const std::set<std::string> headings = {
        "Class Craft", "Amazon Craft", "Assassin Craft", "Barbarian Craft", "Druid Craft",
        "Necromancer Craft", "Paladin Craft", "Sorceress Craft", "Warlock Craft"};
    return headings.count(text) != 0;
}

std::string Join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += separator;
        out += parts[i];
    }
    return out;
}

std::vector<Record> BuildRecords(const std::vector<Table>& tables) {
    std::vector<Record> records;
    std::set<std::string> hasSection;
    bool inClassCraft = false;
    const auto emit = [&](const std::string& kind, const std::string& page, std::vector<std::string> fields) {
        if (page == "old") {
            if (kind == "SEC") inClassCraft = IsClassCraftHeading(fields[0]);
            if (inClassCraft) return;
        }
        if (kind == "SEC") {
            hasSection.insert(page);
        } else if (page == "general" && hasSection.count(page) == 0) {
            records.push_back({"SEC", page, "Notes"});
            hasSection.insert(page);
        }
        Record record = {kind, page};
        for (auto& field : fields) {
            Replace(field, "See also Supercharged Tinkering.", "See also Supercharged Tinkering (Secret Recipe 22).");
            record.push_back(std::move(field));
        }
        records.push_back(std::move(record));
    };
    const auto cellText = [](const Cell& cell) { return Join(cell.lines, " | "); };
    const auto isInOut = [](const std::string& text) { return text == "Input" || text == "Output"; };
    const auto plainBg = [](const std::string& bg) { return bg == "#181818" || bg.empty(); };

    for (const Table& table : tables) {
        const std::string tab = table.cat.empty() ? "special" : table.cat;
        if (tab == "new") continue;  // the NewCraft tab is not shown
        const std::string page = SitePage(tab);
        if (page.empty()) continue;
        for (const Row& row : table.rows) {
            if (row.empty()) continue;
            if (row.size() == 1 && row[0].bg == "#300000") {
                const std::string text = Join(row[0].lines, " ");
                if (isInOut(text)) continue;
                // A short line is a heading unless it is a sentence; a long one is a description.
                const bool sentence = !text.empty() && text.back() == '.';
                const std::string kind = text.size() <= 60 ? (sentence ? "NOTE" : "SEC") : "DESC";
                emit(kind, page, {text});
            } else if (row.size() == 2 && row[0].bg == "#300000" && row[1].bg == "#300000") {
                if (isInOut(cellText(row[0])) && isInOut(cellText(row[1]))) continue;
                emit("SEC", page, {row[0].lines.empty() ? std::string() : Join(row[0].lines, " ")});
                if (!row[1].lines.empty()) emit("DESC", page, {Join(row[1].lines, " ")});
            } else if (row.size() == 2 && plainBg(row[0].bg) && plainBg(row[1].bg)) {
                const std::string a = cellText(row[0]);
                const std::string b = cellText(row[1]);
                if (a.empty() && b.empty()) continue;
                emit("REC", page, {a, b});
            } else if (row.size() == 1 && plainBg(row[0].bg)) {
                emit("NOTE", page, {Join(row[0].lines, " ")});
            }
        }
    }
    return records;
}

// ---- Wiki data ------------------------------------------------------------------------------
// The same output as tools/build_cube_wiki.py.

struct Item {
    char kind = 'T';  // 'T' note, 'R' recipe
    std::string a;    // note text, or the ingredients
    std::string b;    // the result
    std::string family;  // the sub-section of an Item Reroll group (its Type filter), or empty
};
struct EntryData {
    std::string title;
    std::vector<Item> items;
};
struct PageData {
    std::string id, title, group;
    std::vector<EntryData> entries;
};

std::string CleanText(std::string text) {
    for (char& c : text) {
        if (c == '\t' || c == '\r') c = ' ';
    }
    return Strip(text);
}

std::string Side(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t stop = text.find(" | ", start);
        if (stop == std::string::npos) stop = text.size();
        const std::string part = Strip(text.substr(start, stop - start));
        if (!part.empty()) parts.push_back(part);
        start = stop + 3;
    }
    return Join(parts, " + ");
}

// strip_color: "ÿc" plus any one character, or "ÿ" plus any one character.
std::string StripColor(const std::string& text) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, kYDiaeresis) == 0 && i + 3 < text.size() && text[i + 2] == 'c') {
            i += 3 + CharLength(static_cast<unsigned char>(text[i + 3]));
            continue;
        }
        if (text.compare(i, 2, kYDiaeresis) == 0 && i + 2 < text.size()) {
            i += 2 + CharLength(static_cast<unsigned char>(text[i + 2]));
            continue;
        }
        out.push_back(text[i++]);
    }
    return out;
}

std::string TidyRecipe(const std::string& raw) {
    std::string text = Strip(CollapseSpaces(StripColor(raw)));
    // Collapse "+ +" (with any spaces between) into one "+".
    for (bool again = true; again;) {
        again = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '+') continue;
            std::size_t j = i + 1;
            while (j < text.size()) {
                const std::size_t n = SpaceLength(text, j);
                if (n == 0) break;
                j += n;
            }
            if (j < text.size() && text[j] == '+') {
                text.erase(i + 1, j + 1 - (i + 1));
                again = true;
                break;
            }
        }
    }
    return StripChars(CollapseSpaces(text), " +");
}

// The first "  ->  " (whitespace, an arrow, whitespace): where it starts and where it ends.
bool FindArrow(const std::string& text, std::size_t& start, std::size_t& end) {
    std::ptrdiff_t runStart = -1;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t space = SpaceLength(text, i);
        if (space != 0) {
            if (runStart < 0) runStart = static_cast<std::ptrdiff_t>(i);
            i += space;
            continue;
        }
        if (runStart >= 0 && text.compare(i, 2, "->") == 0 && i + 2 < text.size() && SpaceLength(text, i + 2) != 0) {
            std::size_t k = i + 2;
            while (k < text.size()) {
                const std::size_t n = SpaceLength(text, k);
                if (n == 0) break;
                k += n;
            }
            start = static_cast<std::size_t>(runStart);
            end = k;
            return true;
        }
        runStart = -1;
        i += std::min(text.size() - i, CharLength(static_cast<unsigned char>(text[i])));
    }
    return false;
}

// Long D-Stone notes run several topics together; each one starts a new paragraph.
std::vector<std::string> SplitNoteTopics(const std::string& text) {
    static const char* const kTopics[] = {"D-Stoning Caps ", "D-Stoning Runeworded Items ", "Fail-Safe Features "};
    const auto isWord = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (static_cast<unsigned char>(c) >= 0x80); };
    std::vector<std::size_t> cuts = {0};
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (isWord(text[i - 1])) continue;  // needs a word boundary before the topic
        for (const char* topic : kTopics) {
            if (StartsWith(text, i, topic)) { cuts.push_back(i); break; }
        }
    }
    cuts.push_back(text.size());
    std::vector<std::string> parts;
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
        const std::string part = Strip(text.substr(cuts[k], cuts[k + 1] - cuts[k]));
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

// Forging sub-headings and D-Stone topics each start their own section.
std::vector<EntryData> SplitSections(const std::vector<EntryData>& entries) {
    static const std::regex kStat(R"(^(Skill|All Skill|Stat|Rune) Forging Target Item includes:)");
    static const std::regex kAura(R"(^Aura Forging\b)");
    static const std::regex kMerc(R"(^Merc Only Conversion\b)");
    struct Section { std::string title; std::vector<std::string> notes; std::vector<std::pair<std::string, std::string>> rows; };
    std::vector<Section> sections;
    for (const EntryData& entry : entries) {
        sections.push_back({entry.title, {}, {}});
        std::vector<std::string> texts;
        for (const Item& item : entry.items) {
            texts.push_back(item.kind == 'T' ? item.a : item.a + "  ->  " + item.b);
        }
        for (const std::string& line : texts) {
            for (const std::string& text : SplitNoteTopics(line)) {
                std::smatch match;
                std::string heading;
                if (std::regex_search(text, match, kStat)) heading = match[1].str() + " Forging";
                else if (std::regex_search(text, match, kAura)) heading = "Aura Forging";
                else if (std::regex_search(text, match, kMerc)) heading = "Merc Only Conversion";
                if (!heading.empty()) {
                    sections.push_back({heading, {}, {}});
                    sections.back().notes.push_back(text);
                    continue;
                }
                std::size_t arrowStart = 0, arrowEnd = 0;
                if (FindArrow(text, arrowStart, arrowEnd)) {
                    sections.back().rows.emplace_back(text.substr(0, arrowStart), text.substr(arrowEnd));
                } else {
                    sections.back().notes.push_back(text);
                }
            }
        }
    }
    std::vector<EntryData> result;
    for (const Section& section : sections) {
        if (section.notes.empty() && section.rows.empty()) continue;
        EntryData entry;
        entry.title = section.title;
        for (const std::string& note : section.notes) entry.items.push_back({'T', note, "", ""});
        for (const auto& row : section.rows) entry.items.push_back({'R', row.first, row.second, ""});
        result.push_back(std::move(entry));
    }
    return result;
}

// The Item Reroll groups (see SUB_GROUPS in tools/build_cube_wiki.py): the sub-sections of a group heading, which
// stay in the group's one entry and are told apart by their family. Empty for any other heading.
const std::vector<std::string>& GroupSubsections(const std::string& page, const std::string& heading) {
    static const std::map<std::string, std::vector<std::string>> groups = {
        {"Magic/Rare Items", {"Standard Reroll", "Starter's Weapon", "CtC Skill Armor"}},
        {"Rings/Amulets", {"Standard Reroll", "Custom Reroll", "Skill Tab Reroll"}}};
    static const std::vector<std::string> none;
    if (page != "reroll") return none;
    const auto found = groups.find(heading);
    return found == groups.end() ? none : found->second;
}

std::string WikiText(const std::vector<Record>& records) {
    std::vector<PageData> pages = {
        {"general", "General", "Items", {}}, {"mat", "Material", "Items", {}}, {"reroll", "Item Reroll", "Items", {}},
        {"old", "Legacy Craft", "Items", {}}, {"for", "Forging", "Modify", {}}, {"dst", "D-Stone", "Modify", {}},
        {"tin", "Tinker", "Modify", {}}, {"bas", "Base Upgrade", "Modify", {}}, {"soc", "Sockets", "Modify", {}},
        {"sec", "Secret", "Modify", {}}};
    std::map<std::string, std::size_t> pageIndex;
    for (std::size_t i = 0; i < pages.size(); ++i) pageIndex[pages[i].id] = i;
    std::map<std::string, long> current;  // page id -> entry being filled
    std::map<std::string, std::string> group;   // page id -> the open Item Reroll group heading
    std::map<std::string, std::string> family;  // page id -> the sub-section being filled
    const auto currentEntry = [&](PageData& page) -> EntryData& {
        long& at = current[page.id];
        if (at == 0) {
            page.entries.push_back({page.title, {}});
            at = static_cast<long>(page.entries.size());
        }
        return page.entries[static_cast<std::size_t>(at - 1)];
    };
    for (const Record& record : records) {
        const auto found = pageIndex.find(record[1]);
        if (found == pageIndex.end()) continue;
        PageData& page = pages[found->second];
        if (record[0] == "SEC") {
            const std::string title = CleanText(record[2]);
            const std::vector<std::string>& subs = GroupSubsections(page.id, group[page.id]);
            if (std::find(subs.begin(), subs.end(), title) != subs.end()) {
                family[page.id] = title;  // a sub-section of the open group: its recipes stay in the group's entry
                continue;
            }
            group[page.id] = GroupSubsections(page.id, title).empty() ? std::string() : title;
            family[page.id].clear();
            page.entries.push_back({title, {}});
            current[page.id] = static_cast<long>(page.entries.size());
        } else if (record[0] == "DESC" || record[0] == "NOTE") {
            currentEntry(page).items.push_back({'T', CleanText(record[2]), "", family[page.id]});
        } else if (record[0] == "REC") {
            currentEntry(page).items.push_back({'R', CleanText(Side(record[2])), CleanText(Side(record[3])), family[page.id]});
        }
    }
    for (PageData& page : pages) {
        if (page.id == "for" || page.id == "dst") page.entries = SplitSections(page.entries);
    }

    std::string out = "#ESRWIKI\t2\n";
    for (const PageData& page : pages) {
        out += "PAGE\t" + page.id + "\t" + page.title + "\t" + page.group + "\n";
        for (const EntryData& entry : page.entries) {
            out += "ENTRY\t" + entry.title + "\t\n";
            // A block is its notes followed by its recipe table, so a note after a table starts a new block. So does a
            // change of sub-section: a block carries its sub-section's name as its family (the Type filter).
            struct Block {
                std::string family;
                std::vector<std::string> notes;
                std::vector<std::pair<std::string, std::string>> recipes;
            };
            std::vector<Block> blocks;
            Block open;
            for (const Item& item : entry.items) {
                if (item.family != open.family) {
                    if (!open.notes.empty() || !open.recipes.empty()) blocks.push_back(open);
                    open = Block{item.family, {}, {}};
                }
                if (item.kind == 'T') {
                    if (!open.recipes.empty()) {
                        blocks.push_back(open);
                        open = Block{item.family, {}, {}};
                    }
                    open.notes.push_back(item.a);
                } else {
                    open.recipes.emplace_back(item.a, item.b);
                }
            }
            if (!open.notes.empty() || !open.recipes.empty()) blocks.push_back(open);
            for (const Block& block : blocks) {
                // A sub-section of an Item Reroll group also gives its family (the Type filter).
                out += block.family.empty() ? std::string("BLOCK\t0\t\t\t\n")
                                            : "BLOCK\t0\t\t\t\t\t" + block.family + "\t\n";
                for (const std::string& text : block.notes) out += "LINE\tT\t" + text + "\n";
                if (block.recipes.empty()) continue;
                out += std::string("LINE\tC\tIngredients") + kTableSeparator + "Result\n";
                for (const auto& recipe : block.recipes) {
                    const auto split = [](const std::string& text) {
                        std::vector<std::string> parts;
                        std::size_t start = 0;
                        while (start <= text.size()) {
                            std::size_t stop = text.find(" + ", start);
                            if (stop == std::string::npos) stop = text.size();
                            const std::string part = Strip(text.substr(start, stop - start));
                            if (!part.empty()) parts.push_back(part);
                            start = stop + 3;
                        }
                        return parts;
                    };
                    const std::vector<std::string> ins = split(TidyRecipe(recipe.first));
                    const std::vector<std::string> outs = split(TidyRecipe(recipe.second));
                    out += "LINE\tR\t" + Join(ins, std::string(1, kLineBreak)) + kTableSeparator + kBlue +
                           Join(outs, std::string(1, kLineBreak)) + "\n";
                }
            }
        }
    }
    return out;
}

}  // namespace

bool BuildCubeWikiData(const std::string& html, std::string& out, std::string& error) {
    // A download cut short would give a panel with half its recipes, so the page must be complete.
    std::string tail = Lower(html.size() > 4096 ? html.substr(html.size() - 4096) : html);
    if (tail.find("</html") == std::string::npos) {
        error = "the page is incomplete (no closing html tag)";
        return false;
    }
    TableParser parser;
    parser.Feed(html);
    const std::vector<Record> records = BuildRecords(parser.tables);
    std::size_t recipes = 0;
    std::set<std::string> pages;
    for (const Record& record : records) {
        if (record[0] == "REC") ++recipes;
        pages.insert(record[1]);
    }
    // The real page has about 600 recipes on ten pages; far fewer means the layout has changed.
    if (recipes < 300 || pages.size() < 8) {
        error = "the page has " + std::to_string(recipes) + " recipes on " + std::to_string(pages.size()) +
                " pages, which does not look like the cube recipes";
        return false;
    }
    out = WikiText(records);
    return true;
}

bool ParseClassCraftSections(const std::string& html, std::vector<ClassCraftSection>& out, std::string& error) {
    out.clear();
    TableParser parser;
    parser.Feed(html);
    const auto isInOut = [](const std::string& text) { return text == "Input" || text == "Output"; };
    const auto plainBg = [](const std::string& bg) { return bg == "#181818" || bg.empty(); };
    for (const Table& table : parser.tables) {
        const std::string tab = table.cat.empty() ? "special" : table.cat;
        if (tab != "new") continue;  // the class craft sections are on the NewCraft tab
        bool inClass = false;
        for (const Row& row : table.rows) {
            if (row.empty()) continue;
            // Headings are dark rows: one cell, or two. The Input and Output header row is skipped.
            std::string heading;
            bool isHeading = false;
            if (row.size() == 1 && row[0].bg == "#300000") {
                const std::string text = Join(row[0].lines, " ");
                if (!isInOut(text)) {
                    heading = text;
                    isHeading = true;
                }
            } else if (row.size() == 2 && row[0].bg == "#300000" && row[1].bg == "#300000") {
                if (!(isInOut(Join(row[0].lines, " ")) && isInOut(Join(row[1].lines, " ")))) {
                    heading = Join(row[0].lines, " ");
                    isHeading = true;
                }
            }
            if (isHeading) {
                inClass = IsClassCraftHeading(heading) && heading != "Class Craft";  // the intro is not a class
                if (inClass) out.push_back(ClassCraftSection{heading, {}});
                continue;
            }
            if (!inClass || row.size() != 2 || !plainBg(row[0].bg) || !plainBg(row[1].bg)) continue;
            if (row[0].lines.empty() && row[1].lines.empty()) continue;
            out.back().recipes.push_back(ClassCraftRecipe{row[0].lines, row[1].lines});
        }
    }
    if (out.empty()) {
        error = "the cube recipes page has no class craft sections";
        return false;
    }
    return true;
}

}  // namespace esrwiki
