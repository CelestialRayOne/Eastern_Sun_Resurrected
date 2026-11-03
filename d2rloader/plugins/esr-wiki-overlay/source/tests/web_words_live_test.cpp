// Checks that the C++ website-words builders produce exactly what tools/web_words.py writes.
//   web_words_live_test <runewords.htm> <gemwords.htm> <gems.htm> <web-word-art.txt> <wiki-data.txt with both website pages>
#include <fstream>
#include <iostream>
#include <sstream>

#include "web_words_live.hpp"

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
    if (argc != 6) { std::cerr << "usage: web_words_live_test runewords.htm gemwords.htm gems.htm web-word-art.txt wiki-data.txt\n"; return 2; }
    const std::string rw = ReadAll(argv[1]), gw = ReadAll(argv[2]), gems = ReadAll(argv[3]), art = ReadAll(argv[4]);
    const std::string expected = ReadAll(argv[5]);
    std::string out, error;
    CHECK(esrwiki::BuildWebRunewordsWikiData(expected, rw, gems, art, out, error), "runewords: " << error);
    if (out != expected) { std::ofstream("web_runewords_actual.txt", std::ios::binary) << out; std::cerr << "FAIL: runewords differ (web_runewords_actual.txt)\n"; return 1; }
    CHECK(esrwiki::BuildWebGemwordsWikiData(expected, gw, art, out, error), "gemwords: " << error);
    if (out != expected) { std::ofstream("web_gemwords_actual.txt", std::ios::binary) << out; std::cerr << "FAIL: gemwords differ (web_gemwords_actual.txt)\n"; return 1; }
    // Insert paths: data without the page gets it back in the same place.
    std::string without = WithoutPage(expected, "gemwords");
    CHECK(without != expected, "test setup: gemwords not removed");
    CHECK(esrwiki::BuildWebGemwordsWikiData(without, gw, art, out, error), "gemwords insert: " << error);
    CHECK(out == expected, "inserted gemwords page is not in the original place");
    without = WithoutPage(WithoutPage(expected, "gemwords"), "runewords");
    CHECK(esrwiki::BuildWebRunewordsWikiData(without, rw, gems, art, out, error), "runewords insert: " << error);
    std::string step;
    CHECK(esrwiki::BuildWebGemwordsWikiData(out, gw, art, step, error), "gemwords after runewords: " << error);
    CHECK(step == expected, "inserted pages are not in the place of the originals");
    // Broken, cut-off and wrong pages are refused.
    const std::string junk = "<html><body><table><tr><td>x</td></tr></table></body></html>";
    CHECK(!esrwiki::BuildWebRunewordsWikiData(expected, junk, gems, art, out, error), "broken runewords accepted");
    CHECK(!esrwiki::BuildWebRunewordsWikiData(expected, rw.substr(0, rw.size() / 2), gems, art, out, error), "cut-off runewords accepted");
    CHECK(!esrwiki::BuildWebRunewordsWikiData(expected, rw, junk, art, out, error), "broken gems page accepted");
    CHECK(!esrwiki::BuildWebGemwordsWikiData(expected, junk, art, out, error), "broken gemwords accepted");
    CHECK(!esrwiki::BuildWebGemwordsWikiData(expected, gw.substr(0, gw.size() / 2), art, out, error), "cut-off gemwords accepted");
    std::cout << "web_words_live_test: all checks passed\n";
    return 0;
}
