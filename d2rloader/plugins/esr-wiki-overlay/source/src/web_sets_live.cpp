#include "web_sets_live.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

const char kPageId[] = "set-items";
const char kPageTitle[] = "Set Items";
const char kPageGroup[] = "Wiki";
const char* const kOrder[] = {"General", "Amazon", "Barbarian", "Necromancer", "Paladin",
                              "Sorceress", "Assassin", "Druid", "Warlock", "Hireling"};
const std::size_t kMinPieces = 200;
const std::size_t kMinSets = 60;
const char kTitleColor[] = "\xc3\xbf" "cC";   // set and piece titles, and the item-count bonus lines
const char kStatColor[] = "\xc3\xbf" "c3";    // stat lines
const char kHeadingColor[] = "\xc3\xbf" "c4"; // "Partial Set Bonus", "Full Set Bonus", "(2 items)"
const char kReqColor[] = "\xc3\xbf" "c5";     // "Required Level: n"
const std::size_t kNone = std::string::npos;

struct Bonus {
    int items = 0;
    std::vector<std::string> lines;
};

struct Piece {
    std::string name;
    std::string base;
    int lvl = 0;
    int req = 0;
    std::vector<std::string> stats;
    std::vector<Bonus> bonus;
};

struct SetRecord {
    std::string header;
    std::vector<Piece> pieces;
    std::vector<std::string> partial;
    int partialCount = 2;
    std::vector<std::string> full;
};

using Sections = std::map<std::string, std::vector<SetRecord>>;
using PictureKey = std::pair<std::string, std::string>;
using Pictures = std::map<PictureKey, std::vector<std::string>>;

struct Cell {
    std::string attrs;
    std::string content;
};

struct Mark {
    std::string name;
    std::size_t start = 0;
};

bool Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

bool IsClass(const std::string& name) {
    for (const char* known : kOrder) {
        if (name == known) return true;
    }
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

// The text of an HTML fragment: tags dropped (no space added), entities decoded, spaces collapsed.
std::string Text(const std::string& fragment) {
    return Clean(StripTags(fragment));
}

// The non-empty text lines of a fragment, split at <br>.
std::vector<std::string> LinesOf(const std::string& fragment) {
    std::vector<std::string> out;
    for (const std::string& part : SplitBr(fragment)) {
        const std::string line = Text(part);
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

// re.search(r"<open>(.*?)</close>"), with the literal open and close strings.
bool Span(const std::string& html, const std::string& open, const std::string& close, std::string& inner) {
    const std::size_t start = html.find(open);
    if (start == kNone) return false;
    const std::size_t begin = start + open.size();
    const std::size_t end = html.find(close, begin);
    if (end == kNone) return false;
    inner = html.substr(begin, end - begin);
    return true;
}

// re.search(r'<a href="[^"]*">(.*?)</a>')
bool BaseSpan(const std::string& left, std::string& inner) {
    const std::string prefix = "<a href=\"";
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = left.find(prefix, at);
        if (open == kNone) return false;
        const std::size_t quote = left.find('"', open + prefix.size());
        if (quote != kNone && quote + 1 < left.size() && left[quote + 1] == '>') {
            const std::size_t begin = quote + 2;
            const std::size_t end = left.find("</a>", begin);
            if (end == kNone) return false;
            inner = left.substr(begin, end - begin);
            return true;
        }
        at = open + 1;
    }
}

// re.sub(r"\s*\([^)]*\)$", "", text)
std::string StripCode(const std::string& text) {
    if (text.empty() || text.back() != ')') return text;
    const std::size_t open = text.rfind('(');
    if (open == kNone) return text;
    if (text.find(')', open) != text.size() - 1) return text;
    std::size_t cut = open;
    while (cut > 0 && text[cut - 1] == ' ') --cut;
    return text.substr(0, cut);
}

// re.search(r"Lvl\s+(\d+)\s*/\s*Req\s+Lvl\s+(\d+)", text)
bool LevelOf(const std::string& text, int& lvl, int& req) {
    static const std::regex pattern(R"(Lvl\s+(\d+)\s*/\s*Req\s+Lvl\s+(\d+))");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return false;
    lvl = std::stoi(match[1]);
    req = std::stoi(match[2]);
    return true;
}

// re.match(r"^\((\d+) items?\)$", line)
bool MarkerOf(const std::string& line, int& items) {
    static const std::regex pattern(R"(\((\d+) items?\))");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) return false;
    items = std::stoi(match[1]);
    return true;
}

// re.match(r"^(.*)-(N|NM|H) \(merc only\)$", name)
bool DiffSuffix(const std::string& name, std::string& base, std::string& code) {
    static const std::regex pattern(R"((.*)-(N|NM|H) \(merc only\))");
    std::smatch match;
    if (!std::regex_match(name, match, pattern)) return false;
    base = match[1];
    code = match[2];
    return true;
}

// Key of a card: its title without case, spacing or punctuation, and its difficulty.
PictureKey KeyOf(const std::string& title, const std::string& subtitle) {
    std::string key;
    for (char c : title) {
        const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) key += lower;
    }
    return {key, subtitle};
}

// re.finditer(r'<td([^>]*)>(.*?)</td>', re.S | re.I)
std::vector<Cell> CellsOf(const std::string& html) {
    std::vector<Cell> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = FindCI(html, "<td", at);
        if (open == kNone) break;
        const std::size_t gt = html.find('>', open + 3);
        if (gt == kNone) break;
        const std::size_t close = FindCI(html, "</td>", gt + 1);
        if (close == kNone) break;
        out.push_back({html.substr(open + 3, gt - open - 3), html.substr(gt + 1, close - gt - 1)});
        at = close + 5;
    }
    return out;
}

// re.finditer(r'<a name="([A-Za-z]+)"></a>')
std::vector<Mark> SectionMarks(const std::string& html) {
    std::vector<Mark> out;
    const std::string prefix = "<a name=\"";
    std::size_t at = 0;
    while ((at = html.find(prefix, at)) != kNone) {
        const std::size_t nameStart = at + prefix.size();
        std::size_t k = nameStart;
        while (k < html.size() && ((html[k] >= 'A' && html[k] <= 'Z') || (html[k] >= 'a' && html[k] <= 'z'))) ++k;
        if (k > nameStart && html.compare(k, 6, "\"></a>") == 0) {
            out.push_back({html.substr(nameStart, k - nameStart), at});
        }
        at += 1;
    }
    return out;
}

bool ParsePiece(const std::string& left, const std::string& right, Piece& piece, std::string& error) {
    std::string name, base, stats;
    const bool hasName = Span(left, "<font color=\"#48B850\">", "</font>", name);
    const bool hasBase = BaseSpan(left, base);
    int lvl = 0, req = 0;
    const bool hasLevel = LevelOf(Text(left), lvl, req);
    if (!(hasName && hasLevel)) {
        return Fail(error, "a set piece could not be read: " + Text(left).substr(0, 60));
    }
    piece.name = Text(name);
    if (hasBase) {
        piece.base = StripCode(Text(base));
    } else {
        // the base is written on the line under the name, as "Ring (rin)"
        const std::size_t cut = left.find("<font color=\"8080E6\">");
        const std::vector<std::string> before = LinesOf(cut == kNone ? left : left.substr(0, cut));
        const auto at = std::find(before.begin(), before.end(), piece.name);
        const std::ptrdiff_t next = (at == before.end() ? -1 : static_cast<std::ptrdiff_t>(at - before.begin())) + 1;
        static const std::regex codeLine(R"((.*)\s\([^)]*\))");
        std::smatch match;
        if (next < 0 || next >= static_cast<std::ptrdiff_t>(before.size()) ||
            !std::regex_match(before[static_cast<std::size_t>(next)], match, codeLine)) {
            return Fail(error, "a set piece has no base: " + piece.name);
        }
        piece.base = match[1];
    }
    piece.lvl = lvl;
    piece.req = req;
    if (Span(left, "<font color=\"8080E6\">", "</font>", stats)) piece.stats = LinesOf(stats);
    std::vector<std::string> pending;
    for (const std::string& part : SplitBr(right)) {
        const std::string line = Text(part);
        if (line.empty()) continue;
        int items = 0;
        if (MarkerOf(line, items)) {
            piece.bonus.push_back({items, pending});
            pending.clear();
        } else {
            pending.push_back(line);
        }
    }
    return true;
}

bool ParsePartial(const std::string& cell, std::vector<std::string>& stats, int& count, std::string& error) {
    const std::vector<std::string> lines = LinesOf(cell);
    const auto start = std::find(lines.begin(), lines.end(), "Partial Set Bonus");
    if (start == lines.end()) return Fail(error, "the partial set bonus is missing");
    stats.clear();
    count = 2;
    for (auto line = start + 1; line != lines.end(); ++line) {
        int items = 0;
        if (MarkerOf(*line, items)) {
            count = items;
            break;
        }
        stats.push_back(*line);
    }
    return true;
}

bool ParseFull(const std::string& cell, std::vector<std::string>& full, std::string& error) {
    const std::vector<std::string> lines = LinesOf(cell);
    const auto start = std::find(lines.begin(), lines.end(), "Full Set Bonus");
    if (start == lines.end()) return Fail(error, "the full set bonus is missing");
    full.assign(start + 1, lines.end());
    return true;
}

bool ParseSections(const std::string& html, Sections& result, std::string& error) {
    const std::vector<Mark> marks = SectionMarks(html);
    if (marks.empty()) return Fail(error, "the sets page has no sections");
    for (std::size_t n = 0; n < marks.size(); ++n) {
        const std::string& name = marks[n].name;
        if (!IsClass(name)) continue;
        const std::size_t end = n + 1 < marks.size() ? marks[n + 1].start : html.size();
        const std::vector<Cell> cells = CellsOf(html.substr(marks[n].start, end - marks[n].start));
        std::vector<SetRecord> records;
        std::size_t current = kNone;
        for (std::size_t i = 0; i < cells.size();) {
            const Cell& cell = cells[i];
            if (cell.attrs.find("colspan") != kNone && cell.attrs.find("#402040") != kNone) {
                const std::string header = Text(cell.content);
                ++i;
                if (header == name) {
                    current = kNone;
                    continue;
                }
                SetRecord record;
                record.header = header;
                records.push_back(record);
                current = records.size() - 1;
                continue;
            }
            if (current == kNone) {
                ++i;
                continue;
            }
            if (Text(cell.content).find("Lvl") != kNone && cell.content.find("48B850") != kNone && i + 1 < cells.size()) {
                Piece piece;
                if (!ParsePiece(cell.content, cells[i + 1].content, piece, error)) return false;
                // old pieces of a set (names ending in _old) are left out
                const bool old = piece.name.size() > 4 && piece.name.compare(piece.name.size() - 4, 4, "_old") == 0;
                if (!old) records[current].pieces.push_back(piece);
                i += 2;
                continue;
            }
            if (cell.content.find("Partial Set Bonus") != kNone && i + 1 < cells.size()) {
                if (!ParsePartial(cell.content, records[current].partial, records[current].partialCount, error)) return false;
                if (!ParseFull(cells[i + 1].content, records[current].full, error)) return false;
                i += 2;
                continue;
            }
            ++i;
        }
        result[name] = records;
    }
    for (const char* name : kOrder) {
        if (result.find(name) == result.end()) return Fail(error, std::string("the sets page has no ") + name + " section");
    }
    return true;
}

// The lines of the Set Items page of the wiki data (empty when there is none).
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

// (title, subtitle) key -> the picture keys the wiki data already gives that card.
// The picture keys of the first card with each title that has any (a card keeps the picture it had).
Pictures ExistingPictures(const std::vector<std::string>& lines) {
    std::vector<std::pair<PictureKey, std::vector<std::string>>> cards;
    for (const std::string& line : lines) {
        const std::vector<std::string> parts = SplitTabs(line);
        if (parts.empty()) continue;
        if (parts[0] == "BLOCK" && parts.size() > 2) {
            cards.push_back({KeyOf(parts[2], parts.size() > 3 ? parts[3] : ""), {}});
        } else if (parts[0] == "IMG" && !cards.empty() && parts.size() > 1) {
            // keeps the size flag: "IMG\tkey\ttiny" stays whole
            std::string picture = parts[1];
            for (std::size_t k = 2; k < parts.size(); ++k) picture += "\t" + parts[k];
            cards.back().second.push_back(picture);
        }
    }
    Pictures pictures;
    for (const auto& card : cards) {
        if (!card.second.empty()) pictures.insert({card.first, card.second});  // insert keeps the first
    }
    return pictures;
}

bool SetBlocks(const std::string& section, const SetRecord& record, const Pictures& pictures,
               std::vector<std::string>& blocks, std::size_t& count, std::string& error) {
    blocks.clear();
    count = 0;
    const bool hireling = section == "Hireling";
    const std::vector<Piece>& pieces = record.pieces;
    if (pieces.empty()) return true;
    std::string diff;
    int lvl = pieces[0].lvl;
    if (hireling) {
        std::string code;
        for (const Piece& piece : pieces) {
            std::string base, next;
            if (!DiffSuffix(piece.name, base, next)) return Fail(error, "set " + record.header + " has pieces from more than one difficulty");
            if (code.empty()) code = next;
            if (next != code) return Fail(error, "set " + record.header + " has pieces from more than one difficulty");
        }
        diff = code == "N" ? "Normal" : code == "NM" ? "Nightmare" : "Hell";
    }
    const std::string display = record.header + (hireling ? " (" + diff + ", Lvl " + std::to_string(lvl) + ")" : "");
    for (const Piece& piece : pieces) {
        std::string title = piece.name;
        if (hireling) {
            std::string base, code;
            if (DiffSuffix(title, base, code)) title = base;
        }
        ++count;
        const auto found = pictures.find(KeyOf(title, diff));
        blocks.push_back("BLOCK\t1\t" + title + "\t" + diff + "\t" + display + "\t" + diff + "\t\t" + std::to_string(piece.req));
        blocks.push_back("FIELD\tSet\t" + record.header);
        blocks.push_back("FIELD\tBase\t" + piece.base);
        blocks.push_back("FIELD\tRequired Level\t" + std::to_string(piece.req));
        if (hireling) blocks.push_back("FIELD\tItem Level\t" + std::to_string(piece.lvl));
        blocks.push_back("FIELD\tSet pieces\t" + std::to_string(pieces.size()));
        if (found != pictures.end()) {
            for (const std::string& key : found->second) blocks.push_back("IMG\t" + key);
        }
        const std::string head = kTitleColor + title + (hireling ? " " + diff : "");
        blocks.push_back("LINE\tS\t" + head);
        for (const std::string& stat : piece.stats) blocks.push_back("LINE\tS\t" + std::string(kStatColor) + stat);
        for (const Bonus& bonus : piece.bonus) {
            blocks.push_back("LINE\tS\t" + std::string(kTitleColor) + "(" + std::to_string(bonus.items) + " Items)");
            for (const std::string& line : bonus.lines) blocks.push_back("LINE\tS\t" + std::string(kTitleColor) + line);
        }
        blocks.push_back("LINE\tS\t" + std::string(kReqColor) + "Required Level: " + std::to_string(piece.req));
    }
    // the set card: its partial and full bonuses
    const auto found = pictures.find(KeyOf(record.header, diff));
    blocks.push_back("BLOCK\t1\t" + record.header + "\t" + diff + "\t" + display + "\t" + diff + "\t\t0");
    if (found != pictures.end()) {
        for (const std::string& key : found->second) blocks.push_back("IMG\t" + key);
    }
    blocks.push_back("LINE\tH\tSet bonus");
    blocks.push_back("LINE\tS\t" + std::string(kTitleColor) + record.header);
    blocks.push_back("LINE\tS\t" + std::string(kHeadingColor) + "Partial Set Bonus");
    for (const std::string& stat : record.partial) blocks.push_back("LINE\tS\t" + std::string(kStatColor) + stat);
    blocks.push_back("LINE\tS\t" + std::string(kHeadingColor) + "(" + std::to_string(record.partialCount) + " items)");
    blocks.push_back("LINE\tS\t" + std::string(kHeadingColor) + "Full Set Bonus");
    for (const std::string& stat : record.full) blocks.push_back("LINE\tS\t" + std::string(kStatColor) + stat);
    return true;
}

bool BuildPage(const Sections& sections, const Pictures& pictures, std::vector<std::string>& out, std::string& error) {
    out.clear();
    out.push_back(std::string("PAGE\t") + kPageId + "\t" + kPageTitle + "\t" + kPageGroup);
    std::size_t totalPieces = 0;
    std::size_t totalSets = 0;
    for (const char* section : kOrder) {
        std::vector<std::string> entryLines;
        std::size_t pieces = 0;
        for (const SetRecord& record : sections.at(section)) {
            std::vector<std::string> blocks;
            std::size_t count = 0;
            if (!SetBlocks(section, record, pictures, blocks, count, error)) return false;
            entryLines.insert(entryLines.end(), blocks.begin(), blocks.end());
            pieces += count;
            if (!blocks.empty()) ++totalSets;
        }
        totalPieces += pieces;
        std::size_t cards = 0;
        for (const std::string& line : entryLines) {
            if (line.compare(0, 6, "BLOCK\t") == 0) ++cards;
        }
        const std::string name = std::string(section) == "General" ? "Generic" : section;
        out.push_back("ENTRY\t" + name + "\t" + std::to_string(cards) + " items");
        out.insert(out.end(), entryLines.begin(), entryLines.end());
    }
    if (totalPieces < kMinPieces || totalSets < kMinSets) {
        return Fail(error, "only " + std::to_string(totalPieces) + " pieces in " + std::to_string(totalSets) +
                               " sets; the website page looks broken");
    }
    return true;
}

}  // namespace

bool BuildWebSetsWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error) {
    if (FindCI(page, "</html") == kNone) return Fail(error, "the saved sets page is incomplete");
    Sections sections;
    if (!ParseSections(page, sections, error)) return false;
    const std::vector<std::string> lines = SplitLines(current);
    const std::vector<std::string> existing = PageLines(lines);
    if (existing.empty()) return Fail(error, "wiki-data.txt has no set-items page");
    std::vector<std::string> built;
    if (!BuildPage(sections, ExistingPictures(existing), built, error)) return false;
    out = Splice(current, built, kPageId, "");
    return true;
}

}  // namespace esrwiki
