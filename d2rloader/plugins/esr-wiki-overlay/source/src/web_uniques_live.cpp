#include "web_uniques_live.hpp"

#include "web_html.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace esrwiki {
namespace {

const char kColorLead[] = "\xC3\xBF" "c";  // the colour-code lead character, then 'c'

using namespace web;

struct Record {
    std::string page, heading, group, title, base, code, gamble, ilvl, level, note;
    bool coupon = false;
    std::vector<Line> lines;
};

const char* HeadingGroup(const std::string& page, const std::string& heading) {
    if (page == "unique_mythicals.htm") return "Mythical";
    static const std::map<std::string, const char*> table = {
        {"Large Charm", "Charm"}, {"Grand Charm", "Charm"}, {"Odd Charm", "Charm"}, {"Small Charm", "Charm"},
        {"Jewel", "Jewel"}, {"Bow Quiver", "Quiver"}, {"Crossbow Quiver", "Quiver"},
        {"Helm", "Helm"}, {"Pelt", "Helm"}, {"Primal Helm", "Helm"}, {"Circlet", "Helm"}, {"Spirit Crown", "Helm"},
        {"Body Armor", "Torso"}, {"Cloak", "Torso"}, {"Robe", "Torso"},
        {"Shield", "Shield"}, {"Auric Shields", "Shield"}, {"Voodoo Heads", "Shield"}, {"Grimoire", "Shield"},
        {"Gloves", "Gloves"}, {"Boots", "Boots"}, {"Belt", "Belt"}};
    const auto found = table.find(heading);
    if (found != table.end()) return found->second;
    if (EndsWith(heading, "Ring")) return "Ring";
    if (EndsWith(heading, "Amulet")) return "Amulet";
    if (page == "unique_weapons.htm") return "Weapon";
    return "";
}

bool IsCode(const std::string& s) {
    if (s.size() != 3) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    return true;
}

// "Mancatcher (7br)" -> base "Mancatcher", code "7br"; no code leaves the text as it is.
std::string StripCode(const std::string& text, std::string* code = nullptr) {
    if (text.size() >= 5 && text.back() == ')' && text[text.size() - 5] == '(' && IsCode(text.substr(text.size() - 4, 3))) {
        if (code != nullptr) *code = text.substr(text.size() - 4, 3);
        return Trim(text.substr(0, text.size() - 5));
    }
    return text;
}

std::string LevelValue(const std::string& text, const std::string& label) {
    std::string found;
    std::size_t pos = 0;
    for (;;) {
        const std::size_t at = text.find(label + ":", pos);
        if (at == std::string::npos) break;
        std::size_t k = at + label.size() + 1;
        while (k < text.size() && SpaceLength(text, k) != 0) k += SpaceLength(text, k);
        const std::size_t digits = k;
        while (k < text.size() && text[k] >= '0' && text[k] <= '9') ++k;
        if (k > digits) found = text.substr(digits, k - digits);
        pos = at + label.size() + 1;
    }
    return found;
}

std::vector<Record> ParsePage(const std::string& name, const std::string& html) {
    std::vector<Record> records;
    std::string heading;
    for (const Span& row : Elements(html, "<tr", "</tr>")) {
        const std::string rowText = html.substr(row.begin, row.end - row.begin);
        std::vector<std::string> cells;
        for (const Span& cell : Elements(rowText, "<td", "</td>")) cells.push_back(rowText.substr(cell.begin, cell.end - cell.begin));
        if (cells.size() == 1 && FindCI(rowText, "colspan") != std::string::npos) {
            heading = TextOf(cells[0]);
            continue;
        }
        if (cells.size() != 4 || cells[1].find("Item Level") == std::string::npos) continue;
        // The part of the first cell before the coupon text.
        const std::size_t cut = FindCI(cells[0], "<font face=\"arial,helvetica\" size=\"-1\" color");
        const std::string head = cut == std::string::npos ? cells[0] : cells[0].substr(0, cut);
        std::vector<std::string> parts = Pieces(head);
        Record rec;
        const auto gamble = std::find(parts.begin(), parts.end(), "Gamble Item:");
        if (gamble != parts.end()) {
            if (gamble + 1 != parts.end()) rec.gamble = *(gamble + 1);
            parts.erase(gamble, parts.end());
        }
        if (parts.empty()) continue;
        rec.title = parts[0];
        if (EndsWith(rec.title, "(") && parts.size() >= 4) {
            rec.title += parts[1] + ")";
            rec.base = parts[3];
        } else {
            rec.base = parts.size() > 1 ? parts[1] : "";
        }
        rec.base = StripCode(rec.base, &rec.code);
        const std::string levels = TextOf(cells[1]);
        rec.page = name;
        rec.heading = heading;
        rec.group = HeadingGroup(name, heading);
        rec.coupon = cells[0].find("Ancient Coupon Unique") != std::string::npos;
        rec.ilvl = LevelValue(levels, "Item Level");
        rec.level = LevelValue(levels, "Required Level");
        rec.lines = PropertyLines(cells[2]);
        rec.note = TextOf(cells[3]);
        records.push_back(rec);
    }
    return records;
}

// ---- The page ---------------------------------------------------------------------------------------------------

struct Group {
    const char* key;
    const char* title;
};
const Group kGroups[] = {{"Mythical", "Mythical Uniques"}, {"Weapon", "Weapons"}, {"Helm", "Helms"}, {"Torso", "Torso"},
                         {"Shield", "Shields"}, {"Belt", "Belts"}, {"Boots", "Boots"}, {"Gloves", "Gloves"},
                         {"Ring", "Rings"}, {"Amulet", "Amulets"}, {"Jewel", "Jewels"}, {"Charm", "Charms"},
                         {"Quiver", "Quivers"}};

const char kPageId[] = "uniques";
const char kPageTitle[] = "Uniques";

// Page span [begin, end) of each page id in `lines`.
std::map<std::string, std::pair<std::size_t, std::size_t>> PageSpans(const std::vector<std::string>& lines) {
    std::map<std::string, std::pair<std::size_t, std::size_t>> spans;
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < lines.size(); ++i) if (StartsWith(lines[i], "PAGE\t")) starts.push_back(i);
    for (std::size_t n = 0; n < starts.size(); ++n) {
        const std::vector<std::string> parts = SplitTabs(lines[starts[n]]);
        if (parts.size() > 1) spans[parts[1]] = {starts[n], n + 1 < starts.size() ? starts[n + 1] : lines.size()};
    }
    return spans;
}

// "Tathamet's Awakening (Fire Variant)" -> "Tathamet's Awakening"
std::string WithoutVariant(const std::string& title) {
    if (!EndsWith(title, "Variant)")) return title;
    const std::size_t open = title.rfind('(');
    if (open == std::string::npos || title.find(')', open) != title.size() - 1) return title;
    std::size_t end = open;
    while (end > 0 && title[end - 1] == ' ') --end;
    return title.substr(0, end);
}

}  // namespace

bool BuildWebUniquesWikiData(const std::string& shipped, const std::vector<std::string>& pages,
                             const std::string& artTable, std::string& out, std::string& error) {
    if (pages.size() != 4) {
        error = "expected the four website pages";
        return false;
    }
    std::vector<Record> records;
    for (std::size_t n = 0; n < 4; ++n) {
        if (FindCI(pages[n], "</html") == std::string::npos) {
            error = std::string(kWebUniquePageNames[n]) + " is incomplete";
            return false;
        }
        std::vector<Record> found = ParsePage(kWebUniquePageNames[n], pages[n]);
        if (found.empty()) {
            error = std::string("no items found in ") + kWebUniquePageNames[n];
            return false;
        }
        records.insert(records.end(), found.begin(), found.end());
    }
    if (records.size() < 900) {
        error = "only " + std::to_string(records.size()) + " items found; the website pages look broken";
        return false;
    }

    std::vector<std::string> lines = SplitLines(shipped);
    const bool trailing = !lines.empty() && lines.back().empty();
    if (trailing) lines.pop_back();

    // web-unique-art.txt: the art, tier and weapon family the website does not give, by unique name and by base code.
    struct Art {
        std::string img, tier, family;  // img is a whole IMG line, or empty
    };
    std::map<std::string, Art> byTitle, byTitleNorm, byCode;
    for (const std::string& line : SplitLines(artTable)) {
        const std::vector<std::string> parts = SplitTabs(line);
        if (parts.size() < 6 || (parts[0] != "UNIQUE" && parts[0] != "BASE")) continue;
        Art art;
        if (!parts[2].empty()) art.img = "IMG\t" + parts[2] + (parts[3].empty() ? std::string() : "\t" + parts[3]);
        art.tier = parts[4];
        art.family = parts[5];
        (parts[0] == "UNIQUE" ? byTitle : byCode)[parts[1]] = art;
    }
    for (const auto& entry : byTitle) byTitleNorm[Norm(entry.first)] = entry.second;

    std::vector<std::vector<std::vector<std::string>>> entries(sizeof(kGroups) / sizeof(kGroups[0]));
    for (const Record& rec : records) {
        std::size_t g = 0;
        while (g < entries.size() && rec.group != kGroups[g].key) ++g;
        if (g == entries.size()) {
            error = "an item sits under an unknown table heading: " + rec.title;
            return false;
        }
        const bool mythical = rec.group == "Mythical";
        // The unique's own art first, else the art of its base item (by the code the website shows).
        const Art* own = nullptr;
        if (auto it = byTitle.find(rec.title); it != byTitle.end()) own = &it->second;
        else if (auto itn = byTitleNorm.find(Norm(rec.title)); itn != byTitleNorm.end()) own = &itn->second;
        else if (auto itp = byTitle.find(WithoutVariant(rec.title)); itp != byTitle.end()) own = &itp->second;
        const auto byBase = byCode.find(rec.code);
        const Art* baseArt = byBase != byCode.end() ? &byBase->second : nullptr;
        std::string img = own != nullptr ? own->img : std::string();
        if (img.empty() && baseArt != nullptr) img = baseArt->img;
        // Tier and weapon family belong to the base item.
        std::pair<std::string, std::string> sort;
        if (!mythical) {
            if (baseArt != nullptr) sort = {baseArt->tier, baseArt->family};
            else if (own != nullptr) sort = {own->tier, own->family};
        }
        // The website's own categories (weapon types, helm and shield kinds).
        if (rec.group == "Weapon" || rec.group == "Helm" || rec.group == "Torso" || rec.group == "Shield") sort.second = rec.heading;
        std::vector<std::string> block;
        block.push_back("BLOCK\t1\t" + rec.title + "\t" + (mythical ? rec.base : std::string()) + "\t\t" + sort.first +
                        "\t" + sort.second + "\t" +
                        (mythical ? std::string("0") : rec.level.empty() ? std::string("0") : rec.level));
        if (rec.group == "Charm") {
            // charms cannot be gambled and are not coupon items
        } else if (rec.coupon) {
            block.push_back("TAG\t(Coupon only)");
        } else if (!rec.gamble.empty() && Upper(rec.gamble) != "N/A") {
            block.push_back("TAG\tGamble Item:\\n" + StripCode(rec.gamble) + " - ilvl: " + rec.ilvl);
        }
        if (!mythical && !rec.base.empty()) block.push_back("FIELD\tBase\t" + rec.base);
        if (!rec.level.empty()) block.push_back("FIELD\tRequired Level\t" + rec.level);
        if (!img.empty()) block.push_back(img);
        for (const Line& line : rec.lines) block.push_back(std::string("LINE\tS\t") + kColorLead + line.color + line.text);
        if (!rec.note.empty()) block.push_back("NOTE\t" + rec.note);
        entries[g].push_back(std::move(block));
    }

    std::vector<std::string> page;
    page.push_back(std::string("PAGE\t") + kPageId + "\t" + kPageTitle + "\tWiki");
    for (std::size_t g = 0; g < entries.size(); ++g) {
        if (entries[g].empty()) continue;
        page.push_back(std::string("ENTRY\t") + kGroups[g].title + "\t" + std::to_string(entries[g].size()) + " items\t" +
                       (std::string(kGroups[g].key) == "Mythical" ? "wide" : "sortable"));
        for (const auto& block : entries[g]) page.insert(page.end(), block.begin(), block.end());
    }

    // Replace the page with this id; a file without one gets it before 'set-items' (else at the end).
    const auto spans = PageSpans(lines);
    std::vector<std::string> kept;
    const auto own = spans.find(kPageId);
    if (own != spans.end()) {
        kept.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(own->second.first));
        kept.insert(kept.end(), page.begin(), page.end());
        kept.insert(kept.end(), lines.begin() + static_cast<std::ptrdiff_t>(own->second.second), lines.end());
    } else {
        const auto next = spans.find("set-items");
        const std::size_t at = next != spans.end() ? next->second.first : lines.size();
        kept.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(at));
        kept.insert(kept.end(), page.begin(), page.end());
        kept.insert(kept.end(), lines.begin() + static_cast<std::ptrdiff_t>(at), lines.end());
    }

    std::string text;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (i != 0) text += '\n';
        text += kept[i];
    }
    if (trailing) text += '\n';
    out = std::move(text);
    return true;
}

}  // namespace esrwiki
