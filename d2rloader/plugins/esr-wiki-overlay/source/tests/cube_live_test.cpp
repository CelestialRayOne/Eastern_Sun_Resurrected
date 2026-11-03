// Checks that the C++ cube page reader produces exactly what the Python tools write.
//   cube_live_test <cube recipes page .html> <expected cube-wiki-data.txt>
#include <fstream>
#include <iostream>
#include <sstream>

#include "cube_live.hpp"

static std::string ReadAll(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: cube_live_test page.html expected.txt\n"; return 2; }
    std::string out, error;
    if (!esrwiki::BuildCubeWikiData(ReadAll(argv[1]), out, error)) { std::cerr << "FAIL: " << error << "\n"; return 1; }
    const std::string expected = ReadAll(argv[2]);
    if (out != expected) {
        std::ofstream("cube_live_actual.txt", std::ios::binary) << out;
        std::cerr << "FAIL: output differs (written to cube_live_actual.txt)\n";
        return 1;
    }
    // A page with no recipes must be refused, not turned into an empty panel.
    if (esrwiki::BuildCubeWikiData("<html><body><table><tr><td>x</td></tr></table></body></html>", out, error)) {
        std::cerr << "FAIL: an empty page was accepted\n";
        return 1;
    }
    std::cout << "cube_live_test: all checks passed\n";
    return 0;
}
