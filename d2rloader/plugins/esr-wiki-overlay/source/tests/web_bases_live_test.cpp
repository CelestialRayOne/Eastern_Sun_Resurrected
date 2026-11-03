// Checks that the C++ website-bases builder produces exactly what tools/web_bases.py writes.
//   web_bases_live_test <armors.htm> <weapons.htm> <web-base-art.txt> <wiki-data.txt with the website page>
#include <fstream>
#include <iostream>
#include <sstream>

#include "web_bases_live.hpp"

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
    if (argc != 5) { std::cerr << "usage: web_bases_live_test armors.htm weapons.htm web-base-art.txt wiki-data.txt\n"; return 2; }
    const std::string armors = ReadAll(argv[1]), weapons = ReadAll(argv[2]), art = ReadAll(argv[3]), expected = ReadAll(argv[4]);
    std::string out, error;
    CHECK(esrwiki::BuildWebBasesWikiData(expected, armors, weapons, art, out, error), error);
    if (out != expected) { std::ofstream("web_bases_actual.txt", std::ios::binary) << out; std::cerr << "FAIL: output differs (web_bases_actual.txt)\n"; return 1; }
    const std::string without = WithoutPage(expected, "item-bases");
    CHECK(without != expected, "test setup: the page was not removed");
    CHECK(esrwiki::BuildWebBasesWikiData(without, armors, weapons, art, out, error), "insert: " << error);
    CHECK(out == expected, "the inserted page is not in the place of the original");
    const std::string junk = "<html><body><table><tr><td>x</td></tr></table></body></html>";
    CHECK(!esrwiki::BuildWebBasesWikiData(expected, junk, weapons, art, out, error), "broken armors accepted");
    CHECK(!esrwiki::BuildWebBasesWikiData(expected, armors, junk, art, out, error), "broken weapons accepted");
    CHECK(!esrwiki::BuildWebBasesWikiData(expected, armors.substr(0, armors.size() / 2), weapons, art, out, error), "cut-off armors accepted");
    CHECK(!esrwiki::BuildWebBasesWikiData(expected, armors, weapons.substr(0, weapons.size() / 2), art, out, error), "cut-off weapons accepted");
    std::cout << "web_bases_live_test: all checks passed\n";
    return 0;
}
