// The Frame Table calculator of the ESR website (ias-calculator.html), as an engine without any Win32.
//
// It is a port of the page's own script: the same formulas, the same notes, the same tables. The data it works on
// (classes, forms, weapons and the animation frames) comes from the page's data file, and the tunable numbers
// (cast rate cap, Whirlwind, crossbow, ...) from the constants at the top of its script, so a change on the website
// reaches the game without a new build. A change to the formulas themselves needs a new build; tests/ias_calc_test.cpp
// checks this port against the page's script.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace esrwiki::ias {

struct Anim3 {
    int fpd = 0;  // frames per direction
    int spd = 0;  // animation speed
    int af = 0;   // attack frame (the hit)
};

struct ClassData {
    std::string name;
    std::map<std::string, Anim3> a1;  // attack animation per weapon class (hth, 1hs, 2ht, bow, ...)
    std::optional<Anim3> sc, kk, s1, s2;  // cast, kick, smite, trap
};

struct FormData {
    std::string name;
    Anim3 a1, sc;
};

struct Weapon {
    std::string name;
    int wsm = 0;
    std::string wclass, wclass2;
    bool twoHanded = false;
    std::string typeCode, typeName;
    bool oneOrTwo = false;  // a two-handed sword that may be held in one hand
};

struct Constants {
    int fcrCap = 500;
    bool wwHit = true;
    int wwWeight = 50, wwStretch = 170, wwMinFrames = 6, wwMaxRate = 300, wwMinRate = 15, wwNoAnim = 10;
    int xbowFrames = 5, strafeRollback = 15, seqPenalty = 30;
};

struct Data {
    std::vector<ClassData> classes;
    std::vector<FormData> forms;
    std::vector<Weapon> weapons;
    Constants k;
};

// Optional display-text translator: receives the English text (with {0}, {1} placeholders) and returns the translation, or
// an empty string when there is none. Without one, everything stays English.
using Translator = std::string (*)(const std::string&);
void SetTranslator(Translator translator);

// The website pages the data is read from, in the order Parse expects them.
inline constexpr const char* kPageNames[2] = {"ias-calculator.html", "ias-calculator-data.js"};

// Reads the page's script constants and the data file. Fails, leaving `out` untouched, when the pages are incomplete
// or do not look like the expected ones.
bool Parse(const std::string& pageHtml, const std::string& dataJs, Data& out, std::string& error);

struct Setup {
    int cls = 0;
    int form = -1;  // index into forms, -1 for none
    int wep = -1;   // index into weapons, -1 for bare-handed
    int wep2 = -1;  // off hand
    std::string skill = "standard";
    int ias = 0, wias = 0, oias = 0, ar = 0, mar = 0, oar = 0, fcr = 0;
};

struct SkillChoice {
    const char* id;
    const char* name;
};
inline constexpr SkillChoice kSkills[10] = {
    {"standard", "Standard attack"}, {"ww", "Whirlwind"}, {"strafe", "Strafe"}, {"frenzy", "Frenzy"},
    {"chargeup", "Charge-up strike"}, {"cleave", "Cleave"}, {"mirrored", "Mirrored Blades"}, {"kick", "Kick"},
    {"traps", "Laying traps"}, {"smite", "Smite"}};

bool SkillAllowed(const Data& data, const Setup& setup, const std::string& skill);
bool CanOffhand(const Data& data, const Setup& setup, const Weapon& weapon);
bool SkillUsesOffhand(const std::string& skill);  // Whirlwind, Frenzy and charge-up strikes

// Brings a setup to a state the page could be in: a skill the class cannot use becomes the standard attack, and an
// off-hand weapon that cannot be held (or is not swung by the skill) is dropped. Compute does it first.
void Normalise(const Data& data, Setup& setup);

struct TableRow {
    int stat = 0;
    int frames = 0;
    std::string perSecond;
    std::string gap;
    int state = 0;  // 0 not reached, 1 reached, 2 you are here, 3 next
};

struct Table {
    std::vector<TableRow> rows;  // empty: not available for this setup
    std::string note;
};

struct Result {
    Setup setup;  // as normalised
    // the readout
    bool hasAnim = false;
    std::string anim, baseFrames, speedModifier, totalIas, flatSpeed, effectiveIas;
    bool effectiveWarn = false;
    // the attack box
    std::string attackFrames, attackPerSecond, attackSolo, attackLabel, attackPerSecondLabel;
    std::string tableTitleStat, tableTitlePerSecond;
    Table attack;
    // the cast box
    std::string castFrames, castPerSecond;
    Table cast;
    // what the page shows or hides
    bool showMainHandIas = false, showOffHandFields = false;
    bool offhandLive = false;
    std::string iasHint, mainHandIasHint, formHint, offhandHint;
};

Result Compute(const Data& data, Setup setup);

}  // namespace esrwiki::ias
