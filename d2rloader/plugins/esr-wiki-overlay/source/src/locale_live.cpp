#include "locale_live.hpp"

#include "cube_live.hpp"
#include "locale_align.hpp"
#include "web_affixes_live.hpp"
#include "web_ascendancy_live.hpp"
#include "web_bases_live.hpp"
#include "web_class_live.hpp"
#include "web_endgame_live.hpp"
#include "web_kill_live.hpp"
#include "web_mastery_live.hpp"
#include "web_runes_live.hpp"
#include "web_sets_live.hpp"
#include "web_skills_live.hpp"
#include "web_special_live.hpp"
#include "web_uniques_live.hpp"
#include "web_vessel_live.hpp"
#include "web_words_live.hpp"

#include <cstring>
#include <functional>

namespace esrwiki {
namespace {

// The text of one page of wiki data: from its PAGE line up to the next one.
bool FindPage(const std::string& data, const std::string& id, std::size_t& begin, std::size_t& end) {
    const std::string marker = "PAGE\t" + id + "\t";
    std::size_t at = 0;
    for (;;) {
        at = data.find(marker, at);
        if (at == std::string::npos) return false;
        if (at == 0 || data[at - 1] == '\n') break;
        ++at;
    }
    begin = at;
    const std::size_t next = data.find("\nPAGE\t", at);
    end = next == std::string::npos ? data.size() : next + 1;
    return true;
}

// The lines of a page after its PAGE line.
std::string PageBody(const std::string& data, std::size_t begin, std::size_t end) {
    const std::size_t lineEnd = data.find('\n', begin);
    if (lineEnd == std::string::npos || lineEnd >= end) return std::string();
    return data.substr(lineEnd + 1, end - lineEnd - 1);
}

std::string Page(const std::vector<std::string>& pages, const char* name) {
    for (std::size_t i = 0; i < kLocalizedPageCount; ++i)
        if (std::strcmp(kLocalizedPageNames[i], name) == 0) return pages[i];
    return std::string();
}

struct Step {
    const char* pageId;
    std::function<bool(const std::string& current, std::string& next, std::string& error)> build;
};

}  // namespace

bool BuildLocalizedWikiData(const LocalizedBuildInput& in, std::string& out, std::string& report, std::string& error,
                            std::string* cubeOut) {
    if (in.englishPages.size() != kLocalizedPageCount || in.localizedPages.size() != kLocalizedPageCount) {
        error = "the set of website pages is incomplete";
        return false;
    }
    for (std::size_t i = 0; i < kLocalizedPageCount; ++i)
        if (in.englishPages[i].empty() || in.localizedPages[i].empty()) {
            error = std::string("the page ") + kLocalizedPageNames[i] + " is missing";
            return false;
        }

    localize::Translator translator;
    for (std::size_t i = 0; i < kLocalizedPageCount; ++i) translator.AddPage(in.englishPages[i], in.localizedPages[i]);
    {   // the manual translations of overlay-authored labels fill in only what no website page translated
        std::size_t from = 0;
        const std::string& ui = in.uiStrings;
        while (from < ui.size()) {
            std::size_t to = ui.find('\n', from);
            if (to == std::string::npos) to = ui.size();
            std::string line = ui.substr(from, to - from);
            from = to + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t tab = line.find('\t');
            if (line.empty() || line[0] == '#' || tab == std::string::npos) continue;
            translator.AddFallback(line.substr(0, tab), line.substr(tab + 1));
        }
    }
    translator.Finish();

    const auto& en = in.englishPages;
    const auto P = [&](const char* name) { return Page(en, name); };
    std::vector<std::string> uniquePages;
    for (const char* name : kWebUniquePageNames) uniquePages.push_back(P(name));

    const std::vector<Step> steps = {
        {"uniques", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebUniquesWikiData(c, uniquePages, in.uniqueArtTable, n, e); }},
        {"runes", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebRunesWikiData(c, P("gems.htm"), P("Eastern%20Sun%20Resurrected%20Cube%20Recipes.html"), n, e); }},
        {"runewords", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebRunewordsWikiData(c, P("runewords.htm"), P("gems.htm"), in.wordArtTable, n, e); }},
        {"gemwords", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebGemwordsWikiData(c, P("gemwords.htm"), in.wordArtTable, n, e); }},
        {"item-bases", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebBasesWikiData(c, P("armors.htm"), P("weapons.htm"), in.baseArtTable, n, e); }},
        {"item-affixes", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebAffixesWikiData(c, P("prefixes.htm"), P("suffixes.htm"), P("quality_affixes.htm"), n, e); }},
        {"ascendancy", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebAscendancyWikiData(c, P("ascendancies.htm"), n, e); }},
        {"class-crafting", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebClassCraftingWikiData(c, P("Eastern%20Sun%20Resurrected%20Cube%20Recipes.html"), n, e); }},
        {"set-items", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebSetsWikiData(c, P("sets.htm"), n, e); }},
        {"endgame-maps", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebEndgameWikiData(c, P("endgame_maps.htm"), n, e); }},
        {"corruption", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebSpecialWikiData(c, P("special_crafting_outcomes.htm"), n, e); }},
        {"merc-oskill", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebSkillsWikiData(c, P("skill_information.htm"), n, e); }},
        {"vessel-of-souls", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebVesselWikiData(c, P("vessel_of_souls.htm"), n, e); }},
        {"kill-ledger", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebKillWikiData(c, P("kill_ledger.htm"), n, e); }},
        {"weapon-mastery", [&](const std::string& c, std::string& n, std::string& e) { return BuildWebMasteryWikiData(c, P("weapon_mastery.html"), n, e); }},
    };

    std::string english = in.shippedEnglish;  // the English builders chain through this, as they do on the live English wiki
    out = in.shippedLocalized;
    std::size_t rebuilt = 0;
    for (const Step& step : steps) {
        std::string next, why;
        if (!step.build(english, next, why)) {
            report += std::string(step.pageId) + ": kept the shipped page (" + why + ")\n";
            continue;
        }
        english = std::move(next);
        std::size_t eb = 0, ee = 0, lb = 0, le = 0;
        if (!FindPage(english, step.pageId, eb, ee) || !FindPage(out, step.pageId, lb, le)) {
            report += std::string(step.pageId) + ": page not found, kept the shipped page\n";
            continue;
        }
        // The shipped PAGE line keeps its localized title; the lines after it come from the website.
        const std::size_t headEnd = out.find('\n', lb);
        const std::string head = headEnd == std::string::npos || headEnd >= le ? out.substr(lb, le - lb) : out.substr(lb, headEnd + 1 - lb);
        const std::string body = translator.Translate(PageBody(english, eb, ee));
        out.replace(lb, le - lb, head + body);
        report += std::string(step.pageId) + ": rebuilt from the website\n";
        ++rebuilt;
    }
    if (cubeOut != nullptr) {
        cubeOut->clear();
        std::string cubeEnglish, why;
        if (in.shippedCubeLocalized.empty()) {
            report += "cube recipes: no shipped localized file, not rebuilt\n";
        } else if (!BuildCubeWikiData(P("Eastern%20Sun%20Resurrected%20Cube%20Recipes.html"), cubeEnglish, why)) {
            report += "cube recipes: kept the shipped file (" + why + ")\n";
        } else {
            // Page by page, as above: the shipped PAGE line (its localized tab name), then the translated body.
            std::string cube = in.shippedCubeLocalized;
            bool all = true;
            std::size_t at = 0;
            while (at < cubeEnglish.size()) {
                const std::size_t pageAt = cubeEnglish.find("PAGE\t", at);
                if (pageAt == std::string::npos) break;
                const std::size_t idStart = pageAt + 5, idEnd = cubeEnglish.find('\t', idStart);
                const std::string id = cubeEnglish.substr(idStart, idEnd - idStart);
                std::size_t eb = 0, ee = 0, lb = 0, le = 0;
                if (!FindPage(cubeEnglish, id, eb, ee) || !FindPage(cube, id, lb, le)) { all = false; break; }
                const std::size_t headEnd = cube.find('\n', lb);
                const std::string head = headEnd == std::string::npos || headEnd >= le ? cube.substr(lb, le - lb) : cube.substr(lb, headEnd + 1 - lb);
                cube.replace(lb, le - lb, head + translator.Translate(PageBody(cubeEnglish, eb, ee)));
                at = ee;
            }
            if (all) {
                *cubeOut = cube;
                report += "cube recipes: rebuilt from the website\n";
            } else {
                report += "cube recipes: page not found, kept the shipped file\n";
            }
        }
    }
    if (rebuilt == 0) {
        error = "no page could be rebuilt from the website";
        return false;
    }
    return true;
}

}  // namespace esrwiki
