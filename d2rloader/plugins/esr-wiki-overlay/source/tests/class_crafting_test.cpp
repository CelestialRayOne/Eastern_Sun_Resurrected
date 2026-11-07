// Checks the Class Crafting page built from the cube recipes page against the shipped page: the same items by title,
// every item with its art, no duplicates, and only the Class Crafting page changed.
// Usage: class_crafting_test <cube-recipes-page.html> <wiki-data.txt>
#include "web_class_live.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool ReadFile(const char* path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// The span [begin, end) of the class-crafting page in these lines.
bool Span(const std::vector<std::string>& lines, std::size_t& begin, std::size_t& end) {
    bool found = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!found && lines[i].compare(0, 24, "PAGE\tclass-crafting\tClas") == 0) {
            begin = i;
            found = true;
        } else if (found && lines[i].compare(0, 5, "PAGE\t") == 0) {
            end = i;
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::printf("usage: class_crafting_test <cube-recipes-page.html> <wiki-data.txt>\n");
        return 2;
    }
    std::string cube, shipped, error;
    if (!ReadFile(argv[1], cube) || !ReadFile(argv[2], shipped)) {
        std::printf("could not read the inputs\n");
        return 2;
    }
    std::string built;
    if (!esrwiki::BuildWebClassCraftingWikiData(shipped, cube, built, error)) {
        std::printf("FAILED to build: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAILED: %s\n", what);
            ++failures;
        }
    };

    const std::vector<std::string> oldLines = Lines(shipped), newLines = Lines(built);
    std::size_t ob = 0, oe = 0, nb = 0, ne = 0;
    check(Span(oldLines, ob, oe), "the shipped wiki data has a class-crafting page");
    check(Span(newLines, nb, ne), "the built wiki data has a class-crafting page");
    if (failures == 0) {
        check(std::vector<std::string>(oldLines.begin(), oldLines.begin() + static_cast<std::ptrdiff_t>(ob)) ==
                  std::vector<std::string>(newLines.begin(), newLines.begin() + static_cast<std::ptrdiff_t>(nb)),
              "the pages before Class Crafting are unchanged");
        check(std::vector<std::string>(oldLines.begin() + static_cast<std::ptrdiff_t>(oe), oldLines.end()) ==
                  std::vector<std::string>(newLines.begin() + static_cast<std::ptrdiff_t>(ne), newLines.end()),
              "the pages after Class Crafting are unchanged");

        std::set<std::string> oldTitles, newTitles;
        std::map<std::string, int> seen;
        int withArt = 0;
        for (std::size_t i = nb; i < ne; ++i) {
            if (newLines[i].compare(0, 8, "BLOCK\t1\t") != 0) continue;
            const std::string title = newLines[i].substr(8, newLines[i].find('\t', 8) - 8);
            ++seen[title];
            newTitles.insert(title);
            if (i + 1 < ne && newLines[i + 1].compare(0, 4, "IMG\t") == 0) ++withArt;
        }
        for (std::size_t i = ob; i < oe; ++i) {
            if (oldLines[i].compare(0, 8, "BLOCK\t1\t") != 0) continue;
            oldTitles.insert(oldLines[i].substr(8, oldLines[i].find('\t', 8) - 8));
        }
        int duplicates = 0;
        for (const auto& entry : seen) duplicates += entry.second - 1;
        std::printf("items: %zu built, %zu shipped, %d with art, %d duplicates\n", newTitles.size(), oldTitles.size(), withArt,
                    duplicates);
        check(duplicates == 0, "no item is listed twice");
        check(newTitles == oldTitles, "the same items as the shipped page, by title");
        check(withArt == static_cast<int>(newTitles.size()), "every item has art");
    }

    if (failures == 0) std::printf("class_crafting_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
