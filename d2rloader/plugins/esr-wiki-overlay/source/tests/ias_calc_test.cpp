// Checks the C++ calculator against the website's own script.
//   ias_calc_test <ias-calculator.html> <ias-calculator-data.js> <cases.tsv from tests/gen_ias_cases.js>
#include <fstream>
#include <iostream>
#include <sstream>

#include "ias_calc.hpp"

using namespace esrwiki::ias;

static std::string ReadAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

static std::vector<std::string> Split(const std::string& line, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : line) {
        if (c == separator) { parts.push_back(current); current.clear(); }
        else current += c;
    }
    parts.push_back(current);
    return parts;
}

static std::string Normal(const std::string& text) {
    std::string out;
    bool space = true;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { if (!space) out += ' '; space = true; }
        else { out += c; space = false; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static std::string RowsText(const Table& table) {
    std::string out;
    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        const TableRow& row = table.rows[i];
        if (i) out += "~";
        out += std::to_string(row.stat) + "|" + std::to_string(row.frames) + "|" + row.perSecond + "|" + row.gap + "|" + std::to_string(row.state);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "usage: ias_calc_test page.html data.js cases.tsv\n"; return 2; }
    Data data;
    std::string error;
    if (!Parse(ReadAll(argv[1]), ReadAll(argv[2]), data, error)) { std::cerr << "FAIL: parse: " << error << "\n"; return 1; }
    std::cout << data.classes.size() << " classes, " << data.forms.size() << " forms, " << data.weapons.size() << " weapons\n";
    if (Parse("<html></html>", ReadAll(argv[2]), data, error)) { std::cerr << "FAIL: a page without constants was accepted\n"; return 1; }
    if (Parse(ReadAll(argv[1]), "window.__DATA__={\"c\":[]", data, error)) { std::cerr << "FAIL: a cut-off data file was accepted\n"; return 1; }
    if (Parse(ReadAll(argv[1]), "window.__DATA__={\"c\":[],\"f\":[],\"w\":[]};", data, error)) { std::cerr << "FAIL: an empty data file was accepted\n"; return 1; }
    Data good;
    Parse(ReadAll(argv[1]), ReadAll(argv[2]), good, error);

    std::istringstream cases(ReadAll(argv[3]));
    std::string line;
    int number = 0, failures = 0;
    while (std::getline(cases, line)) {
        if (line.empty()) continue;
        ++number;
        std::vector<std::string> f = Split(line, '\t');
        if (f.size() != 41) { std::cerr << "FAIL: case " << number << " has " << f.size() << " fields\n"; return 1; }
        Setup setup;
        setup.cls = std::stoi(f[0]); setup.form = std::stoi(f[1]); setup.wep = std::stoi(f[2]); setup.wep2 = std::stoi(f[3]);
        setup.skill = f[4];
        setup.ias = std::stoi(f[5]); setup.wias = std::stoi(f[6]); setup.oias = std::stoi(f[7]);
        setup.ar = std::stoi(f[8]); setup.mar = std::stoi(f[9]); setup.oar = std::stoi(f[10]); setup.fcr = std::stoi(f[11]);
        const Result r = Compute(good, setup);
        const bool hasAnim = f[14] == "1";
        struct Check { const char* name; std::string got, want; };
        std::vector<Check> checks = {
            {"skill", r.setup.skill, f[12]},
            {"offhand", std::to_string(r.setup.wep2), f[13]},
            {"hasAnim", r.hasAnim ? "1" : "0", f[14]},
            {"totalIas", r.totalIas, f[18]},
            {"flat", r.flatSpeed, f[19]},
            {"attackFrames", r.attackFrames, f[22]},
            {"attackPerSecond", r.attackPerSecond, f[23]},
            {"tableTitleStat", r.tableTitleStat, f[27]},
            {"attackRows", RowsText(r.attack), f[29]},
            {"attackNote", Normal(r.attack.note), Normal(f[30])},
            {"castFrames", r.castFrames, f[31]},
            {"castPerSecond", r.castPerSecond, f[32]},
            {"castRows", RowsText(r.cast), f[33]},
            {"castNote", Normal(r.cast.note), Normal(f[34])},
            {"showSplit", r.showMainHandIas ? "1" : "0", f[35]},
            {"offhandLive", r.offhandLive ? "1" : "0", f[36]},
            {"iasHint", Normal(r.iasHint), Normal(f[37])},
            {"mainHandIasHint", Normal(r.mainHandIasHint), Normal(f[38])},
            {"offhandHint", Normal(r.offhandHint), Normal(f[39])},
            {"formHint", Normal(r.formHint), Normal(f[40])},
        };
        if (hasAnim) {
            checks.push_back({"anim", r.anim, f[15]});
            checks.push_back({"baseFrames", r.baseFrames, f[16]});
            checks.push_back({"speedModifier", r.speedModifier, f[17]});
            checks.push_back({"effectiveIas", r.effectiveIas, f[20]});
            checks.push_back({"effectiveWarn", r.effectiveWarn ? "1" : "0", f[21]});
            checks.push_back({"attackSolo", r.attackSolo, f[24]});
            checks.push_back({"attackLabel", r.attackLabel, f[25]});
            checks.push_back({"attackPerSecondLabel", r.attackPerSecondLabel, f[26]});
            checks.push_back({"tableTitlePerSecond", r.tableTitlePerSecond, f[28]});
        }
        for (const Check& check : checks) {
            if (check.got != check.want) {
                if (failures < 12)
                    std::cerr << "FAIL: case " << number << " " << check.name << "\n  got:  " << check.got << "\n  want: " << check.want
<< "\n  setup: " << f[0] << " form " << f[1] << " wep " << f[2] << "/" << f[3] << " " << f[4] << " ias " << f[5] << "," << f[6] << "," << f[7]
                              << " ar " << f[8] << "," << f[9] << "," << f[10] << " fcr " << f[11] << "\n";
                ++failures;
                break;
            }
        }
    }
    if (failures) { std::cerr << failures << " of " << number << " cases differ\n"; return 1; }
    std::cout << "ias_calc_test: " << number << " cases match the website script\n";
    return 0;
}
