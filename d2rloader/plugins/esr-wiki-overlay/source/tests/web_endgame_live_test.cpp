// Checks that the C++ website-endgame builder produces exactly what tools/web_endgame.py writes.
//   web_endgame_live_test <endgame_maps.htm> <wiki-data.txt with the website page>
#include <fstream>
#include <iostream>
#include <sstream>

#include "web_endgame_live.hpp"

static std::string ReadAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

static std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: web_endgame_live_test endgame_maps.htm wiki-data.txt\n"; return 2; }
    const std::string page = ReadAll(argv[1]), expected = ReadAll(argv[2]);
    std::string out, error;
    CHECK(esrwiki::BuildWebEndgameWikiData(expected, page, out, error), error);
    if (out != expected) { std::ofstream("web_endgame_actual.txt", std::ios::binary) << out; std::cerr << "FAIL: output differs (web_endgame_actual.txt)\n"; return 1; }
    const std::string junk = "<html><body><table><tr><td>x</td></tr></table></body></html>";
    CHECK(!esrwiki::BuildWebEndgameWikiData(expected, junk, out, error), "a broken page was accepted");
    CHECK(!esrwiki::BuildWebEndgameWikiData(expected, page.substr(0, page.size() / 2), out, error), "a cut-off page was accepted");
    CHECK(!esrwiki::BuildWebEndgameWikiData(expected, Replace(page, "map-bosses/lucionwhisper.png", "maps/lucionwhisper.png"), out, error),
          "a page with a missing boss was accepted");
    CHECK(!esrwiki::BuildWebEndgameWikiData(Replace(expected, "PAGE\tendgame-maps\t", "PAGE\tother\t"), page, out, error), "data without the page was accepted");
    std::cout << "web_endgame_live_test: all checks passed\n";
    return 0;
}
