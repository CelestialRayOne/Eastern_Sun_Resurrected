// Checks that the C++ website-runes builder produces exactly what tools/web_runes.py writes.
//   web_runes_live_test <gems.htm> <cube recipes page> <wiki-data.txt with the website page>
#include <fstream>
#include <iostream>
#include <sstream>

#include "web_runes_live.hpp"

static std::string ReadAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

// The wiki data without the page whose id is `id`.
static std::string WithoutPage(const std::string& text, const std::string& id) {
    const std::string marker = "PAGE\t" + id + "\t";
    const std::size_t begin = text.find("\n" + marker);
    if (begin == std::string::npos) return text;
    const std::size_t end = text.find("\nPAGE\t", begin + 1);
    return text.substr(0, begin) + (end == std::string::npos ? std::string() : text.substr(end));
}

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "usage: web_runes_live_test gems.htm cube-recipes-page.html wiki-data.txt\n"; return 2; }
    const std::string gems = ReadAll(argv[1]);
    const std::string cube = ReadAll(argv[2]);
    const std::string expected = ReadAll(argv[3]);
    std::string out, error;
    // Feeding it the finished data checks the replace step too: the Runes page is rebuilt in place.
    if (!esrwiki::BuildWebRunesWikiData(expected, gems, cube, out, error)) { std::cerr << "FAIL: " << error << "\n"; return 1; }
    if (out != expected) {
        std::ofstream("web_runes_actual.txt", std::ios::binary) << out;
        std::cerr << "FAIL: output differs (written to web_runes_actual.txt)\n";
        return 1;
    }
    // Data without a Runes page gets one: the same text comes back when the page is put after "ascendancy" again.
    const std::string without = WithoutPage(expected, "runes");
    if (without == expected) { std::cerr << "FAIL: test setup: the page was not removed\n"; return 1; }
    if (!esrwiki::BuildWebRunesWikiData(without, gems, cube, out, error)) { std::cerr << "FAIL (insert): " << error << "\n"; return 1; }
    if (out != expected) { std::cerr << "FAIL: the inserted page is not in the place of the original\n"; return 1; }
    // A broken page must be refused, not turned into an empty rune list.
    if (esrwiki::BuildWebRunesWikiData(expected, "<html><body><table><tr><td>x</td></tr></table></body></html>", cube, out, error)) {
        std::cerr << "FAIL: a broken page was accepted\n"; return 1;
    }
    if (esrwiki::BuildWebRunesWikiData(expected, gems.substr(0, gems.size() / 2), cube, out, error)) {
        std::cerr << "FAIL: a cut-off page was accepted\n"; return 1;
    }
    // A cut-off or wrong cube page is refused too, and the note must hold the recipes of the cube page.
    if (esrwiki::BuildWebRunesWikiData(expected, gems, cube.substr(0, cube.size() / 2), out, error)) {
        std::cerr << "FAIL: a cut-off cube page was accepted\n"; return 1;
    }
    if (esrwiki::BuildWebRunesWikiData(expected, gems, "<html><body>nothing</body></html>", out, error)) {
        std::cerr << "FAIL: a wrong cube page was accepted\n"; return 1;
    }
    std::cout << "web_runes_live_test: all checks passed\n";
    return 0;
}
