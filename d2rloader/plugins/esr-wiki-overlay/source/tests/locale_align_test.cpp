// Checks locale_align: text nodes, alignment, translation of wiki data. With a data folder as argument it also
// translates the shipped English wiki data from the saved French pages and checks the amount translated.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "locale_align.hpp"

using namespace esrwiki::localize;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static std::string Read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main(int argc, char** argv) {
    // text nodes: entities, white space, script and style
    {
        const auto nodes = ExtractTextNodes("<html><style>p{}</style><body><p class=\"a b\"> Hello&nbsp; &amp; <b>wor&#108;d</b></p><script>var x='<p>no</p>';</script><br/>1 &lt; 2</body></html>");
        CHECK(nodes.size() == 3);
        CHECK(nodes.size() == 3 && nodes[0].text == "Hello &" && nodes[1].text == "world" && nodes[2].text == "1 < 2");
    }
    {
        const auto nodes = ExtractTextNodes("<p>  a \n b  </p><p>x</p>");
        CHECK(nodes.size() == 2 && nodes[0].text == "a b" && nodes[1].text == "x");
    }
    // equal counts pair by position
    {
        const auto pairs = AlignNodes(ExtractTextNodes("<p>One</p><p>Two</p>"), ExtractTextNodes("<p>Un</p><p>Deux</p>"));
        CHECK(pairs.size() == 2 && pairs[0].second == "Un" && pairs[1].second == "Deux");
    }
    // an extra node in the localized page: pairing resumes after it
    {
        std::string en, fr;
        for (int i = 0; i < 20; ++i) { en += "<td class=\"c\">Word " + std::to_string(i) + "</td>"; }
        for (int i = 0; i < 20; ++i) {
            if (i == 8) fr += "<td class=\"extra\">Plus</td>";
            fr += "<td class=\"c\">Mot " + std::to_string(i) + "</td>";
        }
        const auto pairs = AlignNodes(ExtractTextNodes(en), ExtractTextNodes(fr));
        CHECK(pairs.size() == 20);
        CHECK(!pairs.empty() && pairs.back().first == "Word 19" && pairs.back().second == "Mot 19");
    }
    // translating wiki data: exact, numbers, pieces, colour codes, the tier column stays
    {
        Translator t;
        t.AddPage("<p>Fire Resist</p><p>+(5-10)% Enhanced Damage</p><p>Helm</p><p>Gloves</p><p>Normal</p>",
                  "<p>Resistance au feu</p><p>+(5-10)% Degats ameliores</p><p>Casque</p><p>Gants</p><p>Normal</p>");
        t.Finish();
        const std::string data = "ENTRY\tFire Resist\tHelm\nBLOCK\t1\tHelm\t\t\tNormal\tGloves\t0\nLINE\tS\t\xC3\xBF" "c3+(20-30)% Enhanced Damage\nLINE\tS\tHelm, Gloves\nLINE\tS\tNothing here\n";
        TranslateStats stats;
        const std::string out = t.Translate(data, &stats);
        CHECK(out.find("ENTRY\tResistance au feu\tCasque") == 0);
        CHECK(out.find("+(20-30)% Degats ameliores") != std::string::npos);
        CHECK(out.find("\xC3\xBF" "c3+(20-30)% Degats") != std::string::npos);
        CHECK(out.find("Casque, Gants") != std::string::npos);
        CHECK(out.find("Nothing here") != std::string::npos);  // not on the website: stays as it is
        CHECK(out.find("\tNormal\tGants\t") != std::string::npos);
    }
    // saved copies: the English data translated from the French pages
    if (argc > 1) {
        const std::string data = argv[1];
        Translator t;
        for (const char* page : {"armors.htm", "gems.htm", "prefixes.htm", "suffixes.htm", "unique_armors.htm", "weapons.htm"}) {
            std::string en;
            for (const char* dir : {"website-bases", "website-runes", "website-affixes", "website-uniques"}) {
                en = Read(data + "/" + dir + "/" + page);
                if (!en.empty()) break;
            }
            const std::string fr = Read(data + "/locale-pages/frFR/" + page);
            CHECK(!en.empty() && !fr.empty());
            t.AddPage(en, fr);
        }
        t.Finish();
        CHECK(t.Size() > 5000);
        TranslateStats stats;
        t.Translate(Read(data + "/wiki-data.txt"), &stats);
        std::printf("%zu of %zu cells translated\n", stats.translated, stats.cellsWithWords);
        CHECK(stats.translated > 50000);
    }
    if (failures == 0) std::printf("locale_align_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
