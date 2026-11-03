#include "web_bases_live.hpp"

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

constexpr std::size_t kMinArmor = 200;
constexpr std::size_t kMinWeapons = 300;
constexpr std::size_t kMaxTitle = 40;
const char kHeaderColor[] = "#402040";

struct Item {
    std::string name, code;
    std::vector<std::vector<std::string>> rows;
};

struct Section {
    std::string title;
    std::vector<std::string> headers;
    std::vector<Item> items;
};

struct Cell {
    std::string attrs, content;
};

// re.findall(r"<td([^>]*)>(.*?)</td>", tr, re.S | re.I)
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

// The number in `name="N"` among the attributes, or `fallback`.
int Span(const std::string& attrs, const char* name, int fallback) {
    const std::string wanted = std::string(name) + "=\"";
    const std::size_t at = attrs.find(wanted);
    if (at == std::string::npos) return fallback;
    std::size_t k = at + wanted.size();
    const std::size_t digits = k;
    while (k < attrs.size() && attrs[k] >= '0' && attrs[k] <= '9') ++k;
    if (k == digits || k >= attrs.size() || attrs[k] != '"') return fallback;
    return std::stoi(attrs.substr(digits, k - digits));
}

std::vector<Section> ParseSections(const std::string& html, bool weapons) {
    std::vector<Section> sections;
    Section* current = nullptr;
    std::vector<int> pending;
    std::vector<std::string> held;
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
        if (cells.size() == 1 && cells[0].attrs.find(kHeaderColor) != std::string::npos && TextOf(cells[0].content).size() <= kMaxTitle) {
            sections.push_back(Section{TextOf(cells[0].content), {}, {}});
            current = &sections.back();
            continue;
        }
        if (current == nullptr || cells.size() == 1) continue;
        if (cells[0].attrs.find(kHeaderColor) != std::string::npos) {
            current->headers.clear();
            for (const Cell& cell : cells) {
                const int count = Span(cell.attrs, "colspan", 1);
                const std::string label = TextOf(cell.content);
                if (label == "Damage" && count == 3) {
                    for (const char* name : {"Hands", "Damage", "Average"}) current->headers.push_back(name);
                } else {
                    for (int i = 0; i < count; ++i) current->headers.push_back(label);
                }
            }
            pending.assign(current->headers.size(), 0);
            held.assign(current->headers.size(), std::string());
            continue;
        }
        if (current->headers.empty()) continue;
        std::vector<std::string> row;
        std::size_t used = 0;
        for (std::size_t i = 0; i < current->headers.size(); ++i) {
            if (pending[i] > 0) {
                row.push_back(i == 0 ? std::string() : held[i]);
                --pending[i];
            } else if (used < cells.size()) {
                const Cell& cell = cells[used++];
                const int rows = Span(cell.attrs, "rowspan", 1);
                if (rows > 1) { pending[i] = rows - 1; held[i] = cell.content; }
                row.push_back(cell.content);
            } else {
                row.push_back(std::string());
            }
        }
        if (row[0].empty() && !current->items.empty()) {
            std::vector<std::string> text;
            for (const std::string& cell : row) text.push_back(TextOf(cell));
            current->items.back().rows.push_back(std::move(text));
            continue;
        }
        Item item;
        std::vector<std::string> first;
        for (const std::string& cell : row) first.push_back(TextOf(cell));
        if (weapons) {
            const std::vector<std::string> pieces = SplitBr(row[0]);
            item.name = TextOf(pieces[0]);
            item.code = pieces.size() > 1 ? TextOf(pieces[1]) : std::string();
            first[0] = Trim(item.name + " " + item.code);
        } else {
            item.name = TextOf(row[0]);
            item.code = row.size() > 1 ? TextOf(row[1]) : std::string();
        }
        item.rows.push_back(std::move(first));
        current->items.push_back(std::move(item));
    }
    std::vector<Section> kept;
    for (Section& section : sections)
        if (!section.headers.empty() && !section.items.empty()) kept.push_back(std::move(section));
    return kept;
}

struct Art {
    struct Base { std::string tier, family, image; };
    std::map<std::string, Base> codes;
    std::map<std::string, std::string> sections;
};

Art ReadArt(const std::string& text) {
    Art art;
    for (const std::string& line : SplitLines(text)) {
        const std::vector<std::string> parts = SplitTabs(line);
        if (parts[0] == "BASE" && parts.size() >= 5) art.codes[parts[1]] = {parts[2], parts[3], parts[4]};
        else if (parts[0] == "SECTION" && parts.size() >= 3) art.sections[parts[1]] = parts[2];
    }
    return art;
}

// re.sub(r"_lod\b", "", clean(value))
std::string OutText(const std::string& value) {
    const std::string cleaned = Clean(value);
    std::string out;
    for (std::size_t i = 0; i < cleaned.size();) {
        if (cleaned.compare(i, 4, "_lod") == 0) {
            const std::size_t after = i + 4;
            const bool word = after < cleaned.size() &&
                              ((cleaned[after] >= 'a' && cleaned[after] <= 'z') || (cleaned[after] >= 'A' && cleaned[after] <= 'Z') ||
                               (cleaned[after] >= '0' && cleaned[after] <= '9') || cleaned[after] == '_');
            if (!word) { i = after; continue; }
        }
        out += cleaned[i++];
    }
    return out;
}

std::string AlternateLabel(const std::string& hands) {
    if (hands == "Mis") return "Missile";
    if (hands == "2H") return "Two-Handed";
    return hands.empty() ? std::string("Alternate") : hands;
}

bool LooksLikeNumber(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size() && text[i] == '-') ++i;
    if (i == text.size()) return false;
    for (std::size_t k = i; k < text.size(); ++k) if (text[k] < '0' || text[k] > '9') return false;
    return true;
}

std::vector<std::string> BuildPage(const std::vector<Section>& armor, const std::vector<Section>& weapons, const Art& art) {
    std::vector<std::string> out;
    out.push_back("PAGE\titem-bases\tItem Bases\tWiki");
    const std::vector<Section>* kinds[2] = {&armor, &weapons};
    const char* titles[2] = {"Armor", "Weapons"};
    for (int kind = 0; kind < 2; ++kind) {
        std::vector<std::vector<std::string>> blocks;
        for (const Section& section : *kinds[kind]) {
            const std::vector<std::string>& headers = section.headers;
            std::ptrdiff_t qlvlAt = -1;
            for (std::size_t i = 0; i < headers.size(); ++i) if (headers[i] == "Qlvl") { qlvlAt = static_cast<std::ptrdiff_t>(i); break; }
            std::set<std::pair<std::string, std::vector<std::vector<std::string>>>> seen;
            for (const Item& item : section.items) {
                Art::Base base;
                const auto known = art.codes.find(item.code);
                if (known != art.codes.end()) base = known->second;
                std::vector<std::vector<std::string>> cellsKey;
                for (const auto& row : item.rows) {
                    std::vector<std::string> kept;
                    for (std::size_t i = 0; i < headers.size() && i < row.size(); ++i)
                        if (headers[i] != "Name" && headers[i] != "Code") kept.push_back(row[i]);
                    cellsKey.push_back(std::move(kept));
                }
                if (!seen.insert({item.name + '\x01' + base.tier, cellsKey}).second) continue;
                const std::vector<std::string>& first = item.rows[0];
                std::vector<std::pair<std::string, std::string>> fields;
                for (std::size_t i = 0; i < headers.size() && i < first.size(); ++i)
                    if (headers[i] != "Name" && headers[i] != "Code" && !Trim(first[i]).empty()) fields.emplace_back(headers[i], first[i]);
                for (std::size_t r = 1; r < item.rows.size(); ++r) {
                    const auto& row = item.rows[r];
                    if (row.size() > 3 && !Trim(row[2]).empty()) fields.emplace_back(AlternateLabel(row[1]), row[2] + " (" + row[3] + ")");
                }
                int level = 0;
                if (qlvlAt >= 0 && static_cast<std::size_t>(qlvlAt) < first.size() && LooksLikeNumber(first[qlvlAt])) level = std::stoi(first[qlvlAt]);
                std::string image = base.image;
                if (image.empty()) {
                    const auto fallback = art.sections.find(section.title);
                    if (fallback != art.sections.end()) image = fallback->second;
                }
                // Mythical bases get their own tier so the Tier filter can show them apart from Normal.
                const std::string tier = (base.tier == "Normal" && item.name.compare(0, 9, "Mythical ") == 0) ? "Mythic" : base.tier;
                std::string subtitle = tier;
                if (!item.code.empty()) subtitle += (subtitle.empty() ? "" : " ") + item.code;
                std::vector<std::string> block;
                block.push_back("BLOCK\t1\t" + OutText(item.name) + "\t" + OutText(subtitle) + "\t" + OutText(section.title) + "\t" +
                                OutText(tier) + "\t" + OutText(section.title) + "\t" + std::to_string(level));
                for (const auto& field : fields) block.push_back("FIELD\t" + OutText(field.first) + "\t" + OutText(field.second));
                if (!image.empty()) block.push_back("IMG\t" + image);
                blocks.push_back(std::move(block));
            }
        }
        out.push_back(std::string("ENTRY\t") + titles[kind] + "\t" + std::to_string(blocks.size()) + " items\tsortable\tfiveacross");
        for (const auto& block : blocks) out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

std::size_t CountItems(const std::vector<Section>& sections) {
    std::size_t count = 0;
    for (const Section& section : sections) count += section.items.size();
    return count;
}

}  // namespace

bool BuildWebBasesWikiData(const std::string& current, const std::string& armorsPage, const std::string& weaponsPage,
                           const std::string& artTable, std::string& out, std::string& error) {
    for (const std::string* page : {&armorsPage, &weaponsPage}) {
        if (FindCI(*page, "</html") == std::string::npos) {
            error = std::string(page == &armorsPage ? kWebBasesPageNames[0] : kWebBasesPageNames[1]) + " is incomplete";
            return false;
        }
    }
    const std::vector<Section> armor = ParseSections(armorsPage, false);
    const std::vector<Section> weapons = ParseSections(weaponsPage, true);
    if (CountItems(armor) < kMinArmor || CountItems(weapons) < kMinWeapons) {
        error = "only " + std::to_string(CountItems(armor)) + " armors and " + std::to_string(CountItems(weapons)) +
                " weapons found; the website pages look broken";
        return false;
    }
    out = Splice(current, BuildPage(armor, weapons, ReadArt(artTable)), "item-bases", "gemwords");
    return true;
}

}  // namespace esrwiki
