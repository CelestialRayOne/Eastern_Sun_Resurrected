#include <cstdlib>
#include "wiki_model.hpp"
#include "debug_log.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace esrwiki {
namespace {

constexpr const char* kColorPrefix = "\xC3\xBF" "c";  // U+00FF 'y-diaeresis' followed by 'c'
constexpr std::size_t kColorPrefixLength = 3;

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char ch : line) {
        if (ch == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string AsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool Contains(const std::string& haystack, const std::string& lowerNeedle) {
    return AsciiLower(haystack).find(lowerNeedle) != std::string::npos;
}

bool BlockContains(const Block& block, const std::string& needle) {
    if (Contains(block.title, needle) || Contains(block.subtitle, needle) || Contains(block.section, needle))
        return true;
    if (Contains(block.tag, needle)) return true;
    for (const Field& field : block.fields) {
        if (Contains(field.label, needle) || Contains(field.value, needle)) return true;
    }
    for (const Line& line : block.lines) {
        if (Contains(line.text, needle)) return true;
    }
    for (const RuneIcon& rune : block.runes) {
        if (Contains(rune.label, needle)) return true;
    }
    return false;
}

}  // namespace

std::size_t Database::entryCount() const {
    std::size_t total = 0;
    for (const Page& page : pages) total += page.entries.size();
    return total;
}

std::size_t Database::blockCount() const {
    std::size_t total = 0;
    for (const Page& page : pages)
        for (const Entry& entry : page.entries) total += entry.blocks.size();
    return total;
}

bool ParseDatabase(const std::string& text, Database& out, std::string& error) {
    out = Database{};
    std::istringstream stream(text);
    std::string raw;
    std::size_t lineNumber = 0;
    bool sawHeader = false;

    while (std::getline(stream, raw)) {
        ++lineNumber;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.empty()) continue;

        const std::vector<std::string> fields = SplitTabs(raw);
        const std::string& tag = fields[0];
        auto fail = [&](const char* reason) {
            error = "line " + std::to_string(lineNumber) + ": " + reason;
            return false;
        };
        auto field = [&](std::size_t index) -> std::string {
            return index < fields.size() ? fields[index] : std::string();
        };

        if (!sawHeader) {
            if (tag != "#ESRWIKI") return fail("missing #ESRWIKI header");
            if (field(1) != "2") return fail("unsupported format version, expected 2");
            sawHeader = true;
            continue;
        }

        if (tag == "PAGE") {
            if (fields.size() < 4) return fail("PAGE needs id, title and group");
            Page page;
            page.id = fields[1];
            page.title = fields[2];
            page.group = fields[3];
            page.compact = field(4) == "compact";
            out.pages.push_back(std::move(page));
            continue;
        }
        if (tag == "ENTRY") {
            if (out.pages.empty()) return fail("ENTRY before any PAGE");
            Entry entry;
            entry.title = field(1);
            entry.subtitle = field(2);
            entry.wide = field(3) == "wide";
            entry.compact = field(3) == "compact";
            entry.sortable = field(3) == "sortable";
            for (std::size_t flag = 3; flag < fields.size(); ++flag) {
                if (fields[flag] == "fiveacross") entry.fiveAcross = true;
                if (fields[flag] == "keeporder") entry.keepOrder = true;
                if (fields[flag] == "rowsort") entry.rowSort = true;
                if (fields[flag] == "socketsort") entry.socketSort = true;
                if (fields[flag] == "split") entry.splitLayout = 1;
                if (fields[flag] == "splitflip") entry.splitLayout = 2;
            }
            if (entry.title.empty()) return fail("ENTRY needs a title");
            out.pages.back().entries.push_back(std::move(entry));
            continue;
        }

        if (out.pages.empty() || out.pages.back().entries.empty())
            return fail("record before any ENTRY");
        Entry& entry = out.pages.back().entries.back();

        if (tag == "BLOCK") {
            Block block;
            block.item = field(1) == "1";
            block.title = field(2);
            block.subtitle = field(3);
            block.section = field(4);
            block.tier = field(5);
            block.family = field(6);
            block.level = std::atoi(field(7).c_str());
            entry.blocks.push_back(std::move(block));
            continue;
        }

        // Records below belong to the most recent block. Guide sections often have
        // no BLOCK line, so one is created on demand.
        if (entry.blocks.empty()) entry.blocks.emplace_back();
        Block& block = entry.blocks.back();

        if (tag == "FIELD") {
            if (fields.size() < 3) return fail("FIELD needs a label and value");
            block.fields.push_back(Field{fields[1], fields[2]});
        } else if (tag == "IMG") {
            if (fields.size() < 2) return fail("IMG needs a key");
            block.images.push_back(fields[1]);
            if (fields.size() > 2 && fields[2] == "small") block.smallArt = true;
            if (fields.size() > 2 && fields[2] == "tiny") block.tinyArt = true;
            if (fields.size() > 2 && fields[2] == "original") block.originalSize = true;
        } else if (tag == "TAG") {
            block.tag = fields.size() > 1 ? fields[1] : std::string();
            // A two-character "\\n" in the file is a line break in the tag.
            for (std::size_t at = block.tag.find("\\n"); at != std::string::npos; at = block.tag.find("\\n", at + 1))
                block.tag.replace(at, 2, "\n");
        } else if (tag == "NOTE") {
            block.note = fields.size() > 1 ? fields[1] : std::string();
        } else if (tag == "RUNE") {
            if (fields.size() < 3) return fail("RUNE needs a key and label");
            block.runes.push_back(RuneIcon{fields[1], fields[2], fields.size() > 3 && fields[3] == "lod"});
        } else if (tag == "LINE") {
            if (fields.size() < 3) return fail("LINE needs a kind and text");
            const std::string& kind = fields[1];
            LineKind lineKind = LineKind::Text;
            if (kind == "H") lineKind = LineKind::Heading;
            else if (kind == "S") lineKind = LineKind::Stat;
            else if (kind == "R") lineKind = LineKind::Row;
            else if (kind == "C") lineKind = LineKind::Header;
            else if (kind != "T") return fail("LINE kind must be H, T, S, R or C");
            block.lines.push_back(Line{lineKind, fields[2]});
        } else {
            return fail("unknown record type");
        }
    }

    if (!sawHeader) {
        error = "empty database";
        return false;
    }
    return true;
}

std::vector<std::size_t> FilterEntries(const Page& page, const std::string& query) {
    const std::string needle = AsciiLower(query);
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < page.entries.size(); ++index) {
        const Entry& entry = page.entries[index];
        bool matched = needle.empty() || Contains(entry.title, needle) || Contains(entry.subtitle, needle);
        for (std::size_t i = 0; !matched && i < entry.blocks.size(); ++i) {
            matched = BlockContains(entry.blocks[i], needle);
        }
        if (matched) matches.push_back(index);
    }
    return matches;
}

bool HasRuneBlocks(const Entry& entry) {
    for (const Block& block : entry.blocks) {
        if (!block.runes.empty()) return true;
    }
    return false;
}

// How many sockets a block has: its runes for runewords, or the Sockets field for gemwords (which have no runes).
static std::size_t BlockSocketCount(const Block& block) {
    if (!block.runes.empty()) return block.runes.size();
    for (const Field& field : block.fields) {
        if (field.label != "Sockets") continue;
        std::size_t count = 0;
        for (char c : field.value) {
            if (c < '0' || c > '9') break;
            count = count * 10 + static_cast<std::size_t>(c - '0');
        }
        return count;
    }
    return 0;
}

std::vector<std::size_t> SocketCounts(const Entry& entry) {
    std::vector<std::size_t> counts;
    for (const Block& block : entry.blocks) {
        const std::size_t count = BlockSocketCount(block);
        if (count == 0) continue;
        if (std::find(counts.begin(), counts.end(), count) == counts.end()) counts.push_back(count);
    }
    std::sort(counts.begin(), counts.end());
    return counts;
}

bool BlockMatches(const Block& block, const std::string& query) {
    return BlockContains(block, AsciiLower(query));
}

bool IsRowSort(BlockSort sort) {
    return sort == BlockSort::RowMinIlvl || sort == BlockSort::RowMaxIlvl || sort == BlockSort::RowRarity
        || sort == BlockSort::RowRarityHigh;
}


// The number in a table cell, ignoring colour codes; -1 when there is none.
static long CellNumber(const std::string& cell) {
    std::size_t i = 0;
    while (i < cell.size()) {
        if (static_cast<unsigned char>(cell[i]) == 0xC3 && i + 3 < cell.size() && static_cast<unsigned char>(cell[i + 1]) == 0xBF && cell[i + 2] == 'c') {
            i += 4;  // "ÿc" and the colour letter
            continue;
        }
        if (cell[i] >= '0' && cell[i] <= '9') {
            long value = 0;
            while (i < cell.size() && cell[i] >= '0' && cell[i] <= '9') value = value * 10 + (cell[i++] - '0');
            return value;
        }
        ++i;
    }
    return -1;
}

static std::vector<std::string> SplitRowCells(const std::string& text) {
    std::vector<std::string> cells(1);
    for (const char c : text) {
        if (c == '\x1f') cells.emplace_back();
        else cells.back() += c;
    }
    return cells;
}

std::vector<Line> SortTableRows(const std::vector<Line>& lines, BlockSort sort) {
    if (!IsRowSort(sort)) return lines;
    const char* const column = sort == BlockSort::RowMinIlvl ? "Min iLvl" : sort == BlockSort::RowMaxIlvl ? "Max iLvl" : "Rarity";
    const bool highFirst = sort == BlockSort::RowMaxIlvl || sort == BlockSort::RowRarityHigh;
    std::vector<Line> out = lines;
    std::size_t index = 0;
    while (index < out.size()) {
        if (out[index].kind != LineKind::Row) { ++index; continue; }
        std::size_t end = index;
        while (end < out.size() && out[end].kind == LineKind::Row) ++end;
        // the header row just above the rows says which column holds the number
        std::size_t position = std::string::npos;
        if (index > 0 && out[index - 1].kind == LineKind::Header) {
            const std::vector<std::string> names = SplitRowCells(out[index - 1].text);
            for (std::size_t c = 0; c < names.size(); ++c)
                if (names[c] == column) position = c;
            // A translated page names the columns in its own language; they keep the English order (Name, Rare, Min iLvl,
            // Max iLvl, Req Lvl, Rarity, Group, Stats).
            const bool byName = position != std::string::npos;
            if (position == std::string::npos && names.size() == 8)
                position = sort == BlockSort::RowMinIlvl ? 2 : sort == BlockSort::RowMaxIlvl ? 3 : 5;
            DebugLogOnce(std::string("sort:") + column + ":" + std::to_string(names.size()) + (position == std::string::npos ? ":none" : ":ok"),
                         std::string("row sort by ") + column + ": " +
                             (position == std::string::npos ? "no matching column (the header has " + std::to_string(names.size()) + " cells)"
                                                            : "column " + std::to_string(position) + (byName ? " found by name" : " found by position")));
        }
        if (position != std::string::npos) {
            std::vector<long> keys;
            for (std::size_t r = index; r < end; ++r) {
                const std::vector<std::string> cells = SplitRowCells(out[r].text);
                long value = position < cells.size() ? CellNumber(cells[position]) : -1;
                if (value < 0 && highFirst && sort == BlockSort::RowMaxIlvl) value = 1000000;  // no maximum item level: no upper limit, so it counts as the highest
                keys.push_back(value);
            }
            std::vector<std::size_t> order(end - index);
            for (std::size_t k = 0; k < order.size(); ++k) order[k] = k;
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return highFirst ? keys[a] > keys[b] : keys[a] < keys[b];
            });
            std::vector<Line> block;
            block.reserve(order.size());
            for (const std::size_t k : order) block.push_back(out[index + k]);
            std::copy(block.begin(), block.end(), out.begin() + static_cast<std::ptrdiff_t>(index));
        }
        index = end;
    }
    return out;
}

BlockSort ResolveBlockSort(const Entry& entry, BlockSort sort) {
    if (entry.rowSort) return IsRowSort(sort) ? sort : BlockSort::Default;  // the blocks keep their order
    if (entry.socketSort) return BlockSort::Default;  // gemwords keep their order; the Sockets filter picks
    if (sort != BlockSort::Default) return sort;
    // Only sortable entries have a level sort; everything else keeps its section order.
    return entry.sortable ? BlockSort::LevelLowFirst : BlockSort::Default;
}

// Lower-cased titles, indexed like the blocks, so a name sort compares them without copying.
static std::vector<std::string> LowerTitles(const std::vector<Block>& blocks) {
    std::vector<std::string> titles;
    titles.reserve(blocks.size());
    for (const Block& block : blocks) titles.push_back(AsciiLower(block.title));
    return titles;
}

std::vector<std::size_t> ArrangeBlocks(const Entry& entry, std::size_t socketFilter, const std::string& tierFilter,
                                       const std::string& familyFilter, BlockSort sort, const std::string& query) {
    sort = ResolveBlockSort(entry, sort);
    const std::string needle = AsciiLower(query);
    const bool wholeEntry = needle.empty() || Contains(entry.title, needle) || Contains(entry.subtitle, needle);
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < entry.blocks.size(); ++i) {
        const Block& block = entry.blocks[i];
        if (socketFilter != 0 && BlockSocketCount(block) != socketFilter) continue;
        if (!tierFilter.empty() && block.tier != tierFilter) continue;
        if (!familyFilter.empty() && block.family != familyFilter) continue;
        if (!wholeEntry && !BlockContains(block, needle)) continue;
        order.push_back(i);
    }

    if (entry.sortable) {
        const auto& blocks = entry.blocks;
        switch (sort) {
            case BlockSort::Name: {
                const std::vector<std::string> titles = LowerTitles(blocks);
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    return titles[a] < titles[b];
                });
                break;
            }
            case BlockSort::LevelHighFirst:
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    return blocks[a].level > blocks[b].level;
                });
                break;
            default:  // LevelLowFirst
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    return blocks[a].level < blocks[b].level;
                });
                break;
        }
        return order;
    }

    if (sort == BlockSort::Name) {
        const std::vector<std::string> titles = LowerTitles(entry.blocks);
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return titles[a] < titles[b];
        });
        return order;
    }

    // Keep each section together, in the order its first block appears.
    std::vector<std::string> sectionOrder;
    std::vector<std::size_t> rank(order.size(), 0);
    for (std::size_t k = 0; k < order.size(); ++k) {
        const std::string& section = entry.blocks[order[k]].section;
        auto found = std::find(sectionOrder.begin(), sectionOrder.end(), section);
        if (found == sectionOrder.end()) {
            sectionOrder.push_back(section);
            found = sectionOrder.end() - 1;
        }
        rank[k] = static_cast<std::size_t>(found - sectionOrder.begin());
    }
    std::vector<std::size_t> keys(order.size());
    for (std::size_t k = 0; k < keys.size(); ++k) keys[k] = k;
    std::stable_sort(keys.begin(), keys.end(), [&](std::size_t a, std::size_t b) { return rank[a] < rank[b]; });
    std::vector<std::size_t> grouped;
    grouped.reserve(order.size());
    for (std::size_t k : keys) grouped.push_back(order[k]);
    return grouped;
}

std::size_t PageCount(std::size_t total, std::size_t perPage) {
    if (perPage == 0) return 1;
    return std::max<std::size_t>(1, (total + perPage - 1) / perPage);
}

std::vector<ColorRun> SplitColorCodes(const std::string& text) {
    std::vector<ColorRun> runs;
    ColorRun current;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, kColorPrefixLength, kColorPrefix) == 0 && i + kColorPrefixLength < text.size()) {
            if (!current.text.empty()) runs.push_back(current);
            current.text.clear();
            current.code = text[i + kColorPrefixLength];
            i += kColorPrefixLength + 1;
            continue;
        }
        current.text.push_back(text[i]);
        ++i;
    }
    if (!current.text.empty() || runs.empty()) runs.push_back(current);
    return runs;
}

std::vector<Line> RuneBonusLines(const Database& db, const std::string& runeLabel, bool lod,
                                 const std::string& entryTitle) {
    std::vector<Line> out;
    const std::string wanted = runeLabel + " Rune";
    // The Runes page gives each rune's lines under the website's own item-kind labels ("Weapons / Gloves",
    // "Helms / Boots", "Armor / Shields / Belts", "All Items"). Only the sections that name the card's item kind
    // (or "All Items") are shown, without their labels; a kind the website gives no bonus for (charms) shows none.
    std::string kind;  // the label word for this kind of item
    if (entryTitle == "Torso") kind = "Armor";
    else if (entryTitle == "Weapons" || entryTitle == "Gloves" || entryTitle == "Helms" || entryTitle == "Boots" ||
             entryTitle == "Shields" || entryTitle == "Belts") kind = entryTitle;
    auto keep = [&](const std::string& heading) {
        if (heading == "All Items") return true;
        if (kind.empty()) return false;
        std::size_t start = 0;
        for (;;) {
            const std::size_t at = heading.find(" / ", start);
            if (heading.substr(start, at == std::string::npos ? std::string::npos : at - start) == kind) return true;
            if (at == std::string::npos) return false;
            start = at + 3;
        }
    };
    for (const Page& page : db.pages) {
        if (page.id != "runes") continue;
        for (const Entry& entry : page.entries) {
            if ((entry.title == "LoD Runes") != lod) continue;
            for (const Block& block : entry.blocks) {
                if (block.title != wanted) continue;
                bool include = true;
                for (const Line& line : block.lines) {
                    std::string plain;
                    for (const ColorRun& run : SplitColorCodes(line.text)) plain += run.text;
                    if (!plain.empty() && plain.back() == ':' && line.text.compare(0, 4, "\xC3\xBF" "c0") == 0) {
                        include = keep(plain.substr(0, plain.size() - 1));
                        continue;
                    }
                    if (include) out.push_back(line);
                }
                return out;
            }
        }
    }
    return out;
}

}  // namespace esrwiki
