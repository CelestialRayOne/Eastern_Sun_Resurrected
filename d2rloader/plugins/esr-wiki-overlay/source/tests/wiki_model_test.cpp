// Tests for the platform-neutral wiki model. Runs on inline samples and on the
// generated data/wiki-data.txt (run from the overlay folder, or pass its path).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/wiki_model.hpp"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::string ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

const char* kSample =
    "#ESRWIKI\t2\n"
    "PAGE\tguide\tGuide\tGuide\n"
    "ENTRY\tIntroduction\tWelcome\n"
    "LINE\tT\tHello there\n"
    "PAGE\titems\tItems\tItem Library\n"
    "ENTRY\tWeapons\t2 items\n"
    "BLOCK\t1\tBoar\t\t1 Socket\n"
    "FIELD\tSockets\t1\n"
    "FIELD\tBase types\tWeapon, Bow Quiver\n"
    "IMG\titems_weapon_polearm_scythe\n"
    "RUNE\titems_misc_rune_i_rune\t0I\n"
    "LINE\tS\t\xC3\xBF" "c4Boar\n"
    "LINE\tS\t\xC3\xBF" "c3Adds 10-20 Damage\n"
    "BLOCK\t1\tPlain Sword\t\t\n"
    "LINE\tT\tNo special properties\n"
    "ENTRY\tKanji Runes\t46 runes\n"
    "BLOCK\t1\tIst\t\n"
    "LINE\tT\tA rune\n";

void TestParsesSample() {
    esrwiki::Database db;
    std::string error;
    Check(esrwiki::ParseDatabase(kSample, db, error), "sample parses");
    Check(db.pages.size() == 2, "two pages");
    Check(db.pages[0].entries.size() == 1, "guide has one entry");
    Check(db.pages[0].entries[0].blocks.size() == 1, "guide section gets an implicit block");
    Check(!db.pages[0].entries[0].blocks[0].item, "guide section is not an item");
    Check(db.pages[1].entries.size() == 2, "items page has two entries");
    Check(db.entryCount() == 3, "entryCount counts entries across pages");
    Check(db.blockCount() == 4, "blockCount counts blocks across pages");

    const esrwiki::Entry& weapons = db.pages[1].entries[0];
    Check(weapons.title == "Weapons", "entry title");
    Check(weapons.subtitle == "2 items", "entry subtitle");
    Check(weapons.blocks.size() == 2, "two item blocks");
    const esrwiki::Block& boar = weapons.blocks[0];
    Check(boar.item, "BLOCK 1 is an item");
    Check(boar.title == "Boar", "block title");
    Check(boar.fields.size() == 2, "two fields");
    Check(boar.fields[1].label == "Base types", "field label");
    Check(boar.fields[1].value == "Weapon, Bow Quiver", "field value keeps commas");
    Check(boar.images.size() == 1, "one image");
    Check(boar.runes.size() == 1 && boar.runes[0].label == "0I", "rune icon");
    Check(boar.lines.size() == 2 && boar.lines[0].kind == esrwiki::LineKind::Stat, "stat lines");
}

void TestRejectsBadInput() {
    esrwiki::Database db;
    std::string error;
    Check(!esrwiki::ParseDatabase("#ESRWIKI\t1\n", db, error), "old format version rejected");
    Check(!esrwiki::ParseDatabase("PAGE\ta\tA\tG\n", db, error), "missing header rejected");
    Check(!esrwiki::ParseDatabase("#ESRWIKI\t2\nENTRY\tX\tY\n", db, error), "entry before page rejected");
    Check(!esrwiki::ParseDatabase("#ESRWIKI\t2\nPAGE\ta\tA\tG\nENTRY\tX\tY\nLINE\tQ\tbad\n", db, error),
          "bad line kind rejected");
    Check(!esrwiki::ParseDatabase("", db, error), "empty input rejected");
}

void TestFilter() {
    esrwiki::Database db;
    std::string error;
    esrwiki::ParseDatabase(kSample, db, error);
    const esrwiki::Page& items = db.pages[1];
    Check(esrwiki::FilterEntries(items, "").size() == 2, "empty query matches all entries");
    Check(esrwiki::FilterEntries(items, "boar").size() == 1, "search finds block title, case-insensitive");
    Check(esrwiki::FilterEntries(items, "bow quiver").size() == 1, "search finds field value");
    Check(esrwiki::FilterEntries(items, "xyzzy").size() == 0, "search with no hits returns none");
    Check(esrwiki::FilterEntries(items, "ist").size() == 1, "search finds rune in kanji entry");
}

void TestPaging() {
    Check(esrwiki::PageCount(0, 10) == 1, "zero rows is one page");
    Check(esrwiki::PageCount(10, 10) == 1, "exact fit is one page");
    Check(esrwiki::PageCount(11, 10) == 2, "overflow adds a page");
    Check(esrwiki::PageCount(5, 0) == 1, "zero per page does not divide by zero");
}

void TestColorCodes() {
    const auto runs = esrwiki::SplitColorCodes("plain \xC3\xBF" "c1red\xC3\xBF" "c;purple");
    Check(runs.size() == 3, "three color runs");
    Check(runs[0].code == '\0' && runs[0].text == "plain ", "default run");
    Check(runs[1].code == '1' && runs[1].text == "red", "red run");
    Check(runs[2].code == ';' && runs[2].text == "purple", "purple run");
}


void TestArrangement() {
    // Two runewords with 2 runes and one with 1, plus a non-runeword set group.
    const char* text =
        "#ESRWIKI\t2\nPAGE\tr\tR\tG\nENTRY\tWeapons\t\n"
        "BLOCK\t1\tAlpha\t\t2 Sockets\nRUNE\ta\tA\nRUNE\tb\tB\n"
        "BLOCK\t1\tBravo\t\t1 Socket\nRUNE\tc\tC\n"
        "BLOCK\t1\tCharlie\t\t2 Sockets\nRUNE\td\tD\nRUNE\te\tE\n"
        "ENTRY\tHelms\t\n"
        "BLOCK\t1\tZeta\t\tSet A\nBLOCK\t1\tEta\t\tSet B\nBLOCK\t1\tTheta\t\tSet A\n";
    esrwiki::Database db;
    std::string error;
    Check(esrwiki::ParseDatabase(text, db, error), "arrangement sample parses");
    const esrwiki::Entry& weapons = db.pages[0].entries[0];
    Check(weapons.blocks[1].section == "1 Socket", "section parsed from BLOCK");
    Check(esrwiki::HasRuneBlocks(weapons), "runeword entry detected");
    Check(!esrwiki::HasRuneBlocks(db.pages[0].entries[1]), "set entry is not runewords");

    const auto counts = esrwiki::SocketCounts(weapons);
    Check(counts.size() == 2 && counts[0] == 1 && counts[1] == 2, "socket counts ascending and distinct");

    // Weapons is not sortable, so blocks keep their section groups in first-appearance order.
    auto defaults = esrwiki::ArrangeBlocks(weapons, 0, "", "", esrwiki::BlockSort::Default);
    Check(defaults.size() == 3 && defaults[0] == 0 && defaults[1] == 2 && defaults[2] == 1,
          "default order keeps each section together");
    auto filtered = esrwiki::ArrangeBlocks(weapons, 2, "", "", esrwiki::BlockSort::Default);
    Check(filtered.size() == 2 && filtered[0] == 0 && filtered[1] == 2, "socket filter keeps matching blocks");
    auto byName = esrwiki::ArrangeBlocks(weapons, 0, "", "", esrwiki::BlockSort::Name);
    Check(byName.size() == 3 && byName[0] == 0 && byName[1] == 1 && byName[2] == 2, "name sort");

    const auto& helms = db.pages[0].entries[1];
    auto sets = esrwiki::ArrangeBlocks(helms, 0, "", "", esrwiki::BlockSort::Default);
    Check(sets.size() == 3 && sets[0] == 0 && sets[1] == 2 && sets[2] == 1,
          "set items keep each section together in first-appearance order");
}

void TestSearchAndCompact() {
    const char* text =
        "#ESRWIKI\t2\nPAGE\tr\tR\tG\tcompact\nENTRY\tWeapons\t\n"
        "BLOCK\t1\tAlpha\tfast\t2 Sockets\nTAG\t(Coupon only)\nFIELD\tBase\tBow\nLINE\tS\tadds fire damage\n"
        "BLOCK\t1\tBravo\t\t1 Socket\nRUNE\ta\tIst\n"
        "BLOCK\t1\tCharlie\t\t2 Sockets\nLINE\tS\tadds cold damage\n";
    esrwiki::Database db;
    std::string error;
    Check(esrwiki::ParseDatabase(text, db, error), "compact sample parses");
    Check(db.pages[0].compact, "PAGE with compact style");
    Check(db.pages[0].entries[0].blocks[0].tag == "(Coupon only)", "TAG parsed into the block");
    Check(esrwiki::ArrangeBlocks(db.pages[0].entries[0], 0, "", "", esrwiki::BlockSort::Name, "coupon").size() == 1,
          "search matches the tag");
    const esrwiki::Entry& entry = db.pages[0].entries[0];
    Check(esrwiki::ArrangeBlocks(entry, 0, "", "", esrwiki::BlockSort::Name, "fire").size() == 1, "search matches stat lines");
    Check(esrwiki::ArrangeBlocks(entry, 0, "", "", esrwiki::BlockSort::Name, "bow").size() == 1, "search matches fields");
    Check(esrwiki::ArrangeBlocks(entry, 0, "", "", esrwiki::BlockSort::Name, "ist").size() == 1, "search matches rune labels");
    Check(esrwiki::ArrangeBlocks(entry, 0, "", "", esrwiki::BlockSort::Name, "fast").size() == 1, "search matches subtitle");
    Check(esrwiki::ArrangeBlocks(entry, 0, "", "", esrwiki::BlockSort::Name, "damage").size() == 2, "search matches several blocks");
    Check(esrwiki::ArrangeBlocks(entry, 0, "", "", esrwiki::BlockSort::Name, "zzz").empty(), "search with no match is empty");
    Check(esrwiki::ArrangeBlocks(entry, 0, "", "", esrwiki::BlockSort::Name, "weapons").size() == 3, "entry title match keeps all blocks");
    Check(esrwiki::BlockMatches(entry.blocks[0], "ADDS"), "BlockMatches is case-insensitive");
}

void TestGeneratedData(const std::string& path) {
    const std::string text = ReadFile(path);
    if (text.empty()) {
        std::printf("skip: %s not found\n", path.c_str());
        return;
    }
    esrwiki::Database db;
    std::string error;
    Check(esrwiki::ParseDatabase(text, db, error), ("generated data parses: " + error).c_str());
    Check(db.pages.size() == 17, "17 pages");
    auto find = [&](const char* id) -> const esrwiki::Page* {
        for (const auto& page : db.pages)
            if (page.id == id) return &page;
        return nullptr;
    };
    struct Expect { const char* id; std::size_t entries; std::size_t blocks; };
    const Expect expected[] = {
        {"runes", 4, 95},      {"runewords", 8, 474}, {"set-items", 10, 346},
        {"uniques", 13, 1106}, {"gemwords", 8, 1408},
    };
    for (const Expect& e : expected) {
        const esrwiki::Page* page = find(e.id);
        Check(page != nullptr, e.id);
        if (page == nullptr) continue;
        std::size_t blocks = 0;
        for (const auto& entry : page->entries) blocks += entry.blocks.size();
        Check(page->entries.size() == e.entries, (std::string(e.id) + " entry count").c_str());
        Check(blocks == e.blocks, (std::string(e.id) + " block count").c_str());
    }
    {
        auto text_of = [](const std::vector<esrwiki::Line>& lines) {
            std::string joined;
            for (const auto& line : lines) joined += line.text + "|";
            return joined;
        };
        // Ko exists in both rune sets: the LoD copy gives Dexterity, the Kanji one Strength.
        const std::string lod = text_of(esrwiki::RuneBonusLines(db, "Ko", true, "Weapons"));
        const std::string kanji = text_of(esrwiki::RuneBonusLines(db, "Ko", false, "Weapons"));
        Check(lod.find("Dexterity") != std::string::npos, "LoD Ko bonus lines found");
        Check(kanji.find("Strength") != std::string::npos, "Kanji Ko bonus lines found");
        Check(esrwiki::RuneBonusLines(db, "Nonexistent", false, "Weapons").empty(), "unknown rune has no lines");
        // The website's labels ("Weapons / Gloves", "Helms / Boots / Armor / Shields / Belts") pick the lines by item kind.
        Check(text_of(esrwiki::RuneBonusLines(db, "I", false, "Gloves")).find("Enhanced Damage") != std::string::npos, "I rune on gloves");
        Check(text_of(esrwiki::RuneBonusLines(db, "I", false, "Torso")).find("+30 Defense") != std::string::npos, "I rune on armor");
        Check(text_of(esrwiki::RuneBonusLines(db, "I", false, "Torso")).find("Enhanced Damage") == std::string::npos, "I rune on armor has no weapon line");
        Check(esrwiki::RuneBonusLines(db, "I", false, "Charms").empty(), "I rune on a charm has no lines");
        Check(text_of(esrwiki::RuneBonusLines(db, "I", false, "Gloves")).find("Gloves:") == std::string::npos, "no section labels");
    }
    const esrwiki::Page* ascendancy = find("ascendancy");
    if (ascendancy != nullptr) {
        int left = 0, flipped = 0;
        for (const auto& entry : ascendancy->entries) {
            if (entry.splitLayout == 1) ++left;
            if (entry.splitLayout == 2) ++flipped;
        }
        Check(left + flipped == 15 && left > 0 && flipped > 0, "the 15 ascendancies use the split layout, on alternating sides");
    }
    const esrwiki::Page* runewords = find("runewords");
    if (runewords != nullptr) Check(esrwiki::FilterEntries(*runewords, "").size() == 8, "runewords has 8 categories");
    const esrwiki::Page* runes = find("runes");
    if (runes != nullptr) {
        Check(runes->entries.size() >= 3 && runes->entries[0].title == "Kanji Runes", "runes start with Kanji");
        Check(runes->entries[0].blocks.size() == 47, "47 Kanji runes");
        Check(runes->entries[1].blocks.size() == 33, "33 LoD runes");
        Check(runes->entries[2].blocks.size() == 14, "14 elemental runes");
        Check(runes->entries[0].blocks[0].title.find("\xC3\xBF") == std::string::npos, "titles have no color codes");
    }
}

void TestRowSort() {
    const char us = '\x1f';
    const auto row = [&](const std::string& name, const char* min, const char* max, const char* rarity) {
        return esrwiki::Line{esrwiki::LineKind::Row, "\xC3\xBF" "c3" + name + us + min + us + max + us + rarity};
    };
    std::vector<esrwiki::Line> lines;
    lines.push_back({esrwiki::LineKind::Heading, "Title"});
    lines.push_back({esrwiki::LineKind::Header, std::string("Name") + us + "Min iLvl" + us + "Max iLvl" + us + "Rarity"});
    lines.push_back(row("A", "10", "50", "7"));
    lines.push_back(row("B", "2", "", "30"));
    lines.push_back(row("C", "10", "20", "7"));
    lines.push_back(row("D", "5", "80", "1"));
    const auto names = [](const std::vector<esrwiki::Line>& sorted) {
        std::string out;
        for (const auto& line : sorted) if (line.kind == esrwiki::LineKind::Row) out += line.text[4];
        return out;
    };
    Check(names(esrwiki::SortTableRows(lines, esrwiki::BlockSort::Default)) == "ABCD", "default keeps the order");
    Check(names(esrwiki::SortTableRows(lines, esrwiki::BlockSort::RowMinIlvl)) == "BDAC", "min iLvl lowest first, ties keep order");
    Check(names(esrwiki::SortTableRows(lines, esrwiki::BlockSort::RowMaxIlvl)) == "BDAC", "max iLvl highest first, no maximum counts as highest");
    Check(names(esrwiki::SortTableRows(lines, esrwiki::BlockSort::RowRarity)) == "DACB", "rarity lowest first");
    Check(esrwiki::SortTableRows(lines, esrwiki::BlockSort::RowMinIlvl)[0].kind == esrwiki::LineKind::Heading, "headings stay put");
}

}  // namespace

int main(int argc, char** argv) {
    TestParsesSample();
    TestRejectsBadInput();
    TestFilter();
    TestPaging();
    TestColorCodes();
    TestArrangement();
    TestSearchAndCompact();
    TestRowSort();
    TestGeneratedData(argc > 1 ? argv[1] : "../data/wiki-data.txt");
    if (failures == 0) std::printf("wiki_model_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
