// Checks that the C++ website-affixes builder produces exactly what tools/web_affixes.py writes.
//   web_affixes_live_test <prefixes.htm> <suffixes.htm> <quality_affixes.htm> <wiki-data.txt with the website page>
#include <fstream>
#include <iostream>
#include <sstream>

#include "web_affixes_live.hpp"

static std::string ReadAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

static std::string WithoutPage(const std::string& text, const std::string& id) {
    const std::string marker = "PAGE\t" + id + "\t";
    const std::size_t begin = text.find("\n" + marker);
    if (begin == std::string::npos) return text;
    const std::size_t end = text.find("\nPAGE\t", begin + 1);
    return text.substr(0, begin) + (end == std::string::npos ? std::string() : text.substr(end));
}

#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc != 5) { std::cerr << "usage: web_affixes_live_test prefixes.htm suffixes.htm quality_affixes.htm wiki-data.txt\n"; return 2; }
    const std::string prefixes = ReadAll(argv[1]), suffixes = ReadAll(argv[2]), quality = ReadAll(argv[3]), expected = ReadAll(argv[4]);
    std::string out, error;
    CHECK(esrwiki::BuildWebAffixesWikiData(expected, prefixes, suffixes, quality, out, error), error);
    if (out != expected) { std::ofstream("web_affixes_actual.txt", std::ios::binary) << out; std::cerr << "FAIL: output differs (web_affixes_actual.txt)\n"; return 1; }
    const std::string without = WithoutPage(expected, "item-affixes");
    CHECK(without != expected, "test setup: the page was not removed");
    CHECK(esrwiki::BuildWebAffixesWikiData(without, prefixes, suffixes, quality, out, error), "insert: " << error);
    CHECK(out == expected, "the inserted page is not in the place of the original");
    const std::string junk = "<html><body><table><tr><td>x</td></tr></table></body></html>";
    CHECK(!esrwiki::BuildWebAffixesWikiData(expected, junk, suffixes, quality, out, error), "broken prefixes accepted");
    CHECK(!esrwiki::BuildWebAffixesWikiData(expected, prefixes, junk, quality, out, error), "broken suffixes accepted");
    CHECK(!esrwiki::BuildWebAffixesWikiData(expected, prefixes, suffixes, junk, out, error), "broken quality accepted");
    CHECK(!esrwiki::BuildWebAffixesWikiData(expected, prefixes.substr(0, prefixes.size() / 2), suffixes, quality, out, error), "cut-off prefixes accepted");
    CHECK(!esrwiki::BuildWebAffixesWikiData(expected, prefixes, suffixes.substr(0, suffixes.size() / 2), quality, out, error), "cut-off suffixes accepted");
    std::cout << "web_affixes_live_test: all checks passed\n";
    return 0;
}
