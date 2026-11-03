#include "locale_align.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <cctype>

namespace esrwiki::localize {
namespace {

bool IsAsciiSpace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

void AppendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// The entities the website pages use, and the common ones besides. Anything else stays as written.
std::string DecodeEntities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    static const std::pair<const char*, unsigned long> kNamed[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE},
        {"deg", 0xB0}, {"plusmn", 0xB1}, {"middot", 0xB7}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"times", 0xD7},
        {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
        {"bull", 0x2022}, {"hellip", 0x2026}};
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        const std::size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) { out += s[i]; continue; }
        const std::string name = s.substr(i + 1, semi - i - 1);
        unsigned long cp = 0;
        bool ok = false;
        if (!name.empty() && name[0] == '#') {
            const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            const std::string digits = name.substr(hex ? 2 : 1);
            ok = !digits.empty() && std::all_of(digits.begin(), digits.end(), [&](unsigned char c) { return hex ? std::isxdigit(c) != 0 : std::isdigit(c) != 0; });
            if (ok) cp = std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10);
            ok = ok && cp > 0 && cp <= 0x10FFFF;
        } else {
            for (const auto& entry : kNamed)
                if (name == entry.first) { cp = entry.second; ok = true; break; }
        }
        if (!ok) { out += s[i]; continue; }
        AppendUtf8(out, cp);
        i = semi;
    }
    return out;
}

// White space as Python's \s sees it in a str: ASCII white space and the Unicode spaces (non-breaking included).
std::string CollapseAndTrim(const std::string& s) {
    std::string out;
    bool pendingSpace = false;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        if (IsAsciiSpace(c)) len = 1;
        else if (c == 0xC2 && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) == 0xA0 || static_cast<unsigned char>(s[i + 1]) == 0x85)) len = 2;
        else if (c == 0xE2 && i + 2 < s.size()) {
            const unsigned char b = static_cast<unsigned char>(s[i + 1]), d = static_cast<unsigned char>(s[i + 2]);
            if (b == 0x80 && ((d >= 0x80 && d <= 0x8A) || d == 0xA8 || d == 0xA9 || d == 0xAF)) len = 3;
            else if (b == 0x81 && d == 0x9F) len = 3;
        } else if (c == 0xE3 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 && static_cast<unsigned char>(s[i + 2]) == 0x80) len = 3;
        else if (c == 0xE1 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x9A && static_cast<unsigned char>(s[i + 2]) == 0x80) len = 3;
        if (len > 0) { pendingSpace = !out.empty(); i += len; continue; }
        if (pendingSpace) { out += ' '; pendingSpace = false; }
        out += s[i++];
    }
    return out;
}

std::string Skeleton(const std::string& text) {
    std::string out;
    for (const char ch : text)
        if (std::isdigit(static_cast<unsigned char>(ch))) out += ch;  // digits only: languages differ in the signs and spacing around them
    return out;
}

bool IsVoid(const std::string& tag) {
    static const char* kVoid[] = {"br", "img", "input", "meta", "link", "hr", "col", "area", "base", "wbr"};
    for (const char* v : kVoid)
        if (tag == v) return true;
    return false;
}

class Extractor {
public:
    std::vector<TextNode> nodes;
    void Run(const std::string& h) {
        const std::size_t n = h.size();
        std::size_t i = 0;
        while (i < n) {
            const std::size_t lt = h.find('<', i);
            if (lt == std::string::npos) { Text(h.substr(i)); break; }
            if (lt > i) Text(h.substr(i, lt - i));
            i = lt;
            const char next = i + 1 < n ? h[i + 1] : '\0';
            if (std::isalpha(static_cast<unsigned char>(next))) { i = StartTag(h, i); continue; }
            if (next == '/') {
                const std::size_t gt = h.find('>', i);
                if (gt == std::string::npos) { Text(h.substr(i)); break; }
                std::size_t k = i + 2;
                std::string name;
                while (k < gt && !IsAsciiSpace(static_cast<unsigned char>(h[k])) && h[k] != '/') name += static_cast<char>(std::tolower(static_cast<unsigned char>(h[k++])));
                if (!name.empty()) EndTag(name);
                i = gt + 1;
                continue;
            }
            if (h.compare(i, 4, "<!--") == 0) {
                const std::size_t end = h.find("-->", i + 4);
                if (end == std::string::npos) break;
                i = end + 3;
                continue;
            }
            if (next == '!' || next == '?') {
                const std::size_t gt = h.find('>', i);
                if (gt == std::string::npos) break;
                i = gt + 1;
                continue;
            }
            Text("<");
            ++i;
        }
    }

private:
    std::vector<std::string> stack_;
    int skip_ = 0;
    void Text(const std::string& raw) {
        if (skip_ > 0) return;
        const std::string t = CollapseAndTrim(DecodeEntities(raw));
        if (t.empty()) return;
        std::string path;
        const std::size_t from = stack_.size() > 3 ? stack_.size() - 3 : 0;
        for (std::size_t k = from; k < stack_.size(); ++k) path += (k > from ? "/" : "") + stack_[k];
        nodes.push_back({t, path + "|" + Skeleton(t)});
    }
    void EndTag(const std::string& tag) {
        if (!IsVoid(tag)) {
            const std::string prefix = tag + ".";
            while (!stack_.empty() && stack_.back().compare(0, prefix.size(), prefix) != 0) stack_.pop_back();
            if (!stack_.empty()) stack_.pop_back();
        }
        if ((tag == "script" || tag == "style") && skip_ > 0) --skip_;
    }
    std::size_t StartTag(const std::string& h, std::size_t i) {
        const std::size_t n = h.size();
        std::size_t k = i + 1;
        std::string name;
        while (k < n && !IsAsciiSpace(static_cast<unsigned char>(h[k])) && h[k] != '>' && h[k] != '/') name += static_cast<char>(std::tolower(static_cast<unsigned char>(h[k++])));
        std::string cls;
        bool selfClose = false;
        while (k < n) {
            while (k < n && (IsAsciiSpace(static_cast<unsigned char>(h[k])))) ++k;
            if (k >= n) break;
            if (h[k] == '>') { ++k; break; }
            if (h[k] == '/') {
                if (k + 1 < n && h[k + 1] == '>') { selfClose = true; k += 2; break; }
                ++k;
                continue;
            }
            std::string attr;
            while (k < n && !IsAsciiSpace(static_cast<unsigned char>(h[k])) && h[k] != '=' && h[k] != '>' && !(h[k] == '/' && k + 1 < n && h[k + 1] == '>'))
                attr += static_cast<char>(std::tolower(static_cast<unsigned char>(h[k++])));
            std::string value;
            std::size_t save = k;
            while (save < n && IsAsciiSpace(static_cast<unsigned char>(h[save]))) ++save;
            if (save < n && h[save] == '=') {
                k = save + 1;
                while (k < n && IsAsciiSpace(static_cast<unsigned char>(h[k]))) ++k;
                if (k < n && (h[k] == '"' || h[k] == '\'')) {
                    const char q = h[k++];
                    const std::size_t end = h.find(q, k);
                    value = h.substr(k, end == std::string::npos ? std::string::npos : end - k);
                    k = end == std::string::npos ? n : end + 1;
                } else {
                    while (k < n && !IsAsciiSpace(static_cast<unsigned char>(h[k])) && h[k] != '>') value += h[k++];
                }
                value = DecodeEntities(value);
            }
            if (attr == "class") cls = value;
        }
        if (!IsVoid(name)) {
            std::string dotted = cls;
            std::replace(dotted.begin(), dotted.end(), ' ', '.');
            stack_.push_back(name + "." + dotted);
        }
        if (name == "script" || name == "style") {
            ++skip_;
            if (!selfClose) {  // raw text up to the closing tag
                std::string lower;
                const std::string close = "</" + name;
                std::size_t at = k;
                for (; at < n; ++at) {
                    if (h[at] == '<' && at + close.size() <= n) {
                        bool same = true;
                        for (std::size_t q = 0; q < close.size(); ++q)
                            if (std::tolower(static_cast<unsigned char>(h[at + q])) != close[q]) { same = false; break; }
                        if (same) break;
                    }
                }
                k = at;
            }
        }
        if (selfClose) EndTag(name);
        return k;
    }
};

bool HasWord(const std::string& s) {  // three ASCII letters in a row
    int run = 0;
    for (const char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { if (++run >= 3) return true; }
        else run = 0;
    }
    return false;
}

bool IsColorCode(const std::string& s, std::size_t at) {  // 0xC3 0xBF 'c' <one more byte>
    return at + 3 < s.size() && static_cast<unsigned char>(s[at]) == 0xC3 && static_cast<unsigned char>(s[at + 1]) == 0xBF && s[at + 2] == 'c';
}

std::string StripCodes(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (IsColorCode(s, i)) { i += 4; continue; }
        out += s[i++];
    }
    return out;
}

std::vector<std::string> SplitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const std::size_t at = s.find(sep, from);
        if (at == std::string::npos) { out.push_back(s.substr(from)); return out; }
        out.push_back(s.substr(from, at - from));
        from = at + 1;
    }
}

// Numbers, and ranges of numbers written "5-10" or "(5 to 10)" (parentheses included), each become one '#'. A fixed
// value and a range then have the same shape, so "+15% Enhanced Damage" on the website also translates "+200-300% ...".
std::string ReplaceNumbers(const std::string& s, std::vector<std::string>* numbers) {
    const auto digit = [&](std::size_t at) { return at < s.size() && std::isdigit(static_cast<unsigned char>(s[at])) != 0; };
    const auto readNumber = [&](std::size_t at) {  // digits with an optional decimal part
        while (digit(at)) ++at;
        if (at + 1 < s.size() && s[at] == '.' && digit(at + 1)) {
            ++at;
            while (digit(at)) ++at;
        }
        return at;
    };
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const bool open = s[i] == '(' && digit(i + 1);
        if (!open && !digit(i)) { out += s[i++]; continue; }
        std::size_t k = readNumber(open ? i + 1 : i);
        if (k + 3 < s.size() && s.compare(k, 4, " to ") == 0 && digit(k + 4)) k = readNumber(k + 4);
        else if (k + 1 < s.size() && s[k] == '-' && digit(k + 1)) k = readNumber(k + 1);
        else if (open && k < s.size() && s[k] == ' ') {  // "(50 to 75)": the word between the numbers is the language's own ("à", "bis", ...)
            std::size_t w = k + 1;
            while (w < s.size() && s[w] != ' ' && s[w] != '(' && s[w] != ')' && !digit(w)) ++w;
            if (w > k + 1 && w - (k + 1) <= 6 && w + 1 < s.size() && s[w] == ' ' && digit(w + 1)) k = readNumber(w + 1);
        }
        if (open) {
            if (k < s.size() && s[k] == ')') ++k;
            else { out += s[i++]; continue; }  // "(" that does not close around the number
        }
        if (numbers != nullptr) numbers->push_back(s.substr(i, k - i));
        out += '#';
        i = k;
    }
    return out;
}

bool HasLetter(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); });
}
unsigned PieceIndex(const std::string& s) {
    return (static_cast<unsigned>(static_cast<unsigned char>(s[0])) << 8) | static_cast<unsigned char>(s[1]);
}
bool WordByte(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || static_cast<unsigned char>(c) >= 0x80; }
bool Letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

}  // namespace

std::vector<TextNode> ExtractTextNodes(const std::string& html) {
    Extractor extractor;
    extractor.Run(html);
    return std::move(extractor.nodes);
}

namespace {
// Localized pages are not always in the English order (sets and runewords are sorted by the localized names), so a
// sequential walk loses its place. Windows of consecutive nodes whose tag paths and numbers form a pattern that
// occurs exactly once in each page tie a block of one page to the same block of the other wherever it sits; each
// such match is then extended forward while the keys keep agreeing.
std::vector<std::pair<std::string, std::string>> AnchoredPairs(const std::vector<TextNode>& a, const std::vector<TextNode>& b) {
    constexpr std::size_t kAnchor = 6;
    const auto window = [&](const std::vector<TextNode>& v, std::size_t i) {
        std::string w;
        for (std::size_t k = 0; k < kAnchor; ++k) w += v[i + k].key + "\x1f";
        return w;
    };
    std::unordered_map<std::string, std::vector<std::size_t>> inA, inB;
    for (std::size_t i = 0; i + kAnchor <= a.size(); ++i) inA[window(a, i)].push_back(i);
    for (std::size_t j = 0; j + kAnchor <= b.size(); ++j) inB[window(b, j)].push_back(j);
    std::vector<char> usedA(a.size(), 0), usedB(b.size(), 0);
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::vector<std::size_t> match(a.size(), kNone);
    for (std::size_t i = 0; i + kAnchor <= a.size(); ++i) {
        if (usedA[i]) continue;
        const std::string w = window(a, i);
        const auto la = inA.find(w);
        const auto lb = inB.find(w);
        if (la == inA.end() || lb == inB.end() || la->second.size() != 1 || lb->second.size() != 1) continue;
        std::size_t j = lb->second[0], k = i;
        while (k < a.size() && j < b.size() && !usedA[k] && !usedB[j] && a[k].key == b[j].key) {
            match[k] = j;
            usedA[k] = usedB[j] = 1;
            ++k;
            ++j;
        }
    }
    // What lies between two matches that keep the same order in both pages is aligned on its own.
    std::vector<std::pair<std::string, std::string>> pairs;
    std::size_t prevA = kNone, prevB = kNone;  // the last matched node, as an index +1 (0 = start of the page)
    std::size_t endA = 0, endB = 0;
    const auto fill = [&](std::size_t fromA, std::size_t toA, std::size_t fromB, std::size_t toB) {
        if (fromA >= toA || fromB >= toB) return;
        const std::vector<TextNode> subA(a.begin() + static_cast<std::ptrdiff_t>(fromA), a.begin() + static_cast<std::ptrdiff_t>(toA));
        const std::vector<TextNode> subB(b.begin() + static_cast<std::ptrdiff_t>(fromB), b.begin() + static_cast<std::ptrdiff_t>(toB));
        for (auto& p : AlignNodesSequential(subA, subB)) pairs.push_back(std::move(p));
    };
    (void)prevA; (void)prevB;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (match[i] == kNone) continue;
        if (match[i] >= endB) fill(endA, i, endB, match[i]);  // only when the other page moves forward too
        pairs.emplace_back(a[i].text, b[match[i]].text);
        endA = i + 1;
        endB = std::max(endB, match[i] + 1);
    }
    fill(endA, a.size(), endB, b.size());
    return pairs;
}
}  // namespace

std::vector<std::pair<std::string, std::string>> AlignNodes(const std::vector<TextNode>& a, const std::vector<TextNode>& b) {
    std::vector<std::pair<std::string, std::string>> pairs;
    if (a.size() == b.size()) {
        // Equal counts do not mean equal order: one row with an extra line here and a missing one there still adds up
        // (the German runewords page), and a plain zip then pairs names with stats from that row on. Zip when
        // the keys agree; otherwise realign, and keep the zip only if the realignment covers far less.
        // A few keys differ on every page for harmless reasons (a number written another way), so the zip is trusted
        // until more than 3% of the keys disagree (French: at most 1.8% on any page; German runewords: 57%).
        // A node agrees when its tag path matches and its numbers match in any order, or when one side has none (Japanese
        // writes "2H" as 両手, and word order moves numbers within a sentence); only a different set of numbers is a
        // disagreement.
        auto agrees = [](const TextNode& x, const TextNode& y) {
            if (x.key == y.key) return true;
            const std::size_t px = x.key.find('|'), py = y.key.find('|');
            if (px == std::string::npos || py == std::string::npos || x.key.compare(0, px, y.key, 0, py) != 0 || px != py) return false;
            std::string sx = x.key.substr(px + 1), sy = y.key.substr(py + 1);
            if (sx.empty() || sy.empty()) return true;
            std::sort(sx.begin(), sx.end());
            std::sort(sy.begin(), sy.end());
            return sx == sy;
        };
        std::size_t disagree = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!agrees(a[i], b[i])) ++disagree;
        if (disagree * 100 > a.size() * 3) {
            auto anchored = AnchoredPairs(a, b);
            std::vector<std::pair<std::string, std::string>> walked = AlignNodesSequential(a, b);
            auto& best = anchored.size() > walked.size() ? anchored : walked;
            if (best.size() * 10 >= a.size() * 9) return best;
        }
        for (std::size_t i = 0; i < a.size(); ++i) pairs.emplace_back(a[i].text, b[i].text);
        return pairs;
    }
    {
        auto anchored = AnchoredPairs(a, b);
        std::vector<std::pair<std::string, std::string>> walked = AlignNodesSequential(a, b);
        return anchored.size() > walked.size() ? anchored : walked;
    }
}

std::vector<std::pair<std::string, std::string>> AlignNodesSequential(const std::vector<TextNode>& a, const std::vector<TextNode>& b) {
    std::vector<std::pair<std::string, std::string>> pairs;
    const std::size_t n = a.size(), m = b.size();
    constexpr std::size_t kWindow = 4, kLook = 60;
    const auto match = [&](std::size_t i, std::size_t j) {
        for (std::size_t k = 0; k < kWindow; ++k)
            if (i + k >= n || j + k >= m || a[i + k].key != b[j + k].key) return false;
        return true;
    };
    std::size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i].key == b[j].key) { pairs.emplace_back(a[i].text, b[j].text); ++i; ++j; continue; }
        bool found = false;
        std::size_t di = 0, dj = 0;
        for (std::size_t d = 1; d < kLook && !found; ++d) {
            const std::size_t tries[3][2] = {{0, d}, {d, 0}, {d, d}};
            for (const auto& t : tries)
                if (match(i + t[0], j + t[1])) { di = t[0]; dj = t[1]; found = true; break; }
        }
        if (!found) { ++i; ++j; continue; }
        if (di == dj)
            for (std::size_t k = 0; k < di; ++k) pairs.emplace_back(a[i + k].text, b[j + k].text);
        i += di;
        j += dj;
    }
    return pairs;
}

void Translator::AddPage(const std::string& englishHtml, const std::string& localizedHtml) {
    std::vector<std::pair<std::string, std::string>> all;
    for (const auto& pair : AlignNodes(ExtractTextNodes(englishHtml), ExtractTextNodes(localizedHtml))) {
        all.push_back(pair);
        // The builders drop the punctuation the website puts around a text ("- The boss...", ", anointments...",
        // "[Hand to Hand 1]"), so the text without the punctuation both sides share is a pair as well.
        std::string e = pair.first, l = pair.second;
        const auto lead = [](char ch) { return ch == '-' || ch == ',' || ch == '.' || ch == ';' || ch == ':' || ch == '[' || ch == '(' || ch == ' '; };
        const auto tail = [](char ch) { return ch == ']' || ch == ')' || ch == ' '; };
        std::size_t i = 0;
        while (i < e.size() && i < l.size() && e[i] == l[i] && lead(e[i])) ++i;
        std::size_t k = 0;
        while (k + i < e.size() && k + i < l.size() && e[e.size() - 1 - k] == l[l.size() - 1 - k] && tail(e[e.size() - 1 - k])) ++k;
        if (i > 0 || k > 0) {
            const std::string ce = e.substr(i, e.size() - i - k), cl = l.substr(i, l.size() - i - k);
            if (!ce.empty() && !cl.empty() && ce != cl) all.emplace_back(ce, cl);
        }
        // The builders also drop a trailing bracket the website adds to a name: "Amazonian Loop (zrn)", "Rogue's Suit-N (merc
        // only)". The name without it, and without a "-N" style tag both sides carry, is a pair as well.
        {
            const auto cutBracket = [](const std::string& s) {
                if (s.size() < 4 || s.back() != ')') return s;
                const std::size_t open = s.rfind(" (");
                return open == std::string::npos || open == 0 ? s : s.substr(0, open);
            };
            std::string ce = cutBracket(e), cl = cutBracket(l);
            if (ce != e && cl != l) {
                // the difficulty tag ("-N", "-NM", "-H"): at the end of the English name, anywhere in the localized one
                for (const char* tag : {"-NM", "-N", "-H"}) {
                    const std::size_t tl = std::strlen(tag);
                    if (ce.size() <= tl || ce.compare(ce.size() - tl, tl, tag) != 0) continue;
                    std::size_t at = cl.find(tag);
                    while (at != std::string::npos && at + tl < cl.size() && cl[at + tl] != ' ') at = cl.find(tag, at + 1);
                    if (at == std::string::npos) break;
                    ce.resize(ce.size() - tl);
                    cl.erase(at, tl);
                    break;
                }
                if (!ce.empty() && !cl.empty() && ce != cl) all.emplace_back(ce, cl);
            }
        }
        // "(At least 500 kills)" / "(Au moins 500 victimes)": the builders keep only what follows the lead-in words, "500 kills".
        if (e.size() > 2 && l.size() > 2 && e.front() == '(' && l.front() == '(' && e.back() == ')' && l.back() == ')') {
            const auto firstDigit = [](const std::string& s) {
                for (std::size_t p = 0; p < s.size(); ++p)
                    if (std::isdigit(static_cast<unsigned char>(s[p]))) return p;
                return std::string::npos;
            };
            const std::size_t de = firstDigit(e), dl = firstDigit(l);
            if (de != std::string::npos && dl != std::string::npos)
                all.emplace_back(e.substr(de, e.size() - 1 - de), l.substr(dl, l.size() - 1 - dl));
        }
    }
    for (const auto& pair : all) {
        if (pair.first == pair.second) {
            // kept as a known piece: a word the site does not translate ("Suffixes", "Charge") still covers its place in a joined text
            if (pair.first.size() >= 2 && pair.first.size() <= 40 && HasLetter(pair.first)) identity_.insert(pair.first);
            continue;
        }
        auto found = seen_.find(pair.first);
        if (found == seen_.end()) {
            order_.push_back(pair.first);
            found = seen_.emplace(pair.first, Choice{}).first;
        }
        auto& values = found->second.values;
        auto value = std::find_if(values.begin(), values.end(), [&](const auto& v) { return v.first == pair.second; });
        if (value == values.end()) values.emplace_back(pair.second, 1);
        else ++value->second;
    }
}

void Translator::AddFallback(const std::string& english, const std::string& localized) {
    if (english.empty() || localized.empty()) return;
    if (english == localized) {  // a word that stays as it is ("Normal") still covers its place in a joined text
        if (HasLetter(english)) identity_.insert(english);
        return;
    }
    if (seen_.count(english)) return;
    order_.push_back(english);
    seen_[english].values.emplace_back(localized, 0);
    fallback_.insert(english);
}

namespace {
bool HasDigit(const std::string& text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}
std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (char ch : text) {
        if (ch == ' ') { if (!word.empty()) words.push_back(word); word.clear(); }
        else word += ch;
    }
    if (!word.empty()) words.push_back(word);
    return words;
}
std::string JoinWords(const std::vector<std::string>& words, std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t i = from; i < to; ++i) out += (out.empty() ? "" : " ") + words[i];
    return out;
}
}  // namespace

void Translator::DeriveNames() {
    derived_.clear();
    derivedLower_.clear();
    struct Member { std::string name; std::vector<std::string> loc; };
    // Texts that share their first or last word(s) form a group; the rest of each text is the name. A group keyed by one
    // word needs 6 members; one keyed by two words (" just killed") needs 2, and the localized texts must then share at
    // least two words.
    for (std::size_t keyWords = 1; keyWords <= 2; ++keyWords) {
        for (int fromEnd = 0; fromEnd < 2; ++fromEnd) {
            std::map<std::string, std::vector<Member>> groups;
            for (const std::string& english : order_) {
                if (HasDigit(english)) continue;
                const std::vector<std::string> words = SplitWords(english);
                if (words.size() < keyWords + 1 || words.size() > 5) continue;
                const std::string key = fromEnd ? JoinWords(words, words.size() - keyWords, words.size()) : JoinWords(words, 0, keyWords);
                Member member{fromEnd ? JoinWords(words, 0, words.size() - keyWords) : JoinWords(words, keyWords, words.size()), SplitWords(exact_.at(english))};
                groups[key].push_back(std::move(member));
            }
            for (auto& entry : groups) {
                auto& members = entry.second;
                if (members.size() < (keyWords == 1 ? 6u : 2u)) continue;
                // The words the localized texts share: the most common leading (and trailing) words, as long as 80% of the group
                // agree, so a few odd texts do not hide the pattern.
                const auto shared = [&](bool lead) {
                    std::size_t len = 0;
                    for (;; ++len) {
                        std::map<std::string, std::size_t> count;
                        std::size_t eligible = 0;
                        for (const Member& m : members) {
                            if (m.loc.size() < len + 2) continue;
                            ++eligible;
                            std::string gram;
                            for (std::size_t k = 0; k <= len; ++k) gram += (lead ? m.loc[k] : m.loc[m.loc.size() - 1 - k]) + " ";
                            ++count[gram];
                        }
                        std::size_t best = 0;
                        for (const auto& e : count) best = std::max(best, e.second);
                        if (eligible == 0 || best * 10 < members.size() * 8) break;
                    }
                    return len;
                };
                const std::size_t prefix = shared(true), suffix = shared(false);
                if (prefix + suffix == 0 || (keyWords == 2 && prefix + suffix < 2)) continue;
                const auto gramOf = [&](const Member& m, std::size_t len, bool lead) {
                    std::string gram;
                    for (std::size_t k = 0; k < len; ++k) gram += (lead ? m.loc[k] : m.loc[m.loc.size() - 1 - k]) + " ";
                    return gram;
                };
                std::map<std::string, std::size_t> headCount, tailCount;
                for (const Member& m : members) {
                    if (m.loc.size() >= prefix + suffix + 1) { ++headCount[gramOf(m, prefix, true)]; ++tailCount[gramOf(m, suffix, false)]; }
                }
                std::string head, tail;
                std::size_t hb = 0, tb = 0;
                for (const auto& e : headCount) if (e.second > hb) { hb = e.second; head = e.first; }
                for (const auto& e : tailCount) if (e.second > tb) { tb = e.second; tail = e.first; }
                for (const Member& m : members) {
                    if (m.loc.size() < prefix + suffix + 1 || gramOf(m, prefix, true) != head || gramOf(m, suffix, false) != tail) continue;
                    const std::string name = JoinWords(m.loc, prefix, m.loc.size() - suffix);
                    if (name.empty()) continue;
                    std::string fixed = name;  // a name cut out of the middle of a sentence may start in lower case
                    if (m.name[0] >= 'A' && m.name[0] <= 'Z') {
                        if (fixed[0] >= 'a' && fixed[0] <= 'z') fixed[0] = static_cast<char>(fixed[0] - 'a' + 'A');
                        else if (fixed.size() > 1 && static_cast<unsigned char>(fixed[0]) == 0xC3 && static_cast<unsigned char>(fixed[1]) >= 0xA0 && static_cast<unsigned char>(fixed[1]) <= 0xBE && static_cast<unsigned char>(fixed[1]) != 0xB7)
                            fixed[1] = static_cast<char>(static_cast<unsigned char>(fixed[1]) - 0x20);
                    }
                    const bool lowerStart = name[0] >= 'a' && name[0] <= 'z', upperName = m.name[0] >= 'A' && m.name[0] <= 'Z';
                    if (lowerStart && upperName) { if (!derivedLower_.count(m.name) && !exact_.count(m.name)) derivedLower_[m.name] = name; }
                    else if (!derived_.count(m.name) && !exact_.count(m.name)) derived_[m.name] = fixed;
                }
            }
        }
    }
}

void Translator::Finish() {
    exact_.clear();
    pattern_.clear();
    pieces_.clear();
    std::unordered_map<std::string, Choice> patterns;
    std::vector<std::string> patternOrder;
    for (const std::string& english : order_) {
        const auto& values = seen_.at(english).values;
        const auto best = std::max_element(values.begin(), values.end(), [](const auto& l, const auto& r) { return l.second < r.second; });
        exact_[english] = best->first;
        std::vector<std::string> a, b;
        const std::string ta = ReplaceNumbers(english, &a), tb = ReplaceNumbers(best->first, &b);
        if (a.size() == b.size() && ta.size() >= 2 && HasLetter(ta)) {
            auto& list = pieces_[PieceIndex(ta)];
            if (std::none_of(list.begin(), list.end(), [&](const auto& p) { return p.first == ta; })) list.emplace_back(ta, tb);
        }
        if (a.empty() || a.size() != b.size()) continue;
        auto found = patterns.find(ta);
        if (found == patterns.end()) { patternOrder.push_back(ta); found = patterns.emplace(ta, Choice{}).first; }
        auto& v = found->second.values;
        auto it = std::find_if(v.begin(), v.end(), [&](const auto& x) { return x.first == tb; });
        if (it == v.end()) v.emplace_back(tb, 1);
        else ++it->second;
    }
    for (const std::string& same : identity_) {
        const std::string ta = ReplaceNumbers(same, nullptr);
        if (ta.size() < 2 || !HasLetter(ta)) continue;
        auto& list = pieces_[PieceIndex(ta)];
        if (std::none_of(list.begin(), list.end(), [&](const auto& p) { return p.first == ta; })) list.emplace_back(ta, ta);
    }
    for (auto& entry : pieces_)
        std::stable_sort(entry.second.begin(), entry.second.end(), [](const auto& l, const auto& r) { return l.first.size() > r.first.size(); });
    for (const std::string& key : patternOrder) {
        const auto& v = patterns.at(key).values;
        const auto best = std::max_element(v.begin(), v.end(), [](const auto& l, const auto& r) { return l.second < r.second; });
        pattern_[key] = best->first;
    }
    DeriveNames();
    const auto lower = [](std::string s) {
        for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        return s;
    };
    lowerExact_.clear();
    lowerPattern_.clear();
    for (const auto& e : exact_) lowerExact_.emplace(lower(e.first), e.second);
    for (const auto& e : pattern_) lowerPattern_.emplace(lower(e.first), e.second);
}

bool Translator::TranslateCell(std::string& cell, bool allowFallback) const {
    std::size_t start = 0, end = cell.size();
    while (IsColorCode(cell, start) && start + 4 <= end) start += 4;
    while (end >= start + 4 && IsColorCode(cell, end - 4)) end -= 4;
    const std::string pre = cell.substr(0, start), post = cell.substr(end), core = cell.substr(start, end - start);
    // A colour code inside the text (a red "*" footnote marker after the sentence, say) splits the website's text into
    // pieces. When everything after the first code has no letters, translate the sentence without the codes and put
    // them back in front of the same ending.
    {
        std::size_t firstCode = std::string::npos;
        for (std::size_t i = 0; i + 4 <= core.size(); ++i)
            if (IsColorCode(core, i)) { firstCode = i; break; }
        if (firstCode != std::string::npos && firstCode > 0) {
            std::string tail = core.substr(firstCode), plainTail;
            bool letters = false;
            for (std::size_t i = 0; i < tail.size();) {
                if (i + 4 <= tail.size() && IsColorCode(tail, i)) { i += 4; continue; }
                if (std::isalpha(static_cast<unsigned char>(tail[i])) || static_cast<unsigned char>(tail[i]) >= 0x80) letters = true;
                plainTail += tail[i++];
            }
            if (!letters && !plainTail.empty()) {
                std::string plain = core.substr(0, firstCode) + plainTail;
                if (TranslateCell(plain, allowFallback) && plain.size() > plainTail.size() &&
                    plain.compare(plain.size() - plainTail.size(), plainTail.size(), plainTail) == 0) {
                    cell = pre + plain.substr(0, plain.size() - plainTail.size()) + tail + post;
                    return true;
                }
            }
        }
    }
    const auto exact = exact_.find(core);
    if (exact != exact_.end()) {
        if (!allowFallback && fallback_.count(core)) return false;
        cell = pre + exact->second + post;
        return true;
    }
    {
        std::string lowerCore = core;
        for (char& ch : lowerCore) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        const auto low = lowerExact_.find(lowerCore);
        if (low != lowerExact_.end() && lowerCore != core && !(!allowFallback && fallback_.count(core))) { cell = pre + low->second + post; return true; }
    }
    // Text coloured in pieces ("The <orange>Kill Ledger</orange> <red>cannot be transfered</red> between characters."):
    // the website's inline pieces are aligned one by one, so translate each piece between the colour codes. Only when
    // every piece with letters is known, so a sentence is never half translated.
    {
        bool inner = false;
        for (std::size_t i = 0; i + 4 <= core.size(); ++i)
            if (IsColorCode(core, i)) { inner = true; break; }
        if (inner) {
            std::string out;
            bool ok = true, any = false;
            for (std::size_t i = 0; i < core.size() && ok;) {
                if (i + 4 <= core.size() && IsColorCode(core, i)) { out += core.substr(i, 4); i += 4; continue; }
                std::size_t j = i;
                while (j < core.size() && !(j + 4 <= core.size() && IsColorCode(core, j))) ++j;
                std::string piece = core.substr(i, j - i);
                i = j;
                std::size_t a = 0, b = piece.size();
                while (a < b && piece[a] == ' ') ++a;
                while (b > a && piece[b - 1] == ' ') --b;
                std::string word = piece.substr(a, b - a);
                if (!HasWord(word)) { out += piece; continue; }
                if (!TranslateCell(word, allowFallback)) { ok = false; break; }
                out += piece.substr(0, a) + word + piece.substr(b);
                any = true;
            }
            if (ok && any) { cell = pre + out + post; return true; }
        }
    }
    std::vector<std::string> numbers;
    const std::string shape = ReplaceNumbers(core, &numbers);
    if (!numbers.empty()) {
        const auto lowered = [](std::string s) {
            for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            return s;
        };
        auto found = pattern_.find(shape);
        const std::string* patternText = found != pattern_.end() ? &found->second : nullptr;
        if (patternText == nullptr) {
            const auto low = lowerPattern_.find(lowered(shape));
            if (low != lowerPattern_.end()) patternText = &low->second;
        }
        if (patternText != nullptr && static_cast<std::size_t>(std::count(patternText->begin(), patternText->end(), '#')) == numbers.size()) {
            std::string out;
            std::size_t used = 0;
            for (const char c : *patternText) {
                if (c == '#') out += numbers[used++];
                else out += c;
            }
            cell = pre + out + post;
            return true;
        }
    }
    {
        std::string joined;
        if (TranslatePieces(core, joined)) { cell = pre + joined + post; return true; }
    }
    // pieces joined by ", ", " / " or ": "
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < core.size();) {
        std::size_t sepLen = 0;
        if (core.compare(i, 2, ", ") == 0) sepLen = 2;
        else if (core.compare(i, 3, " / ") == 0) sepLen = 3;
        else if (core.compare(i, 2, ": ") == 0) sepLen = 2;
        else if (core.compare(i, 3, " - ") == 0) sepLen = 3;
        else if (core.compare(i, 3, " + ") == 0) sepLen = 3;
        else if (core.compare(i, 2, "\\n") == 0) sepLen = 2;  // a written-out "\n" line break inside a text
        if (sepLen > 0) { parts.push_back(current); parts.push_back(core.substr(i, sepLen)); current.clear(); i += sepLen; }
        else current += core[i++];
    }
    parts.push_back(current);
    if (parts.size() > 1) {
        std::string out;
        bool hit = false;
        for (const std::string& part : parts) {
            const auto found = exact_.find(part);
            if (found != exact_.end()) { out += found->second; hit = true; }
            else out += part;
        }
        if (hit) { cell = pre + out + post; return true; }
    }
    return false;
}

// A cell made of several pieces of the website's text run together (a stat line, a list of types). It is translated only
// when every word in it belongs to a known piece, so nothing is half translated by guesswork.
bool Translator::TranslatePieces(const std::string& core, std::string& out) const {
    if (core.find('#') != std::string::npos) return false;
    std::vector<std::string> numbers;
    const std::string shape = ReplaceNumbers(core, &numbers);
    std::string result;
    bool hit = false;
    for (std::size_t i = 0; i < shape.size();) {
        const bool atWordStart = i == 0 || !WordByte(shape[i - 1]) || !WordByte(shape[i]);  // a piece may start with '+' or '(' right after a word
        if (atWordStart && i + 1 < shape.size()) {
            const auto list = pieces_.find(PieceIndex(shape.substr(i, 2)));
            if (list != pieces_.end()) {
                const std::pair<std::string, std::string>* best = nullptr;
                for (const auto& piece : list->second) {
                    if (shape.compare(i, piece.first.size(), piece.first) != 0) continue;
                    const std::size_t end = i + piece.first.size();
                    if (end < shape.size() && WordByte(shape[end]) && WordByte(piece.first.back())) continue;
                    best = &piece;
                    break;  // longest first
                }
                if (best != nullptr) {
                    result += best->second;
                    i += best->first.size();
                    hit = true;
                    continue;
                }
            }
        }
        if (Letter(shape[i])) return false;  // a word the website has no text for
        result += shape[i++];
    }
    if (!hit) return false;
    std::size_t used = 0;
    out.clear();
    for (const char c : result) {
        if (c == '#') {
            if (used >= numbers.size()) return false;
            out += numbers[used++];
        } else out += c;
    }
    return used == numbers.size();
}

std::string Translator::Translate(const std::string& wikiData, TranslateStats* stats) const {
    static const char* kCountries[] = {"America", "Austria", "Britain", "Canada", "China", "France", "Germany", "Italy", "Japan", "Portugal", "Russia"};
    std::string out;
    out.reserve(wikiData.size());
    std::size_t lineStart = 0;
    bool first = true;
    while (lineStart <= wikiData.size()) {
        std::size_t lineEnd = wikiData.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = wikiData.size();
        std::vector<std::string> fields = SplitOn(wikiData.substr(lineStart, lineEnd - lineStart), '\t');
        const std::string& type = fields[0];
        if (type == "LINE" || type == "FIELD" || type == "BLOCK" || type == "ENTRY" || type == "TAG" || type == "NOTE" || type == "RUNE") {
            for (std::size_t i = 1; i < fields.size(); ++i) {
                if (type == "BLOCK" && i == 5) continue;  // the tier is a key the overlay looks for
                if (type == "BLOCK" && i == 6 && std::any_of(std::begin(kCountries), std::end(kCountries), [&](const char* c) { return fields[i] == c; })) continue;
                std::vector<std::string> cells = SplitOn(fields[i], '\x1f');  // table cells
                for (std::string& cell : cells) {
                    std::vector<std::string> lines = SplitOn(cell, '\x1e');  // lines inside one cell
                    for (std::string& line : lines) {
                        if (!HasWord(StripCodes(line))) continue;
                        if (stats != nullptr) ++stats->cellsWithWords;
                        if (TranslateCell(line, !(type == "ENTRY" && i == 1))) {
                            if (stats != nullptr) ++stats->translated;
                        } else {
                            const auto name = derived_.find(StripCodes(line));
                            if (name != derived_.end()) line = name->second;
                            else if (type == "RUNE") {  // "Heaven Rune" / "Rune du Ciel": the label is "du Ciel", shown as "Rune du Ciel"
                                const auto lower = derivedLower_.find(StripCodes(line));
                                if (lower != derivedLower_.end()) line = lower->second;
                            }
                        }
                    }
                    std::string joinedLines;
                    for (std::size_t k = 0; k < lines.size(); ++k) joinedLines += (k > 0 ? "\x1e" : "") + lines[k];
                    cell = joinedLines;
                }
                std::string joined;
                for (std::size_t k = 0; k < cells.size(); ++k) joined += (k > 0 ? "\x1f" : "") + cells[k];
                fields[i] = joined;
            }
        }
        if (!first) out += '\n';
        first = false;
        for (std::size_t i = 0; i < fields.size(); ++i) out += (i > 0 ? "\t" : "") + fields[i];
        if (lineEnd >= wikiData.size()) break;
        lineStart = lineEnd + 1;
    }
    return out;
}

}  // namespace esrwiki::localize
