// Translates the wiki data from the website's localized pages.
//
// Platform-neutral (no Win32). The English page builders only understand the English pages. The localized pages of
// the ESR website (https://easternsunresurrected.com/<code>/<page>) have the same structure as the English ones, so
// the text of a localized page lines up, node by node, with the text of the English page. This turns those pairs
// into a translation table and applies it to the wiki data the English builders produce. Whatever the localized page
// says is used as it is, a missing translation on the website stays missing here.
//
// tools/locale_align.py is the first prototype of the idea (exact matches only); this file is the reference.
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace esrwiki::localize {

struct TextNode {
    std::string text;  // the visible text, entities decoded, white space collapsed
    std::string key;   // where it sits (the last three open tags with their classes) and its digits and punctuation
};

// The visible text nodes of an HTML page in document order (script and style left out).
std::vector<TextNode> ExtractTextNodes(const std::string& html);

// Pairs the nodes of the English page with those of the localized page. Equal counts pair by position; otherwise it
// resynchronises where the keys of four nodes in a row match again.
std::vector<std::pair<std::string, std::string>> AlignNodesSequential(const std::vector<TextNode>& english,
                                                                     const std::vector<TextNode>& localized);
std::vector<std::pair<std::string, std::string>> AlignNodes(const std::vector<TextNode>& english,
                                                            const std::vector<TextNode>& localized);

struct TranslateStats {
    std::size_t cellsWithWords = 0;  // cells that contain at least three letters in a row
    std::size_t translated = 0;
};

class Translator {
public:
    // Adds one page: its English and its localized copy (UTF-8). Pages are added in a fixed order.
    void AddPage(const std::string& englishHtml, const std::string& localizedHtml);
    // Text the website pages do not carry (labels the builders write themselves), with its translation. Used only for
    // text no page translated. Call before Finish.
    void AddFallback(const std::string& english, const std::string& localized);
    // Call after the last AddPage.
    void Finish();
    std::size_t Size() const { return exact_.size(); }

    // `wikiData` is wiki-data.txt as the English builders produce it. Every cell the table knows is replaced.
    std::string Translate(const std::string& wikiData, TranslateStats* stats = nullptr) const;

private:
    struct Choice {
        std::vector<std::pair<std::string, int>> values;  // in the order first seen
    };
    std::vector<std::string> order_;
    std::unordered_map<std::string, Choice> seen_;
    std::unordered_map<std::string, std::string> exact_;
    std::unordered_map<std::string, std::string> pattern_;  // numbers replaced by '#'
    // Every piece of text with numbers replaced by '#', for cells that join several pieces of the website: the longest
    // known piece is taken at each word start. Indexed by the first two bytes, longest first.
    std::unordered_map<unsigned, std::vector<std::pair<std::string, std::string>>> pieces_;
    // Names the pages only carry inside a longer text, such as "Arcanist" in "Arcanist Ascendancy" and "Ascendance
    // Arcaniste": found by removing the words every text of such a group shares. Used for entry titles only.
    std::unordered_map<std::string, std::string> derived_;
    std::unordered_map<std::string, std::string> derivedLower_;  // the same, for names that come out in lower case ("du Ciel")
    void DeriveNames();
    std::unordered_map<std::string, std::string> lowerExact_;    // the same by lower-case English text (the builders change capitals: "(2 Items)")
    std::unordered_map<std::string, std::string> lowerPattern_;
    std::unordered_set<std::string> identity_;  // short texts the localized page leaves exactly as the English page has them
    std::unordered_set<std::string> fallback_;  // text only AddFallback knows
    bool TranslateCell(std::string& cell, bool allowFallback = true) const;
    bool TranslatePieces(const std::string& core, std::string& out) const;
};

}  // namespace esrwiki::localize
