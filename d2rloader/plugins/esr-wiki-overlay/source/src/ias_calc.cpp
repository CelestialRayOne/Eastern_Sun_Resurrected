#include "ias_calc.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace esrwiki::ias {
namespace {

using LL = long long;

// Display text goes through a translator the plugin installs; the English text is the lookup key.
Translator gTranslator = nullptr;
std::string T(const std::string& english) {
    if (gTranslator) {
        std::string out = gTranslator(english);
        if (!out.empty()) return out;
    }
    return english;
}
std::string TF(const std::string& english, std::initializer_list<std::string> args) {
    std::string out = T(english);
    std::size_t index = 0;
    for (const std::string& arg : args) {
        const std::string key = "{" + std::to_string(index++) + "}";
        for (std::size_t at = out.find(key); at != std::string::npos; at = out.find(key, at + arg.size())) out.replace(at, key.size(), arg);
    }
    return out;
}

// ---------- reading the website ----------------------------------------------------------------

struct Json {
    enum Kind { Null, Number, String, Array, Object } kind = Null;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;
    const Json* Find(const std::string& key) const {
        for (const auto& member : members)
            if (member.first == key) return &member.second;
        return nullptr;
    }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : s_(text) {}
    bool Read(std::size_t& at, Json& out, int depth = 0) {
        if (depth > 32) return false;
        Skip(at);
        if (at >= s_.size()) return false;
        const char c = s_[at];
        if (c == '{') {
            out.kind = Json::Object;
            ++at;
            Skip(at);
            if (at < s_.size() && s_[at] == '}') { ++at; return true; }
            for (;;) {
                Skip(at);
                Json key;
                if (at >= s_.size() || s_[at] != '"' || !String(at, key.text)) return false;
                Skip(at);
                if (at >= s_.size() || s_[at] != ':') return false;
                ++at;
                Json value;
                if (!Read(at, value, depth + 1)) return false;
                out.members.emplace_back(key.text, std::move(value));
                Skip(at);
                if (at < s_.size() && s_[at] == ',') { ++at; continue; }
                if (at < s_.size() && s_[at] == '}') { ++at; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.kind = Json::Array;
            ++at;
            Skip(at);
            if (at < s_.size() && s_[at] == ']') { ++at; return true; }
            for (;;) {
                Json value;
                if (!Read(at, value, depth + 1)) return false;
                out.items.push_back(std::move(value));
                Skip(at);
                if (at < s_.size() && s_[at] == ',') { ++at; continue; }
                if (at < s_.size() && s_[at] == ']') { ++at; return true; }
                return false;
            }
        }
        if (c == '"') {
            out.kind = Json::String;
            return String(at, out.text);
        }
        if (s_.compare(at, 4, "null") == 0) { out.kind = Json::Null; at += 4; return true; }
        if (s_.compare(at, 4, "true") == 0) { out.kind = Json::Number; out.number = 1; at += 4; return true; }
        if (s_.compare(at, 5, "false") == 0) { out.kind = Json::Number; out.number = 0; at += 5; return true; }
        char* end = nullptr;
        out.number = std::strtod(s_.c_str() + at, &end);
        if (end == s_.c_str() + at) return false;
        out.kind = Json::Number;
        at = static_cast<std::size_t>(end - s_.c_str());
        return true;
    }

private:
    void Skip(std::size_t& at) const {
        while (at < s_.size() && (s_[at] == ' ' || s_[at] == '\t' || s_[at] == '\n' || s_[at] == '\r')) ++at;
    }
    static void Utf8(std::string& out, unsigned long code) {
        if (code < 0x80) out += static_cast<char>(code);
        else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
        else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    }
    bool String(std::size_t& at, std::string& out) const {
        ++at;  // opening quote
        out.clear();
        while (at < s_.size() && s_[at] != '"') {
            if (s_[at] != '\\') { out += s_[at++]; continue; }
            if (++at >= s_.size()) return false;
            const char e = s_[at++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (at + 4 > s_.size()) return false;
                    Utf8(out, std::strtoul(s_.substr(at, 4).c_str(), nullptr, 16));
                    at += 4;
                    break;
                }
                default: out += e; break;  // \" \\ \/
            }
        }
        if (at >= s_.size()) return false;
        ++at;
        return true;
    }
    const std::string& s_;
};

bool ReadAnim(const Json* node, Anim3& out) {
    if (node == nullptr || node->kind != Json::Array || node->items.size() < 3) return false;
    for (int i = 0; i < 3; ++i)
        if (node->items[i].kind != Json::Number) return false;
    out.fpd = static_cast<int>(node->items[0].number);
    out.spd = static_cast<int>(node->items[1].number);
    out.af = static_cast<int>(node->items[2].number);
    return true;
}

bool ReadOptionalAnim(const Json* node, std::optional<Anim3>& out) {
    if (node == nullptr || node->kind == Json::Null) { out.reset(); return true; }
    Anim3 anim;
    if (!ReadAnim(node, anim)) return false;
    out = anim;
    return true;
}

// `var NAME = 123;` or `= true;` in the page's script
bool Constant(const std::string& page, const std::string& name, int& value) {
    const std::string needle = "var " + name;
    std::size_t at = 0;
    while ((at = page.find(needle, at)) != std::string::npos) {
        std::size_t k = at + needle.size();
        at = k;
        if (k < page.size() && (std::isalnum(static_cast<unsigned char>(page[k])) || page[k] == '_')) continue;
        while (k < page.size() && (page[k] == ' ' || page[k] == '\t')) ++k;
        if (k >= page.size() || page[k] != '=') continue;
        ++k;
        while (k < page.size() && (page[k] == ' ' || page[k] == '\t')) ++k;
        if (page.compare(k, 4, "true") == 0) { value = 1; return true; }
        if (page.compare(k, 5, "false") == 0) { value = 0; return true; }
        char* end = nullptr;
        const long parsed = std::strtol(page.c_str() + k, &end, 10);
        if (end == page.c_str() + k) continue;
        value = static_cast<int>(parsed);
        return true;
    }
    return false;
}

// ---------- the engine -------------------------------------------------------------------------

using Opt = std::optional<LL>;

LL FloorDiv(LL a, LL b) {  // b > 0
    LL q = a / b;
    if ((a % b != 0) && (a < 0)) --q;
    return q;
}
LL CeilDiv(LL a, LL b) {  // b > 0
    return -FloorDiv(-a, b);
}

std::string Signed(LL v) { return (v > 0 ? "+" : "") + std::to_string(v); }

// Number.prototype.toFixed(2): an exact tie rounds up, which printf does not do
std::string Fixed2(double x) {
    const double eighths = x * 8;
    if (x >= 0 && eighths == std::floor(eighths) && (static_cast<LL>(eighths) & 1)) {
        const LL hundredths = static_cast<LL>(std::floor(x * 100)) + 1;
        char text[64];
        std::snprintf(text, sizeof(text), "%lld.%02lld", hundredths / 100, hundredths % 100);
        return text;
    }
    char text[64];
    std::snprintf(text, sizeof(text), "%.2f", x);
    return text;
}

double Fps(LL frames) { return frames > 0 ? 25.0 / static_cast<double>(frames) : 0; }

struct SeqSlot {
    const char* wclass;  // "other" for the default
    int steps;
    int hit1, hit2;      // steps carrying the hit, -1 for none
};

struct SeqDef {
    const char* id;
    const char* note;
    std::vector<SeqSlot> slots;
};

const std::vector<SeqDef>& Sequences() {
    static const std::vector<SeqDef> defs = {
        {"frenzy",
         "The first nine steps come from the normal attack and the main hand strikes on step 5; the last eight come from the left-hand swing and the off hand strikes on step 10.",
         {{"other", 17, 4, 9}}},
        {"chargeup",
         "One claw or bare hands: twelve steps, striking on step 7. A claw in each hand: sixteen steps, the first claw striking on step 7 and the second on step 11, so dual claws make the cycle longer and hit twice.",
         {{"ht2", 16, 6, 10}, {"other", 12, 6, -1}}},
        {"cleave",
         "Every step is a frame of the cast animation, played as an attack, so it runs on attack speed.",
         {{"1hs", 16, 9, -1}, {"1ht", 16, 9, -1}, {"2hs", 18, 10, -1}, {"2ht", 20, 10, -1}, {"stf", 22, 10, -1}, {"other", 19, 12, -1}}},
        {"mirrored",
         "Every step is a frame of the cast animation, played as an attack, so it runs on attack speed.",
         {{"hth", 17, 12, -1}, {"ht1", 17, 12, -1}, {"ht2", 17, 12, -1}, {"1hs", 16, 10, -1}, {"1ht", 16, 10, -1}, {"1js", 16, 10, -1},
          {"1jt", 16, 10, -1}, {"1ss", 16, 10, -1}, {"1st", 16, 10, -1}, {"2hs", 19, 12, -1}, {"2ht", 21, 11, -1}, {"bow", 18, 11, -1},
          {"xbw", 18, 11, -1}, {"stf", 17, 8, -1}, {"other", 17, 12, -1}}}};
    return defs;
}

const SeqDef* FindSeq(const std::string& skill) {
    for (const SeqDef& def : Sequences())
        if (skill == def.id) return &def;
    return nullptr;
}

struct Anim {
    // sequence
    const SeqDef* seq = nullptr;
    int steps = 0;
    int hits = 1;
    // plain
    int fpd = 0, spd = 0, af = 0;
    int formFpd = 0;
    bool shifted = false;
    bool xbow = false;
    // whirlwind
    bool ww = false;
    bool rec = false;
    std::string label;
};

std::string Upper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

class Engine {
public:
    Engine(const Data& data, const Setup& setup) : D(data), S(setup), K(data.k) {
        fcrMax = K.fcrCap;
        fcrRateCap = 100 + static_cast<int>(Trunc(120 * static_cast<LL>(K.fcrCap), K.fcrCap + 120));
        fcrReached = fcrMax;
        for (int s = 0; s <= fcrMax; ++s) {
            const Opt d = Diminish(s);
            if (d && *d + 100 >= fcrRateCap) { fcrReached = s; break; }
        }
    }

    Result Run();

private:
    const Data& D;
    Setup S;
    const Constants& K;
    int fcrMax = 0, fcrRateCap = 0, fcrReached = 0;

    static LL Trunc(LL a, LL b) { return a / b; }  // b > 0 or either sign: C++ truncates toward zero like Math.trunc

    const ClassData& Cls() const { return D.classes[static_cast<std::size_t>(S.cls)]; }
    const FormData* Form() const { return S.form >= 0 ? &D.forms[static_cast<std::size_t>(S.form)] : nullptr; }
    const Weapon* Main() const { return S.wep >= 0 ? &D.weapons[static_cast<std::size_t>(S.wep)] : nullptr; }
    const Weapon* Off() const { return S.wep2 >= 0 ? &D.weapons[static_cast<std::size_t>(S.wep2)] : nullptr; }

    Opt Diminish(LL v) const {
        if (v == 0) return 0;
        if (v == -120) return std::nullopt;
        return Trunc(120 * v, 120 + v);
    }

    // cast
    Opt CastRate(LL fcr, LL animSpeed) const {
        fcr = std::min<LL>(fcr, fcrMax);
        const Opt e = Diminish(fcr);
        if (!e) return std::nullopt;
        const LL rate = std::min<LL>(*e + 100, fcrRateCap);
        if (rate < 0) return 0x7FFF;
        return std::min<LL>(FloorDiv(rate * animSpeed, 100), 0x7FFF);
    }
    bool CastWraps(LL fcr) const {
        const Opt e = Diminish(std::min<LL>(fcr, fcrMax));
        return e && *e + 100 < 0;
    }
    Opt CastFrames(LL fpd, LL animSpeed, LL fcr) const {
        const Opt r = CastRate(fcr, animSpeed);
        if (!r || *r == 0) return std::nullopt;
        return std::max<LL>(1, CeilDiv(256 * fpd, *r) - 1);
    }

    // attack
    LL ClampEias(LL e, bool shifted) const { return std::max<LL>(-85, std::min<LL>(shifted ? 150 : 75, e)); }
    static LL RateOf(LL animSpeed, LL eias) { return FloorDiv(animSpeed * (100 + eias), 100); }
    Opt FramesFor(LL fpd, LL animSpeed, LL eias) const {
        const LL r = RateOf(animSpeed, eias);
        if (r <= 0) return std::nullopt;
        return std::max<LL>(1, CeilDiv(256 * fpd, r) - 1);
    }
    Opt SeqFramesAt(LL steps, LL rate) const {
        if (rate <= 0) return std::nullopt;
        return std::max<LL>(1, CeilDiv(256 * steps, rate));
    }
    Opt FormFrames(LL fpdH, LL spdH, LL fpdF, LL eias) const {
        const LL rh = RateOf(spdH, eias);
        if (rh <= 0) return std::nullopt;
        LL r = FloorDiv(rh * fpdF, fpdH);
        if (r <= 0) return std::nullopt;
        if (FloorDiv(fpdH * 256, rh) < FloorDiv(fpdF * 256, r)) ++r;
        return std::max<LL>(1, CeilDiv(256 * fpdF, r) - 1);
    }

    struct WwRate { LL rate, raw; };
    WwRate WwRateOf(LL d, LL flat, LL wsm, bool shifted) const {
        if (K.wwHit) {
            const LL raw = 100 + Trunc((flat - wsm) * K.wwWeight, 100) + d;
            return {std::max<LL>(K.wwMinRate, std::min<LL>(K.wwMaxRate, raw)), raw};
        }
        return {100 + ClampEias(d + flat - wsm, shifted), 100 + d + flat - wsm};
    }
    Opt WwRaw(const Anim& anim, LL ias, LL flat, LL wsm) const {
        if (!anim.rec) return K.wwNoAnim;
        const Opt d = Diminish(ias);
        if (!d) return std::nullopt;
        const LL rate = WwRateOf(*d, flat, wsm, anim.shifted).rate;
        if (K.wwHit) {
            const LL step = std::max<LL>(1, FloorDiv(anim.spd * rate, 100));
            return std::max<LL>(K.wwMinFrames, Trunc(256 * anim.af * K.wwStretch, 100 * step));
        }
        const LL r = RateOf(anim.spd, rate - 100);
        if (r <= 0) return std::nullopt;
        return FloorDiv(256 * anim.af, r);
    }

    LL XbowRate(LL fpd) const {
        return std::min<LL>(0x7FFF, std::max<LL>(1, FloorDiv(256 * fpd + K.xbowFrames, K.xbowFrames + 1)));
    }
    LL XbowSeqRate(LL steps) const {
        return std::min<LL>(0x7FFF, std::max<LL>(1, FloorDiv(256 * steps + K.xbowFrames - 1, K.xbowFrames)));
    }
    static bool IsCrossbow(const Weapon* w) { return w != nullptr && (w->wclass == "xbw" || w->typeCode == "xbow"); }
    static bool IsClaw(const Weapon* w) { return w != nullptr && (w->wclass == "ht1" || w->wclass == "ht2"); }

    const SeqSlot& SeqSlotOf(const SeqDef& def, const std::string& wc) const {
        const SeqSlot* other = nullptr;
        for (const SeqSlot& slot : def.slots) {
            if (wc == slot.wclass) return slot;
            if (std::string(slot.wclass) == "other") other = &slot;
        }
        return *other;
    }

    bool MainAllowsOffhand() const { return Main() != nullptr && CanOffhand(D, S, *Main()); }
    bool DualActive(const std::string& sk) const { return SkillUsesOffhand(sk) && Main() != nullptr && Off() != nullptr && MainAllowsOffhand(); }
    std::string WClassOf(const Weapon* w, bool forceOneHand) const {
        if (w == nullptr) return "hth";
        if (forceOneHand) return w->wclass.empty() ? "hth" : w->wclass;
        if (w->twoHanded && w->oneOrTwo && Off() != nullptr && MainAllowsOffhand()) return w->wclass.empty() ? "hth" : w->wclass;
        const std::string& wc = w->twoHanded ? w->wclass2 : w->wclass;
        return wc.empty() ? "hth" : wc;
    }
    std::string SeqClass(const std::string& sk) const {
        if (sk == "chargeup" && DualActive(sk) && IsClaw(Main()) && IsClaw(Off())) return "ht2";
        return WClassOf(Main(), false);
    }

    const Anim3* A1(const ClassData& c, const std::string& wc) const {
        const auto found = c.a1.find(wc);
        return found == c.a1.end() ? nullptr : &found->second;
    }

    Anim WwAnim(const ClassData& c, const Weapon* w, bool shifted) const {
        Anim a;
        a.ww = true;
        a.shifted = shifted;
        const std::string wc = WClassOf(w, true);
        const Anim3* rec = A1(c, wc);
        a.rec = rec != nullptr;
        if (rec) { a.fpd = rec->fpd; a.spd = rec->spd; a.af = rec->af; }
        a.label = TF("{0} \xC2\xB7 {1} attack frame", {c.name, Upper(wc)});
        return a;
    }

    // false: no animation for this class and skill
    bool AnimFor(const std::string& sk, Anim& a) const {
        const FormData* f = Form();
        const ClassData& c = Cls();
        const Weapon* w = Main();
        if (const SeqDef* def = FindSeq(sk)) {
            const SeqSlot& slot = SeqSlotOf(*def, SeqClass(sk));
            a = Anim{};
            a.seq = def;
            a.steps = slot.steps;
            a.hits = slot.hit2 >= 0 ? 2 : 1;
            a.shifted = f != nullptr;
            a.xbow = sk == "mirrored" && IsCrossbow(w);
            a.label = c.name + " \xC2\xB7 " + std::to_string(slot.steps) + "-step sequence";
            return true;
        }
        if (sk == "ww") { a = WwAnim(c, w, f != nullptr); return true; }
        const Anim3* base = A1(c, WClassOf(w, false));
        if (base == nullptr) base = A1(c, "hth");
        if (f != nullptr) {
            a = Anim{};
            a.fpd = base->fpd; a.spd = base->spd; a.af = base->af;
            a.shifted = true;
            a.formFpd = f->a1.fpd;
            a.label = f->name + " \xC2\xB7 " + Upper(WClassOf(w, false)) + " timing";
            return true;
        }
        a = Anim{};
        if (sk == "kick") {
            const Anim3 k = c.kk ? *c.kk : *base;
            a.fpd = k.fpd; a.spd = k.spd; a.af = k.af;
            a.label = c.name + " kick";
            return true;
        }
        if (sk == "traps") {
            if (!c.s2) return false;
            a.fpd = c.s2->fpd; a.spd = c.s2->spd; a.af = c.s2->af;
            a.label = c.name + " trap";
            return true;
        }
        if (sk == "smite") {
            if (!c.s1) return false;
            a.fpd = c.s1->fpd; a.spd = c.s1->spd; a.af = c.s1->af;
            a.label = c.name + " smite";
            return true;
        }
        a.fpd = base->fpd; a.spd = base->spd; a.af = base->af;
        a.xbow = IsCrossbow(w);
        a.label = c.name + " \xC2\xB7 " + Upper(WClassOf(w, false));
        return true;
    }

    Opt SkillFrames(const std::string& sk, const Anim& anim, LL gearIas, LL flat, LL wsm) const {
        if (anim.ww) {
            const Opt i = WwRaw(anim, gearIas, flat, wsm);
            if (!i) return std::nullopt;
            return std::max<LL>(1, *i);
        }
        const Opt d = Diminish(gearIas);
        if (!d) return std::nullopt;
        const LL e = ClampEias(*d + flat - wsm - (anim.seq ? K.seqPenalty : 0), anim.shifted);
        if (anim.seq) return SeqFramesAt(anim.steps, anim.xbow ? XbowSeqRate(anim.steps) : RateOf(256, e));
        if (sk == "strafe") {
            const LL start = FloorDiv(static_cast<LL>(anim.af) * (100 - K.strafeRollback), 100);
            const LL span = std::max<LL>(1, anim.fpd - start);
            const LL r = anim.xbow ? XbowRate(anim.fpd) : RateOf(anim.spd, e);
            if (r <= 0) return std::nullopt;
            return std::max<LL>(1, CeilDiv(256 * span, r) - 1);
        }
        if (anim.xbow) return std::max<LL>(1, CeilDiv(256 * anim.fpd, XbowRate(anim.fpd)) - 1);
        if (anim.formFpd) return FormFrames(anim.fpd, anim.spd, anim.formFpd, e);
        return FramesFor(anim.fpd, anim.spd, e);
    }

    Opt DualSeqFrames(const Anim& anim, LL offGear, LL mIas, LL oIas, LL flat, LL mAr, LL oAr, LL wsmM, LL wsmO) const {
        const Opt dm = Diminish(offGear + mIas), dO = Diminish(offGear + oIas);
        if (!dm || !dO) return std::nullopt;
        const LL rA = 100 + *dm + flat + mAr - wsmM;
        const LL rB = 100 + *dO + flat + oAr - wsmO;
        const LL e = ClampEias(Trunc(rA + rB, 2) - 100 - K.seqPenalty, anim.shifted);
        return SeqFramesAt(anim.steps, RateOf(256, e));
    }
    Opt DualWwFrames(const Anim& anim, const Anim& offAnim, LL offGear, LL mIas, LL oIas, LL flat, LL mAr, LL oAr, LL wsmM, LL wsmO) const {
        const Opt a = WwRaw(anim, offGear + mIas, flat + mAr, wsmM);
        const Opt b = WwRaw(offAnim, offGear + oIas, flat + oAr, wsmO);
        if (!a || !b) return std::nullopt;
        return std::max<LL>(1, FloorDiv(*a + 1 + *b, 2));
    }

    struct Step { LL stat, f; };
    std::vector<Step> Ladder(const std::function<Opt(LL)>& fn, LL max) const {
        std::vector<Step> out;
        Opt prev;
        for (LL s = 0; s <= max; ++s) {
            const Opt f = fn(s);
            if (!f) continue;
            if (!prev || *f < *prev) { out.push_back({s, *f}); prev = f; }
        }
        return out;
    }

    Table MakeTable(const std::vector<Step>& rows, LL statNow, const std::string& unit, const std::string& extraNote, LL hits,
                    bool fixed) const {
        Table table;
        if (rows.empty()) {
            table.note = extraNote;
            return table;
        }
        long hereIdx = -1;
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (statNow >= rows[i].stat) hereIdx = static_cast<long>(i);
        const long nextIdx = hereIdx + 1 < static_cast<long>(rows.size()) ? hereIdx + 1 : -1;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            TableRow row;
            row.stat = static_cast<int>(rows[i].stat);
            row.frames = static_cast<int>(rows[i].f);
            const long at = static_cast<long>(i);
            row.state = at == hereIdx ? 2 : at == nextIdx ? 3 : at <= hereIdx ? 1 : 0;
            row.gap = at == hereIdx ? T("you are here") : at > hereIdx ? "+" + std::to_string(rows[i].stat - statNow) + " " + unit : "\xE2\x80\x94";
            row.perSecond = Fixed2(Fps(rows[i].f) * static_cast<double>(hits));
            table.rows.push_back(std::move(row));
        }
        const Step& last = rows.back();
        std::string n;
        if (rows.size() == 1 && fixed) n = T("Attack speed does not change this.");
        else if (hereIdx == static_cast<long>(rows.size()) - 1) n = T("You are at the fastest tier this setup can reach.");
        else if (nextIdx >= 0)
            n = TF("Next tier costs {0}% more {1} and saves a frame. Floor is {2} frames at {3}%.",
                   {std::to_string(rows[static_cast<std::size_t>(nextIdx)].stat - statNow), unit, std::to_string(last.f), std::to_string(last.stat)});
        table.note = (extraNote.empty() ? "" : extraNote + " ") + n;
        return table;
    }
};

Result Engine::Run() {
    Result r;
    r.setup = S;
    const std::string sk = S.skill;
    const ClassData& c = Cls();
    const FormData* f = Form();
    const Weapon* w = Main();
    const LL ias = S.ias, wias = S.wias, ar = S.ar, mar = S.mar, oar = S.oar, oias = S.oias;
    const LL fcr = std::min<LL>(S.fcr, fcrMax);

    const bool isWW = sk == "ww";
    const bool dualNow = DualActive(sk);
    const bool splitHands = dualNow && SkillUsesOffhand(sk);
    const LL atkFlat = ar + mar;
    const LL wsm = w ? w->wsm : 0, wsmO = Off() ? Off()->wsm : 0;

    Anim anim;
    const bool haveAnim = AnimFor(sk, anim);
    std::string note;
    if (isWW) {
        if (K.wwHit)
            note = TF("Whirlwind hits on the frame your normal attack would land, made {0}% longer. Increased attack speed counts in full, diminished as usual, while base attack speed, the weapon speed modifier and slows count at {1}% of their value. The rate stops at {2} whether you are shapeshifted or not, the \xE2\x88\x92" "30 sequence penalty never applies, and no setup goes below {3} frames between hits. Two-handed swords are still timed on their one-handed animation.",
                      {std::to_string(K.wwStretch - 100), std::to_string(K.wwWeight), std::to_string(K.wwMaxRate), std::to_string(K.wwMinFrames)});
        else
            note = T("Whirlwind hits on the frame your normal attack would land. The interval is the attack frame of your attack animation for the weapon, played at your attack speed, so all gear, skills, auras, buffs and slows count, diminished as usual, and the \xE2\x88\x92" "30 sequence penalty never applies. Two-handed swords are still timed on their one-handed animation.");
        if (dualNow)
            note += " " + T("With a weapon in each hand each hand is timed on its own weapon and the two intervals are averaged, rounding up. Each strike lands two hits in the same frame, one per weapon, and Eastern Sun lets both hit the same enemy, so there are twice as many hits as strikes.");
    } else if (sk == "strafe" && haveAnim) {
        const LL start = FloorDiv(static_cast<LL>(anim.af) * (100 - K.strafeRollback), 100);
        note = TF("Only the first arrow plays the whole animation. Each shot restarts it at {0} of {1} frames, {2}% of the release frame, so a repeat replays {3} animation frames instead of {1}.",
                  {std::to_string(start), std::to_string(anim.fpd), std::to_string(100 - K.strafeRollback), std::to_string(std::max<LL>(1, anim.fpd - start))});
    } else if (sk == "traps") {
        note = T("Laying a trap plays the throw animation and scales with attack speed.");
    } else if (haveAnim && anim.seq) {
        note = T("This skill runs a fixed sequence compiled into the game, not a plain animation. Sequences run in animation mode 18, which the engine docks a flat 30 speed, and a sequence takes one frame longer than a plain animation of the same length.") +
               " " + T(anim.seq->note) + " " +
               (anim.hits > 1 ? TF("Frame counts below are for the whole cycle, which lands {0} hits.", {std::to_string(anim.hits)})
                              : T("Frame counts below are for the whole cycle."));
    }
    if (haveAnim && anim.xbow)
        note += (note.empty() ? "" : " ") +
                (anim.seq ? TF("Eastern Sun fixes crossbow attacks at {0} frames whatever the attack speed, for the whole attack. Items that add or remove crossbow attack frames move it directly.", {std::to_string(K.xbowFrames)})
                          : TF("Eastern Sun fixes crossbow attacks at {0} frames whatever the attack speed. Items that add or remove crossbow attack frames move it directly.", {std::to_string(K.xbowFrames)}));
    if (sk == "frenzy" && haveAnim)
        note += (note.empty() ? "" : " ") + T("Eastern Sun\xE2\x80\x99s Frenzy frames bonus takes one frame off each of the two swings for every point, two off this cycle, down to 3 frames, the shortest cycle where both hits still land. A negative bonus adds two frames per point. The table shows the cycle without it.");

    r.tableTitleStat = T("IAS needed");
    r.tableTitlePerSecond = T("Attacks / sec");
    r.attackLabel = T("frames per attack");
    r.attackPerSecondLabel = T("attacks / sec");
    if (!haveAnim) {
        r.hasAnim = false;
        r.anim = "\xE2\x80\x94";
        r.attackFrames = r.attackPerSecond = "\xE2\x80\x94";
        r.attack = MakeTable({}, ias, "IAS", T("No animation for this class."), 1, false);
    } else {
        r.hasAnim = true;
        Opt cur;
        std::vector<Step> rows;
        std::string eTxt;
        bool eWarn = false;
        if (splitHands) {
            Anim offAnim;
            if (isWW) offAnim = WwAnim(c, Off(), f != nullptr);
            const auto dual = [&](LL q) -> Opt {
                return isWW ? DualWwFrames(anim, offAnim, ias, q, oias, ar, mar, oar, wsm, wsmO)
                            : DualSeqFrames(anim, ias, q, oias, ar, mar, oar, wsm, wsmO);
            };
            cur = dual(wias);
            rows = Ladder(dual, 2000);
            const Opt dm = Diminish(ias + wias), dO = Diminish(ias + oias);
            if (!dm || !dO) { eTxt = T("game crashes (\xE2\x88\x92" "120 IAS)"); eWarn = true; }
            else if (isWW) {
                const WwRate wm = WwRateOf(*dm, ar + mar, wsm, anim.shifted), wo = WwRateOf(*dO, ar + oar, wsmO, anim.shifted);
                eTxt = TF("main {0}, off {1}", {Signed(wm.rate - 100), Signed(wo.rate - 100)}) +
                       ((wm.rate != wm.raw || wo.rate != wo.raw) ? "  " + T("(clamped)") : "");
                eWarn = wm.rate != wm.raw || wo.rate != wo.raw;
            } else {
                const LL avg = Trunc((100 + *dm + ar + mar - wsm) + (100 + *dO + ar + oar - wsmO), 2) - 100 - K.seqPenalty;
                const LL ca = ClampEias(avg, anim.shifted);
                eTxt = Signed(ca) + "  " + T("(hands averaged)") + (ca != avg ? "  " + T("(clamped)") : "");
                eWarn = ca != avg;
            }
        } else {
            cur = SkillFrames(sk, anim, ias, atkFlat, wsm);
            rows = Ladder([&](LL q) { return SkillFrames(sk, anim, q, atkFlat, wsm); }, 2000);
            const Opt d = Diminish(ias);
            if (!d) { eTxt = T("game crashes (\xE2\x88\x92" "120 IAS)"); eWarn = true; }
            else if (isWW) {
                const WwRate wr = WwRateOf(*d, atkFlat, wsm, anim.shifted);
                eTxt = Signed(wr.rate - 100) + (wr.rate != wr.raw ? "  " + T("(clamped)") : "");
                eWarn = wr.rate != wr.raw;
            } else {
                const LL eRaw = *d + atkFlat - wsm - (anim.seq ? K.seqPenalty : 0);
                const LL e = ClampEias(eRaw, anim.shifted);
                eTxt = Signed(e) + (e != eRaw ? "  " + T("(clamped)") : "");
                eWarn = e != eRaw;
            }
        }
        if (anim.xbow) eTxt += "  " + T("(crossbow: fixed)");
        r.anim = anim.label;
        r.baseFrames = anim.seq ? TF("{0} steps @ 256", {std::to_string(anim.steps)})
                     : anim.ww ? (anim.rec ? TF("hits on frame {0} of {1} @ {2}", {std::to_string(anim.af), std::to_string(anim.fpd), std::to_string(anim.spd)})
                                           : TF("no animation row, flat {0}", {std::to_string(K.wwNoAnim)}))
                     : std::to_string(anim.fpd) + " @ " + std::to_string(anim.spd) + (anim.af ? "  " + TF("(hits f{0})", {std::to_string(anim.af)}) : "");
        r.speedModifier = splitHands ? TF("main {0}, off {1}", {Signed(wsm), Signed(wsmO)}) : Signed(wsm);
        r.effectiveIas = eTxt;
        r.effectiveWarn = eWarn;
        const LL hits = anim.seq ? anim.hits : (isWW && dualNow ? 2 : 1);
        if (dualNow && w->twoHanded && w->oneOrTwo)
            note += " " + T("Held in one hand, so it uses the one-handed animation rather than the two-handed one.");
        r.attackFrames = cur ? std::to_string(*cur) : "\xE2\x80\x94";
        r.attackSolo = (isWW && hits > 1) ? TF("{0} hits per strike", {std::to_string(hits)}) : "";
        r.attackPerSecond = cur ? Fixed2(Fps(*cur) * static_cast<double>(hits)) : "\xE2\x80\x94";
        r.attackLabel = T((anim.seq && hits > 1) ? "frames per cycle" : isWW ? (hits > 1 ? "frames between strikes" : "frames between hits") : "frames per attack");
        r.attackPerSecondLabel = T(hits > 1 ? "hits / sec" : "attacks / sec");
        r.tableTitlePerSecond = T(hits > 1 ? "Hits / sec" : "Attacks / sec");
        r.attack = MakeTable(rows, splitHands ? wias : ias, splitHands ? T("main-hand IAS") : std::string("IAS"), note, hits, anim.xbow || (anim.ww && !anim.rec));
    }

    // cast
    const Anim3 ca = f ? f->sc : *c.sc;
    const Opt cf = CastFrames(ca.fpd, ca.spd, fcr);
    r.castFrames = cf ? std::to_string(*cf) : "\xE2\x80\x94";
    r.castPerSecond = cf ? Fixed2(Fps(*cf)) : "\xE2\x80\x94";
    std::string capNote = TF("Faster cast rate is capped at {0}, and the rate it allows is already reached at {1}.", {std::to_string(fcrMax), std::to_string(fcrReached)});
    if (!Diminish(fcr)) capNote += " " + T("At exactly \xE2\x88\x92" "120 the game divides by zero and crashes.");
    else if (CastWraps(fcr)) capNote += " " + T("Below \xE2\x88\x92" "54 the game\xE2\x80\x99s arithmetic wraps around and the cast becomes instant.");
    r.cast = MakeTable(Ladder([&](LL q) { return CastFrames(ca.fpd, ca.spd, q); }, fcrMax), fcr, "FCR",
                       T(f ? "Casting from a form uses the form\xE2\x80\x99s own animation."
                           : "Every class shares this cast animation, so this table is the same for all of them outside a form.") + " " + capNote,
                       1, false);

    // what the page shows
    r.showMainHandIas = splitHands;
    r.showOffHandFields = splitHands;
    r.iasHint = T(splitHands ? "Everything except the two weapons: rings, armour, helm, gloves."
                             : "Everything you wear, weapon included. Pooled and diminished together.");
    r.mainHandIasHint = T("Increased attack speed on the main-hand weapon, sockets included.");
    if (splitHands) r.tableTitleStat = T("Main-hand IAS needed");
    r.totalIas = splitHands ? TF("main hand {0}, off hand {1}", {std::to_string(ias + wias), std::to_string(ias + oias)}) : std::to_string(ias);
    r.flatSpeed = splitHands ? TF("main {0}, off {1}", {Signed(ar + mar), Signed(ar + oar)}) : Signed(atkFlat);

    const bool skillDual = SkillUsesOffhand(sk);
    const bool canDual = MainAllowsOffhand();
    r.offhandLive = skillDual && canDual;
    r.offhandHint = T(!skillDual ? "Only Whirlwind, Frenzy and charge-up strikes use both hands."
                  : !w ? "Pick a main hand first."
                  : !canDual
                      ? (c.name == "Assassin" ? "The Assassin dual wields claws only."
                         : c.name == "Barbarian" ? "The Barbarian can pair any one-handed weapon, and two-handed swords."
                                                 : "Only the Barbarian and the Assassin dual wield.")
                  : !Off() ? "Leave empty to swing one weapon."
                  : isWW ? "Each hand is timed on its own weapon, the two intervals averaged rounding up. Each strike lands one hit per weapon."
                         : "Each hand is read on its own weapon\xE2\x80\x99s stats and the two rates are averaged.");
    r.formHint = f && isWW && K.wwHit
        ? TF("Whirlwind is timed on your weapon\xE2\x80\x99s human animation, with its own speed ceiling of {0} whether you are shapeshifted or not.", {std::to_string(K.wwMaxRate)})
        : f ? T("Attack timing follows your weapon\xE2\x80\x99s human animation: the engine rescales the form\xE2\x80\x99s animation to it, within a frame\xE2\x80\x99s rounding. Shapeshifted also raises the speed ceiling from 175 to 250.")
            : std::string();
    return r;
}

}  // namespace

bool Parse(const std::string& pageHtml, const std::string& dataJs, Data& out, std::string& error) {
    Data data;
    struct Named { const char* name; int* target; };
    int wwHit = 1;
    const Named constants[] = {{"FCR_CAP", &data.k.fcrCap}, {"WW_HIT_INTERVAL", &wwHit}, {"WW_ATTACKRATE_WEIGHT", &data.k.wwWeight},
                               {"WW_STRETCH", &data.k.wwStretch}, {"WW_MIN_FRAMES", &data.k.wwMinFrames}, {"WW_MAX_RATE", &data.k.wwMaxRate},
                               {"WW_MIN_RATE", &data.k.wwMinRate}, {"WW_NO_ANIM", &data.k.wwNoAnim}, {"XBOW_FRAMES", &data.k.xbowFrames},
                               {"STRAFE_ROLLBACK", &data.k.strafeRollback}, {"SEQ_PENALTY", &data.k.seqPenalty}};
    for (const Named& constant : constants) {
        if (!Constant(pageHtml, constant.name, *constant.target)) {
            error = std::string("the page no longer defines ") + constant.name;
            return false;
        }
    }
    data.k.wwHit = wwHit != 0;
    if (data.k.fcrCap <= 0 || data.k.fcrCap > 100000 || data.k.xbowFrames <= 0 || data.k.wwMinRate < 0) {
        error = "the page's constants look wrong";
        return false;
    }
    const std::size_t marker = dataJs.find("__DATA__");
    std::size_t at = marker == std::string::npos ? std::string::npos : dataJs.find('{', marker);
    Json root;
    if (at == std::string::npos || !JsonReader(dataJs).Read(at, root) || root.kind != Json::Object) {
        error = "the data file is incomplete";
        return false;
    }
    const Json* classes = root.Find("c");
    const Json* forms = root.Find("f");
    const Json* weapons = root.Find("w");
    if (!classes || !forms || !weapons || classes->kind != Json::Array || forms->kind != Json::Array || weapons->kind != Json::Array) {
        error = "the data file has no classes, forms or weapons";
        return false;
    }
    for (const Json& node : classes->items) {
        ClassData c;
        const Json* name = node.Find("n");
        const Json* a1 = node.Find("a1");
        if (!name || name->kind != Json::String || !a1 || a1->kind != Json::Object || !ReadOptionalAnim(node.Find("sc"), c.sc) || !c.sc) {
            error = "a class in the data file is malformed";
            return false;
        }
        c.name = name->text;
        for (const auto& member : a1->members) {
            Anim3 anim;
            if (!ReadAnim(&member.second, anim)) { error = "an attack animation in the data file is malformed"; return false; }
            c.a1[member.first] = anim;
        }
        if (c.a1.find("hth") == c.a1.end()) { error = "a class has no hand-to-hand animation"; return false; }
        if (!ReadOptionalAnim(node.Find("kk"), c.kk) || !ReadOptionalAnim(node.Find("s1"), c.s1) || !ReadOptionalAnim(node.Find("s2"), c.s2)) {
            error = "a class animation in the data file is malformed";
            return false;
        }
        data.classes.push_back(std::move(c));
    }
    for (const Json& node : forms->items) {
        FormData f;
        const Json* name = node.Find("n");
        if (!name || name->kind != Json::String || !ReadAnim(node.Find("a1"), f.a1) || !ReadAnim(node.Find("sc"), f.sc)) {
            error = "a form in the data file is malformed";
            return false;
        }
        f.name = name->text;
        data.forms.push_back(std::move(f));
    }
    for (const Json& node : weapons->items) {
        if (node.kind != Json::Array || node.items.size() < 8) { error = "a weapon in the data file is malformed"; return false; }
        const auto& v = node.items;
        Weapon w;
        w.name = v[0].text;
        w.wsm = static_cast<int>(v[1].number);
        w.wclass = v[2].text;
        w.wclass2 = v[3].text;
        w.twoHanded = v[4].number != 0;
        w.typeCode = v[5].text;
        w.typeName = v[6].text;
        w.oneOrTwo = v[7].number != 0;
        data.weapons.push_back(std::move(w));
    }
    if (data.classes.size() < 5 || data.weapons.size() < 100 || data.forms.empty()) {
        error = "the data file has too little in it (" + std::to_string(data.classes.size()) + " classes, " +
                std::to_string(data.weapons.size()) + " weapons); the website pages look broken";
        return false;
    }
    out = std::move(data);
    return true;
}

bool SkillUsesOffhand(const std::string& skill) { return skill == "frenzy" || skill == "chargeup" || skill == "ww"; }

bool SkillAllowed(const Data& data, const Setup& setup, const std::string& skill) {
    const std::string& cls = data.classes[static_cast<std::size_t>(setup.cls)].name;
    if (skill == "traps") return cls == "Assassin";
    if (skill == "smite") return cls == "Paladin";
    if (skill == "chargeup") return cls == "Assassin";
    if (skill == "cleave" || skill == "mirrored") return cls == "Warlock";
    if (skill == "frenzy") return cls == "Assassin" || cls == "Barbarian";
    return true;
}

bool CanOffhand(const Data& data, const Setup& setup, const Weapon& weapon) {
    const std::string& cls = data.classes[static_cast<std::size_t>(setup.cls)].name;
    if (cls == "Assassin") return weapon.wclass == "ht1" || weapon.wclass == "ht2";
    if (cls != "Barbarian") return false;
    return !weapon.twoHanded || weapon.oneOrTwo;
}

void Normalise(const Data& data, Setup& setup) {
    if (setup.cls < 0 || static_cast<std::size_t>(setup.cls) >= data.classes.size()) setup.cls = 0;
    if (setup.form >= static_cast<int>(data.forms.size())) setup.form = -1;
    if (setup.wep >= static_cast<int>(data.weapons.size())) setup.wep = -1;
    if (!SkillAllowed(data, setup, setup.skill)) setup.skill = "standard";
    bool known = false;
    for (const SkillChoice& choice : kSkills) known = known || setup.skill == choice.id;
    if (!known) setup.skill = "standard";
    const bool live = SkillUsesOffhand(setup.skill) && setup.wep >= 0 &&
                      CanOffhand(data, setup, data.weapons[static_cast<std::size_t>(setup.wep)]);
    if (!live || setup.wep2 >= static_cast<int>(data.weapons.size())) setup.wep2 = -1;
    else if (setup.wep2 >= 0 && !CanOffhand(data, setup, data.weapons[static_cast<std::size_t>(setup.wep2)])) setup.wep2 = -1;
}

Result Compute(const Data& data, Setup setup) {
    Normalise(data, setup);
    Result result = Engine(data, setup).Run();
    result.setup = setup;
    result.setup.fcr = setup.fcr;
    return result;
}

void SetTranslator(Translator translator) { gTranslator = translator; }

}  // namespace esrwiki::ias
