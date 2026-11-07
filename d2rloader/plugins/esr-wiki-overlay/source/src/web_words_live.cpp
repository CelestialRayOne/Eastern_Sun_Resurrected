#include "web_words_live.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

constexpr std::size_t kRunewordMin = 300;
constexpr std::size_t kGemwordMin = 500;
constexpr std::size_t kLevelsMin = 100;
const char kColorLead[] = "\xC3\xBF" "c";  // the colour-code lead character, then 'c'
const char kOuterColor[] = "#908858";       // the colour of the ingredient cell itself, not of a rune

const char* const kLabels[3] = {"Weapons / Gloves", "Helms / Boots", "Armor / Shields / Belts"};
const char* const kCategories[9] = {"Weapons", "Torso", "Boots", "Gloves", "Helms", "Shields", "Belts", "Charms", "Other"};

// ---- Allowed items -> item categories ---------------------------------------------------------------------------------

std::vector<std::string> ArmorCategories(const std::string& token) {
    static const std::map<std::string, std::vector<std::string>> table = {
        {"Body Armor", {"Torso"}}, {"Helm", {"Helms"}}, {"Pelt", {"Helms"}}, {"Any Shield", {"Shields"}},
        {"Paladin Item", {"Shields"}}, {"Gloves", {"Gloves"}}, {"Boots", {"Boots"}}, {"Belt", {"Belts"}},
        {"Charm", {"Charms"}}, {"Any Armor", {"Torso", "Boots", "Gloves", "Helms", "Shields", "Belts"}}};
    const auto found = table.find(token);
    return found != table.end() ? found->second : std::vector<std::string>();
}

bool IsWeaponToken(const std::string& token) {
    static const char* const tokens[] = {"Weapon", "Missile", "Melee Weapon", "Missile Weapon", "Two-Handed Melee Weapon",
                                         "Hand to Hand", "Assassin 2H Katana", "Axe", "Blunt", "Club", "Crossbow",
                                         "Hammer", "Knife", "Mace", "Orb", "Paladin Sword", "Polearm", "Scepter",
                                         "Shuriken", "Sorceress Mana Blade", "Spear", "Staff", "Sword", "Wand"};
    for (const char* t : tokens) if (token == t) return true;
    return false;
}

// ---- The art table ----------------------------------------------------------------------------------------------------

struct Art {
    std::map<std::pair<std::string, std::string>, std::string> word;  // (norm(name), ingredient key) -> picture
    std::map<std::string, std::string> type;                           // allowed item -> picture
    std::vector<std::pair<std::string, std::string>> icon;             // lower-case word -> icon (in file order)
    std::map<std::string, std::string> alias;                          // gem word -> the mod's spelling
    std::map<std::string, int> maxType;                                // item type name -> most sockets a base can have
    std::map<std::string, int> maxCat;                                 // category -> most sockets a base can have
};

Art ReadArt(const std::string& text) {
    Art art;
    for (const std::string& line : SplitLines(text)) {
        const std::vector<std::string> parts = SplitTabs(line);
        if (parts[0] == "RUNEWORD" && parts.size() >= 4) art.word[{Norm(parts[1]), parts[2]}] = parts[3];
        else if (parts[0] == "TYPE" && parts.size() >= 3) art.type[parts[1]] = parts[2];
        else if (parts[0] == "ICON" && parts.size() >= 3) {
            bool replaced = false;
            for (auto& entry : art.icon) if (entry.first == Lower(parts[1])) { entry.second = parts[2]; replaced = true; }
            if (!replaced) art.icon.emplace_back(Lower(parts[1]), parts[2]);
        } else if (parts[0] == "MAXSOCKETS" && parts.size() >= 3) {
            int& cap = art.maxType[parts[1]];
            cap = std::max(cap, std::stoi(parts[2]));
        } else if (parts[0] == "MAXCAT" && parts.size() >= 3) art.maxCat[parts[1]] = std::stoi(parts[2]);
        else if (parts[0] == "ALIAS" && parts.size() >= 3) art.alias[Lower(parts[1])] = parts[2];
    }
    return art;
}

std::string Slug(const std::string& text) {
    std::string out;
    bool pending = false;
    for (char ch : Lower(text)) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            if (pending && !out.empty()) out += '_';
            pending = false;
            out += ch;
        } else {
            pending = true;
        }
    }
    return out;
}

std::string IngredientKey(const std::vector<std::string>& names) {
    std::string key;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) key += '+';
        key += Lower(names[i]).find("jewel") != std::string::npos ? std::string("jewel") : Norm(names[i]);
    }
    return key;
}

std::string GemKey(const std::string& name, const Art& art) {
    std::string out = "items_misc_gem_";
    const std::string slug = Slug(name);
    std::size_t start = 0;
    bool first = true;
    for (;;) {
        const std::size_t at = slug.find('_', start);
        const std::string word = slug.substr(start, at == std::string::npos ? std::string::npos : at - start);
        const auto alias = art.alias.find(word);
        if (!first) out += '_';
        out += alias != art.alias.end() ? alias->second : word;
        first = false;
        if (at == std::string::npos) break;
        start = at + 1;
    }
    return out;
}

struct Icon {
    std::string key, label;
    bool lod;
};

Icon IconOf(const std::string& label, bool uncoloured, const Art& art) {
    const std::string lower = Lower(label);
    for (const auto& entry : art.icon) if (lower.find(entry.first) != std::string::npos) return {entry.second, label, false};
    if (EndsWith(label, " Rune")) return {"items_misc_rune_" + Slug(label), label.substr(0, label.size() - 5), uncoloured};
    return {GemKey(label, art), label, false};
}

// ---- The gems page (levels) ---------------------------------------------------------------------------------------------

using Levels = std::map<std::pair<std::string, bool>, int>;

Levels ReadLevels(const std::string& html) {
    Levels levels;
    for (const Span& rowSpan : Elements(html, "<tr", "</tr>")) {
        const std::string row = html.substr(rowSpan.begin, rowSpan.end - rowSpan.begin);
        const std::vector<Span> cells = Elements(row, "<td", "</td>");
        if (cells.size() != 1) continue;
        const std::string cell = row.substr(cells[0].begin, cells[0].end - cells[0].begin);
        const std::string plain = TextOf(cell);
        std::size_t at = 0;
        std::string level;
        if (!FindReqLvl(plain, at, level) || level.empty()) continue;
        const std::string name = Trim(plain.substr(0, at));
        levels.emplace(std::make_pair(name, NameColor(cell).empty()), std::stoi(level));
    }
    return levels;
}

// -1 when the page does not give the level.
int LevelOf(const Levels& levels, const std::string& name, bool uncoloured) {
    for (bool wanted : {uncoloured, !uncoloured}) {
        const auto found = levels.find({name, wanted});
        if (found != levels.end()) return found->second;
    }
    return -1;
}

// ---- The recipe pages ---------------------------------------------------------------------------------------------------

using Group = std::vector<Line>;

struct Row {
    std::string name, sockets;
    std::vector<std::pair<std::string, bool>> ingredients;  // text, written without a colour
    std::vector<std::string> allowed, excluded;
    std::vector<std::vector<Group>> columns;
};

// The colours of the <font color=...> tags in `part` (re.findall(r"<font[^>]*color\s*=\s*\"?([#0-9A-Za-z]+)")).
std::vector<std::string> FontColors(const std::string& part) {
    std::vector<std::string> out;
    for (std::size_t at = FindCI(part, "<font"); at != std::string::npos; at = FindCI(part, "<font", at + 1)) {
        const std::size_t end = part.find('>', at);
        const std::string tag = part.substr(at, end == std::string::npos ? std::string::npos : end - at);
        std::string value;
        // [^>]* is greedy, so the last "color" that is followed by = and a value wins.
        std::vector<std::size_t> candidates;
        for (std::size_t c = FindCI(tag, "color"); c != std::string::npos; c = FindCI(tag, "color", c + 1)) candidates.push_back(c);
        for (auto it = candidates.rbegin(); it != candidates.rend() && value.empty(); ++it) {
            std::size_t p = *it + 5;
            while (p < tag.size() && IsSpace(tag[p])) ++p;
            if (p >= tag.size() || tag[p] != '=') continue;
            ++p;
            while (p < tag.size() && IsSpace(tag[p])) ++p;
            if (p < tag.size() && tag[p] == '"') ++p;
            while (p < tag.size() && (tag[p] == '#' || (tag[p] >= '0' && tag[p] <= '9') || (tag[p] >= 'a' && tag[p] <= 'z') ||
                                      (tag[p] >= 'A' && tag[p] <= 'Z')))
                value += tag[p++];
        }
        if (!value.empty()) out.push_back(value);
    }
    return out;
}

// re.search(r"\((\d+(?:-\d+)?)\s*Socket", text, re.I)
bool FindSockets(const std::string& text, std::string& sockets) {
    for (std::size_t open = text.find('('); open != std::string::npos; open = text.find('(', open + 1)) {
        std::size_t k = open + 1;
        const std::size_t digits = k;
        while (k < text.size() && text[k] >= '0' && text[k] <= '9') ++k;
        if (k == digits) continue;
        if (k < text.size() && text[k] == '-') {
            std::size_t m = k + 1;
            const std::size_t more = m;
            while (m < text.size() && text[m] >= '0' && text[m] <= '9') ++m;
            if (m > more) k = m;
        }
        const std::size_t number_end = k;
        while (k < text.size() && IsSpace(text[k])) ++k;
        if (FindCI(text, "socket", k) == k) {
            sockets = text.substr(digits, number_end - digits);
            return true;
        }
    }
    return false;
}

std::vector<Row> ParseRows(const std::string& html, std::vector<std::string>& labels) {
    labels.assign(kLabels, kLabels + 3);
    std::vector<Row> rows;
    bool haveLabels = false;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = FindCI(html, "<tr", at);
        if (open == std::string::npos) break;
        const std::size_t gt = html.find('>', open);
        if (gt == std::string::npos) break;
        const std::size_t close = FindCI(html, "</tr>", gt + 1);
        if (close == std::string::npos) break;
        at = close + 5;
        const std::string attrs = html.substr(open + 3, gt - open - 3);
        const std::string row = html.substr(gt + 1, close - gt - 1);
        if (!haveLabels && FindCI(row, "<td") == std::string::npos) {
            const std::vector<Span> heads = Elements(row, "<th", "</th>");
            if (heads.size() == 6) {
                std::vector<std::string> found;
                bool all = true;
                for (std::size_t i = 3; i < 6; ++i) {
                    found.push_back(TextOf(row.substr(heads[i].begin, heads[i].end - heads[i].begin)));
                    if (found.back().empty()) all = false;
                }
                if (all) labels = found;
                haveLabels = true;
            }
        }
        if (FindCI(attrs, "recipeRow") == std::string::npos) continue;
        std::vector<std::string> cells;
        for (const Span& cell : Elements(row, "<td", "</td>")) cells.push_back(row.substr(cell.begin, cell.end - cell.begin));
        if (cells.size() != 6) continue;
        Row out;
        const std::size_t bold = FindCI(cells[0], "<b>");
        if (bold != std::string::npos) {
            const std::size_t boldEnd = FindCI(cells[0], "</b>", bold + 3);
            if (boldEnd != std::string::npos) out.name = TextOf(cells[0].substr(bold + 3, boldEnd - bold - 3));
        }
        if (out.name.empty() || !FindSockets(TextOf(cells[0]), out.sockets)) continue;
        for (const std::string& part : SplitBr(cells[1])) {
            const std::string label = TextOf(part);
            if (label.empty()) continue;
            bool coloured = false;
            for (const std::string& color : FontColors(part)) if (Lower(color) != kOuterColor) coloured = true;
            out.ingredients.emplace_back(label, !coloured);
        }
        bool inExcluded = false;
        for (const std::string& part : SplitBr(cells[2])) {
            const std::string label = TextOf(part);
            if (label.empty()) continue;
            if (Lower(label).compare(0, 8, "excluded") == 0) inExcluded = true;
            else (inExcluded ? out.excluded : out.allowed).push_back(label);
        }
        for (std::size_t c = 3; c < 6; ++c) {
            std::vector<Group> groups;
            Group current;
            for (const Line& line : PropertyLines(cells[c], false, true)) {
                if (line.color == kGap) {
                    if (!current.empty()) groups.push_back(current);
                    current.clear();
                } else {
                    current.push_back(line);
                }
            }
            if (!current.empty()) groups.push_back(current);
            out.columns.push_back(std::move(groups));
        }
        rows.push_back(std::move(out));
    }
    return rows;
}

bool SameGroup(const Group& a, const Group& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i].color != b[i].color || a[i].text != b[i].text) return false;
    return true;
}

std::string AllowedText(const Row& row) {
    std::string text = Join(row.allowed, ", ");
    if (!row.excluded.empty()) text += " (Excluded: " + Join(row.excluded, ", ") + ")";
    return text;
}

std::string Plural(const std::string& sockets) { return sockets + (sockets == "1" ? " Socket" : " Sockets"); }

// False when the item type is known to take fewer sockets than the card needs (the lowest of a range).
bool CanHold(const Row& row, std::string token, const std::string& cat, const Art& art) {
    if (token == "Missile") token = "Missile Weapon";  // the website's name for missile weapons
    int cap = -1;
    if (token == "Any Armor") {
        const auto found = art.maxCat.find(cat);
        if (found != art.maxCat.end()) cap = found->second;
    } else {
        const auto type = art.maxType.find(token);
        const auto category = art.maxCat.find(cat);
        if (type != art.maxType.end()) cap = type->second;
        else if (category != art.maxCat.end()) cap = category->second;
    }
    return cap < 0 || std::stoi(row.sockets) <= cap;  // std::stoi reads the lowest of "N-M"
}

std::vector<std::string> CategoriesOf(const Row& row, bool firstOnly, const Art& art) {
    std::vector<std::string> found;
    for (const std::string& token : row.allowed) {
        std::vector<std::string> cats = ArmorCategories(token);
        if (cats.empty()) cats = {IsWeaponToken(token) ? "Weapons" : "Other"};
        for (const std::string& cat : cats)
            if (CanHold(row, token, cat, art) && std::find(found.begin(), found.end(), cat) == found.end()) found.push_back(cat);
        if (firstOnly && !found.empty()) break;
    }
    if (firstOnly && found.size() > 1) found.resize(1);
    return found;  // empty when no allowed item can hold that many sockets: no card
}

// The category a picture belongs to, by its name ("items_armor_helmet_..." = Helms); empty when not known.
std::string ImageCategory(const std::string& key) {
    static const char* const prefixes[][2] = {{"items_armor_armor", "Torso"}, {"items_armor_boot", "Boots"},
                                              {"items_armor_glove", "Gloves"}, {"items_armor_helmet", "Helms"},
                                              {"items_armor_shield", "Shields"}, {"items_armor_belt", "Belts"},
                                              {"items_misc_charm", "Charms"}, {"items_weapon", "Weapons"}};
    for (const auto& entry : prefixes) if (StartsWith(key, entry[0])) return entry[1];
    return std::string();
}

// The picture of a runeword card in one category: the mod's own picture when it is of that kind of item, else the
// picture of the kind of item of the category (the first weapon type the runeword allows, for weapons).
std::string RunewordImage(const Row& row, const std::string& cat, const std::string& wordImage, const Art& art) {
    if (!wordImage.empty() && ImageCategory(wordImage) == cat) return wordImage;
    std::vector<std::string> kinds;
    if (cat == "Weapons") {
        for (const std::string& token : row.allowed) if (IsWeaponToken(token)) kinds.push_back(token);
        kinds.push_back("Weapon");
    } else {
        static const std::map<std::string, std::string> byCategory = {{"Torso", "Body Armor"}, {"Boots", "Boots"},
            {"Gloves", "Gloves"}, {"Helms", "Helm"}, {"Shields", "Any Shield"}, {"Belts", "Belt"}, {"Charms", "Charm"}};
        const auto found = byCategory.find(cat);
        kinds.push_back(found != byCategory.end() ? found->second : std::string());
    }
    for (const std::string& kind : kinds) {
        const auto type = art.type.find(kind);
        if (type != art.type.end()) return type->second;
    }
    return ImageCategory(wordImage).empty() ? wordImage : std::string();
}

std::string LineOf(char color, const std::string& text) { return std::string("LINE\tS\t") + kColorLead + color + text; }

// The card's stat lines: group by group, shared when the columns agree, else under each column's label.
// The item-kind word the website's column labels use for a category ("Torso" is "Armor"); empty for charms.
std::string LabelKind(const std::string& cat) {
    if (cat == "Torso") return "Armor";
    for (const char* kind : {"Weapons", "Gloves", "Helms", "Boots", "Shields", "Belts"}) if (cat == kind) return cat;
    return std::string();
}

bool LabelHasKind(const std::string& label, const std::string& kind) {
    std::size_t start = 0;
    for (;;) {
        const std::size_t at = label.find(" / ", start);
        if (label.substr(start, at == std::string::npos ? std::string::npos : at - start) == kind) return true;
        if (at == std::string::npos) return false;
        start = at + 3;
    }
}

// With a heading (runewords) the last group is the rune bonuses: only those of the category's own column are shown,
// without a label, and none at all for charms.
void StatLines(const Row& row, const std::vector<std::string>& labels, const char* heading, const std::string& cat,
               std::vector<std::string>& out) {
    std::size_t count = 0;
    for (const auto& column : row.columns) count = std::max(count, column.size());
    for (std::size_t g = 0; g < count; ++g) {
        std::vector<Group> cells;
        for (const auto& column : row.columns) cells.push_back(g < column.size() ? column[g] : Group());
        bool any = false;
        for (const Group& cell : cells) if (!cell.empty()) any = true;
        if (!any) continue;
        const std::string kind = LabelKind(cat);
        const Group* shown = nullptr;
        if (heading != nullptr && g == count - 1 && count >= 2) {  // the rune bonuses: none for charms
            for (std::size_t c = 0; c < cells.size() && c < labels.size() && shown == nullptr; ++c)
                if (!kind.empty() && !cells[c].empty() && LabelHasKind(labels[c], kind)) shown = &cells[c];
            if (shown != nullptr) {
                out.push_back(LineOf('4', heading));
                for (const Line& line : *shown) out.push_back(LineOf(line.color, line.text));
            }
            continue;
        }
        if (!kind.empty()) {  // only the column that names the category's kind of item
            for (std::size_t c = 0; c < cells.size() && c < labels.size() && shown == nullptr; ++c)
                if (LabelHasKind(labels[c], kind)) shown = &cells[c];
        } else {  // charms: what every column agrees on
            // Empty columns are kinds of item the row does not allow: only the filled columns are compared.
            std::vector<const Group*> used;
            for (const Group& cell : cells) if (!cell.empty()) used.push_back(&cell);
            bool agree = !used.empty();
            for (const Group* cell : used) if (!SameGroup(*cell, *used[0])) agree = false;
            if (agree) shown = used[0];
        }
        if (shown != nullptr) for (const Line& line : *shown) out.push_back(LineOf(line.color, line.text));
    }
}

std::vector<std::string> BuildPage(bool runewords, const std::vector<Row>& rows, const std::vector<std::string>& labels,
                                   const Art& art, const Levels& levels) {
    std::map<std::string, std::vector<std::vector<std::string>>> entries;
    for (const Row& row : rows) {
        std::vector<std::string> names;
        std::vector<Icon> icons;
        for (const auto& ingredient : row.ingredients) {
            names.push_back(ingredient.first);
            icons.push_back(IconOf(ingredient.first, ingredient.second, art));
        }
        int level = 0;
        std::string wordImage;
        if (runewords) {
            for (const auto& ingredient : row.ingredients) level = std::max(level, LevelOf(levels, ingredient.first, ingredient.second));
            const auto word = art.word.find({Norm(row.name), IngredientKey(names)});
            if (word != art.word.end()) wordImage = word->second;
        }
        std::vector<std::string> head;
        head.push_back("BLOCK\t1\t" + row.name + "\t\t" + Plural(row.sockets) + "\t\t" + (runewords ? std::string() : row.name) + "\t" +
                       std::to_string(level));
        head.push_back("FIELD\tSockets\t" + row.sockets);
        head.push_back("FIELD\tBase types\t" + AllowedText(row));
        std::vector<std::string> tail;
        if (runewords) {
            for (const Icon& icon : icons) tail.push_back("RUNE\t" + icon.key + "\t" + icon.label + (icon.lod ? "\tlod" : ""));
        }
        tail.push_back(LineOf('4', row.name));
        tail.push_back(LineOf('7', Join(names, " + ")));
        for (const std::string& cat : CategoriesOf(row, false, art)) {
            const std::string img = runewords ? RunewordImage(row, cat, wordImage, art) : (icons.empty() ? std::string() : icons[0].key);
            std::vector<std::string> block = head;
            if (!img.empty()) block.push_back("IMG\t" + img);
            block.insert(block.end(), tail.begin(), tail.end());
            StatLines(row, labels, runewords ? "Rune Bonuses:" : nullptr, cat, block);
            if (level > 0) block.push_back(LineOf('5', "Required Level: " + std::to_string(level)));
            block.push_back(LineOf('0', AllowedText(row)));
            auto& cards = entries[cat];  // the website lists a few recipes twice, word for word
            if (std::find(cards.begin(), cards.end(), block) == cards.end()) cards.push_back(std::move(block));
        }
    }
    std::vector<std::string> page;
    page.push_back(std::string("PAGE\t") + (runewords ? "runewords\tRunewords" : "gemwords\tGemwords") + "\tWiki");
    for (const char* cat : kCategories) {
        const auto found = entries.find(cat);
        if (found == entries.end() || found->second.empty()) continue;
        page.push_back(std::string("ENTRY\t") + cat + "\t" + std::to_string(found->second.size()) + " items\t" +
                       (runewords ? "sortable" : "compact\tsocketsort"));  // gemwords filter by socket count
        for (const auto& block : found->second) page.insert(page.end(), block.begin(), block.end());
    }
    return page;
}

bool Build(bool runewords, const std::string& current, const std::string& page, const std::string& gems, const std::string& artTable,
           std::string& out, std::string& error) {
    const char* name = runewords ? kWebRunewordsPageNames[0] : kWebGemwordsPageName;
    if (FindCI(page, "</html") == std::string::npos) {
        error = std::string(name) + " is incomplete";
        return false;
    }
    std::vector<std::string> labels;
    const std::vector<Row> rows = ParseRows(page, labels);
    if (rows.size() < (runewords ? kRunewordMin : kGemwordMin)) {
        error = "only " + std::to_string(rows.size()) + " recipes found; the website page looks broken";
        return false;
    }
    Levels levels;
    if (runewords) {
        levels = ReadLevels(gems);
        if (levels.size() < kLevelsMin) {
            error = "the gems page gives only " + std::to_string(levels.size()) + " levels";
            return false;
        }
    }
    out = Splice(current, BuildPage(runewords, rows, labels, ReadArt(artTable), levels), runewords ? "runewords" : "gemwords",
                 runewords ? "runes" : "runewords");
    return true;
}

}  // namespace

bool BuildWebRunewordsWikiData(const std::string& current, const std::string& runewordsPage, const std::string& gemsPage,
                               const std::string& artTable, std::string& out, std::string& error) {
    return Build(true, current, runewordsPage, gemsPage, artTable, out, error);
}

bool BuildWebGemwordsWikiData(const std::string& current, const std::string& gemwordsPage, const std::string& artTable,
                              std::string& out, std::string& error) {
    return Build(false, current, gemwordsPage, std::string(), artTable, out, error);
}

}  // namespace esrwiki
