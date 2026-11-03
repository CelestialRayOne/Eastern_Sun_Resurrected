// Checks that the C++ website-uniques builder produces exactly what tools/web_uniques.py writes.
//   web_uniques_live_test <folder with the four website pages> <wiki-data.txt with the website page> <web-unique-art.txt>
#include <fstream>
#include <iostream>
#include <sstream>

#include "web_uniques_live.hpp"

static std::string ReadAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "usage: web_uniques_live_test pagesFolder wiki-data.txt web-unique-art.txt\n"; return 2; }
    std::vector<std::string> pages;
    for (const char* name : esrwiki::kWebUniquePageNames) pages.push_back(ReadAll(std::string(argv[1]) + "/" + name));
    const std::string expected = ReadAll(argv[2]);
    const std::string art = ReadAll(argv[3]);
    std::string out, error;
    // Feeding it the finished data checks the replace step too: the website page is rebuilt in place.
    if (!esrwiki::BuildWebUniquesWikiData(expected, pages, art, out, error)) { std::cerr << "FAIL: " << error << "\n"; return 1; }
    if (out != expected) {
        std::ofstream("web_uniques_actual.txt", std::ios::binary) << out;
        std::cerr << "FAIL: output differs (written to web_uniques_actual.txt)\n";
        return 1;
    }
    // A broken page must be refused, not turned into an empty card list.
    std::vector<std::string> broken = pages;
    broken[1] = "<html><body><table><tr><td>x</td></tr></table></body></html>";
    if (esrwiki::BuildWebUniquesWikiData(expected, broken, art, out, error)) { std::cerr << "FAIL: a broken page was accepted\n"; return 1; }
    broken = pages;
    broken[2].resize(broken[2].size() / 2);
    if (esrwiki::BuildWebUniquesWikiData(expected, broken, art, out, error)) { std::cerr << "FAIL: a cut-off page was accepted\n"; return 1; }
    std::cout << "web_uniques_live_test: all checks passed\n";
    return 0;
}
