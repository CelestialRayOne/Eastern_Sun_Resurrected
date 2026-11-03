#include "web_affixes_live.hpp"

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

constexpr std::size_t kMinRows[3] = {3000, 6000, 600};
constexpr std::size_t kMaxTitle = 60;
const char kHeaderColor[] = "#402040";
const char kSeparator[] = "\x1f";
const char kBreak[] = "\x1e";
const char kBlue[] = "\xc3\xbf" "c3";  // U+00FF "c3"
const char kGold[] = "\xc3\xbf" "c4";  // U+00FF "c4"
const char* const kLabels[3] = {"Prefixes", "Suffixes", "Superior"};

struct Table {
    std::string title;
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;  // cell html
};

struct Cell {
    std::string attrs, content;
};

std::vector<Cell> CellsOf(const std::string& tr) {
    std::vector<Cell> cells;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = FindCI(tr, "<td", at);
        if (open == std::string::npos) break;
        const std::size_t gt = tr.find('>', open);
        if (gt == std::string::npos) break;
        const std::size_t close = FindCI(tr, "</td>", gt + 1);
        if (close == std::string::npos) break;
        cells.push_back({tr.substr(open + 3, gt - open - 3), tr.substr(gt + 1, close - gt - 1)});
        at = close + 5;
    }
    return cells;
}

bool IsHeaderCell(const Cell& cell) { return cell.attrs.find(kHeaderColor) != std::string::npos; }

void ParseTables(const std::string& html, std::vector<Table>& tables, std::vector<std::string>& keys) {
    std::vector<Table> all;
    Table* current = nullptr;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = FindCI(html, "<tr", at);
        if (open == std::string::npos) break;
        const std::size_t gt = html.find('>', open);
        if (gt == std::string::npos) break;
        const std::size_t close = FindCI(html, "</tr>", gt + 1);
        if (close == std::string::npos) break;
        at = close + 5;
        const std::vector<Cell> cells = CellsOf(html.substr(gt + 1, close - gt - 1));
        if (cells.empty()) continue;
        if (cells.size() == 1 && IsHeaderCell(cells[0]) && TextOf(cells[0].content).size() <= kMaxTitle) {
            all.push_back(Table{TextOf(cells[0].content), {}, {}});
            current = &all.back();
            continue;
        }
        if (current == nullptr) continue;
        if (IsHeaderCell(cells[0])) {
            current->headers.clear();
            for (const Cell& cell : cells) current->headers.push_back(TextOf(cell.content));
            continue;
        }
        if (current->title == "Keys") {
            for (const Cell& cell : cells) {
                const std::string text = TextOf(cell.content);
                if (text.find('=') != std::string::npos) keys.push_back(text);
            }
            continue;
        }
        if (!current->headers.empty() && cells.size() >= 2) {
            std::vector<std::string> row;
            for (const Cell& cell : cells) row.push_back(cell.content);
            current->rows.push_back(std::move(row));
        }
    }
    for (Table& table : all)
        if (table.title != "Keys" && !table.headers.empty() && !table.rows.empty()) tables.push_back(std::move(table));
}

std::string RowText(const std::vector<std::string>& headers, const std::vector<std::string>& row) {
    std::string out;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) out += kSeparator;
        const std::string cell = i < row.size() ? row[i] : std::string();
        if (headers[i] == "Name" || headers[i] == "Stats") {
            bool first = true;
            for (const std::string& piece : SplitBr(cell)) {
                const std::string text = TextOf(piece);
                if (Trim(text).empty()) continue;
                if (!first) out += kBreak;
                first = false;
                out += kBlue;
                out += text;
            }
        } else {
            out += TextOf(cell);
        }
    }
    return out;
}

// The rows of a table as text, in alphabetical order of their names (equal names keep the website order).
std::vector<std::string> SortedRows(const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::string> texts;
    texts.reserve(rows.size());
    for (const auto& row : rows) texts.push_back(RowText(headers, row));
    const auto nameColumn = std::find(headers.begin(), headers.end(), "Name");
    if (nameColumn == headers.end()) return texts;
    const std::size_t column = static_cast<std::size_t>(nameColumn - headers.begin());
    std::vector<std::string> keys;
    keys.reserve(texts.size());
    for (const std::string& text : texts) {
        std::size_t begin = 0;
        for (std::size_t i = 0; i < column; ++i) {
            const std::size_t at = text.find(kSeparator, begin);
            if (at == std::string::npos) { begin = text.size(); break; }
            begin = at + std::string(kSeparator).size();
        }
        std::size_t end = text.find(kSeparator, begin);
        if (end == std::string::npos) end = text.size();
        std::string cell = text.substr(begin, end - begin), key;
        for (std::size_t i = 0; i < cell.size();) {
            if (cell.compare(i, std::string(kBlue).size(), kBlue) == 0) { i += std::string(kBlue).size(); continue; }
            if (cell.compare(i, std::string(kBreak).size(), kBreak) == 0) { key += ' '; i += std::string(kBreak).size(); continue; }
            const char c = cell[i++];
            key += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        keys.push_back(std::move(key));
    }
    std::vector<std::size_t> order(texts.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return keys[a] < keys[b]; });
    std::vector<std::string> sorted;
    sorted.reserve(texts.size());
    for (const std::size_t i : order) sorted.push_back(std::move(texts[i]));
    return sorted;
}

bool TypeBefore(const std::string& a, const std::string& b) {
    const bool ag = a == "General", bg = b == "General";
    if (ag != bg) return ag;
    const std::string la = Lower(a), lb = Lower(b);
    if (la != lb) return la < lb;
    return a < b;
}

const char* const kArmor[] = {"Any Shield", "Auric Shields", "Belt", "Body Armor", "Boots", "Circlet", "Cloak", "Gloves", "Grimoire", "Helm", "Pelt", "Primal Helm", "Robe", "Shield", "Spirit Crown", "Voodoo Heads"};
const char* const kWeapons[] = {"Amazon Bow", "Amazon Javelin", "Amazon Spear", "Assassin 1H Katana", "Assassin 2H Katana", "Axe", "Barbarian Javs", "Blunt", "Bow", "Claw", "Club", "Crossbow", "Druid Club", "Hammer", "Hand to Hand", "Hand to Hand 1", "Hand to Hand 2", "Javelin", "Knife", "Knuckle", "Mace", "Necromancer Dagger", "Necromancer Polearm", "Orb", "Paladin Sword", "Polearm", "Scepter", "Shuriken", "Sorceress Mana Blade", "Spear", "Staff", "Sword", "Throwing Axe", "Throwing Knife", "Voidblade", "Wand"};
const char* const kKindWord[3] = {"Prefix", "Suffix", "Superior"};

bool IsExcluded(const std::string& title) {
    return title == "General" || title == "Bow Quiver / Crossbow Quiver" || title == "voib";  // on the website, not on this page
}

const char* const kAfter[2][2] = {{"Amulet", "Ring"}, {"Jewel", "Small Charm"}};  // {type, the type it follows}

int CategoryOf(const std::string& title) {
    for (const char* name : kArmor) if (title == name) return 0;
    for (const char* name : kWeapons) if (title == name) return 1;
    return 2;
}

std::vector<std::string> BuildPage(const std::vector<Table> (&pages)[3]) {
    std::map<std::pair<std::string, int>, const Table*> found;  // (item type, kind) -> the first table of that title
    for (int kind = 0; kind < 3; ++kind)
        for (const Table& table : pages[kind])
            if (!IsExcluded(table.title)) found.emplace(std::make_pair(table.title, kind), &table);

    const char* const categories[3] = {"Armor", "Weapons", "Other"};
    std::vector<std::string> out;
    out.push_back("PAGE\titem-affixes\tItem Affixes\tWiki");
    for (int category = 0; category < 3; ++category) {
        std::vector<std::string> types;
        std::size_t count = 0;
        for (const auto& entry : found) {
            if (CategoryOf(entry.first.first) != category) continue;
            count += entry.second->rows.size();
            if (types.empty() || types.back() != entry.first.first) types.push_back(entry.first.first);  // map order groups equal titles
        }
        std::sort(types.begin(), types.end(), TypeBefore);
        for (const auto& follow : kAfter) {
            const auto moved = std::find(types.begin(), types.end(), follow[0]);
            if (moved == types.end() || std::find(types.begin(), types.end(), follow[1]) == types.end()) continue;
            types.erase(moved);
            types.insert(std::find(types.begin(), types.end(), follow[1]) + 1, follow[0]);
        }
        out.push_back(std::string("ENTRY\t") + categories[category] + "\t" + std::to_string(count) + " affixes, " +
                      std::to_string(types.size()) + " item types\twide\tkeeporder\trowsort");
        for (const std::string& title : types) {
            for (int kind = 0; kind < 3; ++kind) {
                const auto it = found.find({title, kind});
                if (it == found.end()) continue;
                const Table& table = *it->second;
                const std::string name = title + " " + kLabels[kind];
                out.push_back("BLOCK\t0\t" + name + "\t\t\t" + kKindWord[kind] + "\t" + title + "\t0");
                out.push_back(std::string("LINE\tH\t") + kGold + name + " (" + std::to_string(table.rows.size()) + ")");
                out.push_back("LINE\tC\t" + Join(table.headers, kSeparator));
                for (const std::string& text : SortedRows(table.headers, table.rows)) out.push_back("LINE\tR\t" + text);
            }
        }
    }
    return out;
}

std::size_t CountRows(const std::vector<Table>& tables) {
    std::size_t count = 0;
    for (const Table& table : tables) count += table.rows.size();
    return count;
}

}  // namespace

bool BuildWebAffixesWikiData(const std::string& current, const std::string& prefixesPage, const std::string& suffixesPage,
                             const std::string& qualityPage, std::string& out, std::string& error) {
    const std::string* html[3] = {&prefixesPage, &suffixesPage, &qualityPage};
    std::vector<Table> pages[3];
    std::vector<std::string> keys;
    for (int kind = 0; kind < 3; ++kind) {
        if (FindCI(*html[kind], "</html") == std::string::npos) {
            error = std::string(kWebAffixesPageNames[kind]) + " is incomplete";
            return false;
        }
        ParseTables(*html[kind], pages[kind], keys);
        if (CountRows(pages[kind]) < kMinRows[kind]) {
            error = "only " + std::to_string(CountRows(pages[kind])) + " " + Lower(kLabels[kind]) +
                    " found; the website page looks broken";
            return false;
        }
    }
    out = Splice(current, BuildPage(pages), "item-affixes", "item-bases");
    return true;
}

}  // namespace esrwiki
