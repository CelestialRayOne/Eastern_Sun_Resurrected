#include "web_ascendancy_live.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "web_html.hpp"

namespace esrwiki {
namespace {

using namespace web;

const char kOrbTitle[] = "Orb of Creation / Material Locations";
const char kKeyPrefix[] = "d2rloader_esr-wiki_wiki_";
const char kRed[] = "\xc3\xbf" "c1";
const char kBlue[] = "\xc3\xbf" "c3";
const char kGold[] = "\xc3\xbf" "c4";
const char kWhite[] = "\xc3\xbf" "c0";
const char kSeparator[] = "\x1f";
const char kBreak[] = "\x1e";
const char kSpacer[] = "\xe2\x80\x8b";  // U+200B, keeps a blank line
const char kBlockLine[] = "BLOCK\t0\t\t\t\t\t\t0";
const char kDivMarker[] = "<div style=\"display: flex; justify-content: space-around; width: 1025px\">";
const std::size_t kExpectedClasses = 15;

struct Recipe {
    std::vector<std::pair<std::string, bool>> input;  // each line, and whether the website writes it in orange
    std::string output;
};

struct ClassInfo {
    std::string image;
    Recipe recipe;
    std::string headers[6];
    std::vector<std::string> stats[6];
    std::vector<std::string> notes;
    bool tier[6] = {false, false, false, false, false, false};
    bool pictureFirst = false;  // the website shows the picture before the tiers
};

// Text of an HTML fragment: line breaks become spaces, tags are dropped, entities are decoded, spaces collapsed, and a
// space before . , ; : or ) goes.
std::string Plain(const std::string& fragment) {
    std::string noBr;
    for (const std::string& piece : SplitBr(fragment)) {
        if (!noBr.empty() || &piece != &SplitBr(fragment)[0]) {}
        (void)piece;
        break;
    }
    // re.sub(r"<br\s*/?>", " ", ...) then drop tags
    const std::vector<std::string> parts = SplitBr(fragment);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) noBr += ' ';
        noBr += parts[i];
    }
    std::string stripped;
    bool inTag = false;
    for (const char c : noBr) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>' && inTag) { inTag = false; continue; }
        if (!inTag) stripped += c;
    }
    const std::string text = Clean(stripped);
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ' ' && i + 1 < text.size() && std::string(".,;:)").find(text[i + 1]) != std::string::npos) continue;
        out += text[i];
    }
    return out;
}

std::vector<std::string> LinesOf(const std::string& fragment) {
    std::vector<std::string> out;
    for (const std::string& part : SplitBr(fragment)) {
        const std::string text = Plain(part);
        if (!text.empty()) out.push_back(text);
    }
    return out;
}

std::string RedStars(const std::string& text, const char* base) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '*') {
            std::size_t end = i;
            while (end < text.size() && text[end] == '*') ++end;
            out += kRed;
            out += text.substr(i, end - i);
            out += base;
            i = end;
        } else {
            out += text[i++];
        }
    }
    return out;
}

// The inner text of each <tag ...>...</tag>-like span: from the end of `open` (up to its '>') to `close`, in order.
std::vector<std::string> Between(const std::string& html, const std::string& openStart, const std::string& close) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t open = html.find(openStart, at);
        if (open == std::string::npos) break;
        const std::size_t gt = html.find('>', open);
        if (gt == std::string::npos) break;
        const std::size_t end = html.find(close, gt + 1);
        if (end == std::string::npos) break;
        out.push_back(html.substr(gt + 1, end - gt - 1));
        at = end + close.size();
    }
    return out;
}

bool Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

bool Parse(const std::string& html, std::string& intro, std::vector<std::pair<std::string, std::string>>& challenges,
           std::map<std::string, ClassInfo>& classes, std::string& error) {
    if (FindCI(html, "</html") == std::string::npos) return Fail(error, "the saved page is incomplete");
    const std::size_t marker = html.find("are powerful specializations");
    const std::size_t cellOpen = marker == std::string::npos ? std::string::npos : html.rfind("<td", marker);
    const std::size_t introGt = cellOpen == std::string::npos ? std::string::npos : html.find('>', cellOpen);
    const std::size_t introEnd = marker == std::string::npos ? std::string::npos : html.find("</td>", marker);
    if (introGt == std::string::npos || introEnd == std::string::npos) return Fail(error, "the introduction is missing");
    intro = Plain(html.substr(introGt + 1, introEnd - introGt - 1));

    // tier 1: the gems that unlock each ascendancy
    std::map<std::string, Recipe> recipes;
    const std::size_t tier1 = html.find("Tier 1 Challenge"), tier2 = html.find("Tier 2 Challenge");
    if (tier1 == std::string::npos || tier2 == std::string::npos || tier2 < tier1) return Fail(error, "the Tier 1 challenge is missing");
    std::size_t at = tier1;
    for (;;) {
        const std::size_t open = html.find("<tr>", at);
        if (open == std::string::npos || open >= tier2) break;
        const std::size_t close = html.find("</tr>", open);
        if (close == std::string::npos) break;
        at = close + 5;
        const std::string row = html.substr(open + 4, close - open - 4);
        std::vector<std::string> cells;
        std::size_t cursor = 0;
        for (;;) {
            const std::size_t td = row.find("<td", cursor);
            if (td == std::string::npos) break;
            const std::size_t gt = row.find('>', td);
            if (gt == std::string::npos) break;
            const std::size_t end = row.find("</td>", gt + 1);
            if (end == std::string::npos) break;
            cells.push_back(row.substr(gt + 1, end - gt - 1));
            cursor = end + 5;
        }
        if (cells.size() != 2) continue;
        const std::string stone = Plain(cells[1]);
        const std::string suffix = " Ascendancy Stone";
        if (stone.size() > suffix.size() && EndsWith(stone, suffix)) {
            Recipe recipe;
            recipe.output = stone;
            for (const std::string& piece : SplitBr(cells[0])) {
                const std::string text = Plain(piece);
                if (!text.empty()) recipe.input.emplace_back(text, piece.find("color=\"orange\"") != std::string::npos);
            }
            recipes[stone.substr(0, stone.size() - suffix.size())] = std::move(recipe);
        }
    }
    if (recipes.size() != kExpectedClasses) {
        return Fail(error, "expected " + std::to_string(kExpectedClasses) + " ascendancy recipes, found " + std::to_string(recipes.size()));
    }

    // tiers 2 to 5
    for (int number = 2; number <= 5; ++number) {
        const std::size_t start = html.find("Tier " + std::to_string(number) + " Challenge");
        if (start == std::string::npos) return Fail(error, "the Tier " + std::to_string(number) + " challenge is missing");
        const std::size_t td = html.find("<td", start);
        const std::size_t gt = td == std::string::npos ? std::string::npos : html.find('>', td);
        const std::size_t end = gt == std::string::npos ? std::string::npos : html.find("</td>", gt + 1);
        if (end == std::string::npos) return Fail(error, "the Tier " + std::to_string(number) + " challenge is missing");
        challenges.emplace_back("Tier " + std::to_string(number) + ":", Plain(html.substr(gt + 1, end - gt - 1)));
    }

    // the ascendancies
    const std::size_t section = html.find("name=\"ascendancies\"");
    if (section == std::string::npos) return Fail(error, "the ascendancy list is missing");
    std::vector<std::string> parts;
    std::size_t cursor = html.find(kDivMarker, section);
    while (cursor != std::string::npos) {
        const std::size_t next = html.find(kDivMarker, cursor + 1);
        parts.push_back(html.substr(cursor + std::string(kDivMarker).size(), (next == std::string::npos ? html.size() : next) - cursor - std::string(kDivMarker).size()));
        cursor = next;
    }
    for (const std::string& part : parts) {
        const std::string nameOpen = "<font color=\"orange\" size=\"4\">";
        const std::size_t nameAt = part.find(nameOpen);
        const std::size_t nameEnd = nameAt == std::string::npos ? std::string::npos : part.find(" Ascendancy</font>", nameAt);
        const std::size_t imageAt = part.find("<img src=\"");
        const std::size_t imageEnd = imageAt == std::string::npos ? std::string::npos : part.find('"', imageAt + 10);
        const std::string bodyOpen = "<font face=\"arial,helvetica\" size=\"-1\" style=\"width: 100%\">";
        const std::size_t bodyAt = part.find(bodyOpen);
        if (nameEnd == std::string::npos || imageEnd == std::string::npos || bodyAt == std::string::npos) {
            return Fail(error, "an ascendancy block could not be read");
        }
        // body: up to the first </font> followed only by white space and </div>
        std::size_t bodyEnd = std::string::npos;
        for (std::size_t scan = bodyAt + bodyOpen.size();;) {
            const std::size_t close = part.find("</font>", scan);
            if (close == std::string::npos) break;
            std::size_t after = close + 7;
            while (after < part.size() && IsSpace(part[after])) ++after;
            if (part.compare(after, 6, "</div>") == 0) { bodyEnd = close; break; }
            scan = close + 7;
        }
        if (bodyEnd == std::string::npos) return Fail(error, "an ascendancy block could not be read");
        const std::string title = Plain(part.substr(nameAt + nameOpen.size(), nameEnd - nameAt - nameOpen.size()));
        std::string src = part.substr(imageAt + 10, imageEnd - imageAt - 10);
        const std::size_t slash = src.find_last_of('/');
        if (slash != std::string::npos) src = src.substr(slash + 1);
        if (src.size() < 5 || !EndsWith(src, ".png")) return Fail(error, "an ascendancy block could not be read");
        ClassInfo info;
        info.image = src.substr(0, src.size() - 4);
        const std::string body = part.substr(bodyAt + bodyOpen.size(), bodyEnd - bodyAt - bodyOpen.size());
        const std::string tierOpen = "<font color=\"orange\">Tier ";
        std::size_t t = body.find(tierOpen);
        while (t != std::string::npos) {
            const std::size_t digit = t + tierOpen.size();
            std::size_t headerEnd = body.find("</font>", digit);
            if (headerEnd == std::string::npos || digit >= body.size() || body[digit] < '1' || body[digit] > '5') {
                return Fail(error, title + " does not have five tiers");
            }
            const int number = body[digit] - '0';
            const std::string header = Plain(body.substr(t, headerEnd + 7 - t));
            const std::size_t nextTier = body.find(tierOpen, headerEnd);
            info.headers[number] = header;
            info.stats[number] = LinesOf(body.substr(headerEnd + 7, (nextTier == std::string::npos ? body.size() : nextTier) - headerEnd - 7));
            info.tier[number] = true;
            t = nextTier;
        }
        for (int number = 1; number <= 5; ++number)
            if (!info.tier[number]) return Fail(error, title + " does not have five tiers");
        for (const std::string& note : Between(part, "<font size=\"-2\"", "</font>")) {
            const std::string text = Plain(note);
            if (!text.empty()) info.notes.push_back(text);
        }
        if (classes.count(title) || !recipes.count(title)) return Fail(error, "unexpected ascendancy " + title);
        info.recipe = recipes[title];
        info.pictureFirst = nameAt < bodyAt;
        classes[title] = std::move(info);
    }
    if (classes.size() != recipes.size()) return Fail(error, "the ascendancy list and the Tier 1 recipes disagree");
    return true;
}

std::vector<std::string> IntroLines(const std::string& text) {
    static const char* const ends[] = {"special encounters.", "endgame content.", "any order.", "etc.)."};
    std::vector<std::pair<std::size_t, std::size_t>> hits;  // (start, end) of each sentence end, in order of position
    for (const char* end : ends) {
        std::size_t at = 0;
        for (;;) {
            at = text.find(end, at);
            if (at == std::string::npos) break;
            hits.emplace_back(at, at + std::string(end).size());
            at += 1;
        }
    }
    std::sort(hits.begin(), hits.end());
    std::vector<std::string> out;
    std::size_t pos = 0;
    for (const auto& hit : hits) {
        if (hit.first < pos) continue;  // overlaps the previous match, as a regex scan would skip it
        out.push_back(Trim(text.substr(pos, hit.second - pos)));
        out.push_back(kSpacer);
        pos = hit.second;
    }
    if (!Trim(text.substr(pos)).empty()) out.push_back(Trim(text.substr(pos)));
    return out;
}

std::string TextLine(const std::string& text) { return "LINE\tT\t" + text; }
std::string StatLine(const std::string& text) { return "LINE\tS\t" + text; }

std::vector<std::string> ClassEntry(const std::string& title, const ClassInfo& info) {
    std::string inputs;
    for (std::size_t i = 0; i < info.recipe.input.size(); ++i) {
        if (i) inputs += kBreak;
        inputs += std::string(info.recipe.input[i].second ? kGold : kWhite) + info.recipe.input[i].first + kWhite;
    }
    std::vector<std::string> out = {"ENTRY\t" + title + "\t\t" + (info.pictureFirst ? "splitflip" : "split"), kBlockLine, std::string("IMG\t") + kKeyPrefix + info.image};
    for (int number = 1; number <= 5; ++number) {
        if (number > 1) out.push_back(StatLine(kSpacer));
        const std::string& header = info.headers[number];
        std::size_t stars = header.size();
        while (stars > 0 && header[stars - 1] == '*') --stars;
        std::string line = std::string(kGold) + "Tier " + std::to_string(number);
        if (stars < header.size()) line += std::string(kRed) + header.substr(stars) + kGold;
        line += std::string(":") + kWhite;
        out.push_back(StatLine(line));
        for (const std::string& stat : info.stats[number]) out.push_back(StatLine(std::string(kBlue) + RedStars(stat, kBlue) + kWhite));
    }
    if (!info.notes.empty()) {
        out.push_back(TextLine(kSpacer));
        for (const std::string& note : info.notes) out.push_back(TextLine(std::string(kRed) + note));
    }
    // what goes in and what comes out, at the bottom
    out.push_back(TextLine(kSpacer));
    out.push_back(std::string("LINE\tC\tInput") + kSeparator + "Output");
    out.push_back("LINE\tR\t" + inputs + kSeparator + kGold + info.recipe.output + kWhite);
    return out;
}

}  // namespace

bool BuildWebAscendancyWikiData(const std::string& current, const std::string& page, std::string& out, std::string& error) {
    std::string intro;
    std::vector<std::pair<std::string, std::string>> challenges;
    std::map<std::string, ClassInfo> classes;
    if (!Parse(page, intro, challenges, classes, error)) return false;

    // the existing page: its PAGE line, and its Orb of Creation table with what follows it
    std::vector<std::string> lines = SplitLines(current);
    std::size_t begin = lines.size(), end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].compare(0, 5, "PAGE\t") != 0) continue;
        if (begin == lines.size()) {
            const std::vector<std::string> parts = SplitTabs(lines[i]);
            if (parts.size() > 1 && parts[1] == "ascendancy") begin = i;
        } else {
            end = i;
            break;
        }
    }
    if (begin == lines.size()) return Fail(error, "the wiki data has no ascendancy page");
    std::size_t orb = end;
    for (std::size_t i = begin; i < end; ++i)
        if (lines[i] == std::string("LINE\tT\t") + kOrbTitle) { orb = i; break; }
    if (orb == end) return Fail(error, "the existing page has no Orb of Creation table to keep");
    std::size_t orbEnd = end;
    for (std::size_t i = orb; i < end; ++i)
        if (lines[i].compare(0, 6, "ENTRY\t") == 0) { orbEnd = i; break; }

    std::vector<std::string> result = {lines[begin], "ENTRY\tAscendancies\t", kBlockLine};
    for (const std::string& line : IntroLines(intro)) result.push_back(TextLine(line));
    result.insert(result.end(), lines.begin() + static_cast<std::ptrdiff_t>(orb), lines.begin() + static_cast<std::ptrdiff_t>(orbEnd));
    result.push_back("ENTRY\tHigher Tier Challenges\t");
    result.push_back(kBlockLine);
    for (const auto& challenge : challenges) {
        result.push_back(TextLine(challenge.first));
        result.push_back(TextLine(challenge.second));
        result.push_back(TextLine(kSpacer));
    }
    std::vector<std::string> titles;
    for (const auto& entry : classes) titles.push_back(entry.first);
    std::stable_sort(titles.begin(), titles.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
    for (const std::string& title : titles) {
        const std::vector<std::string> entry = ClassEntry(title, classes[title]);
        result.insert(result.end(), entry.begin(), entry.end());
    }
    out = Splice(current, result, "ascendancy", "");
    return true;
}

}  // namespace esrwiki
