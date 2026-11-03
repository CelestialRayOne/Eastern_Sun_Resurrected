// Reading the ESR website's HTML pages: text cleaning, <br>-separated property lines with their colours, and
// element scanning. Shared by the unique (web_uniques_live.cpp) and rune (web_runes_live.cpp) page builders so the
// two cannot drift apart. The Python twin is tools/web_uniques.py. Platform-neutral.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace esrwiki::web {

// ---- Text helpers (the Python twin uses str.split(), html.unescape and re) -----------------------------------

inline std::size_t SpaceLength(const std::string& s, std::size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x1F)) return 1;
    const auto at = [&](std::size_t k) { return i + k < s.size() ? static_cast<unsigned char>(s[i + k]) : 0; };
    if (c == 0xC2 && (at(1) == 0x85 || at(1) == 0xA0)) return 2;
    if (c == 0xE1 && at(1) == 0x9A && at(2) == 0x80) return 3;
    if (c == 0xE2 && at(1) == 0x80 && ((at(2) >= 0x80 && at(2) <= 0x8A) || at(2) == 0xA8 || at(2) == 0xA9 || at(2) == 0xAF)) return 3;
    if (c == 0xE2 && at(1) == 0x81 && at(2) == 0x9F) return 3;
    if (c == 0xE3 && at(1) == 0x80 && at(2) == 0x80) return 3;
    return 0;
}

inline void AppendUtf8(std::string& out, unsigned long code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

inline std::string Unescape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const std::size_t end = s.find(';', i);
        if (end == std::string::npos || end - i > 10) {
            out += s[i];
            continue;
        }
        const std::string name = s.substr(i + 1, end - i - 1);
        bool done = true;
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name == "nbsp") AppendUtf8(out, 0xA0);
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const std::string digits = name.substr(hex ? 2 : 1);
            bool valid = !digits.empty();
            unsigned long value = 0;
            for (char d : digits) {
                const int v = (d >= '0' && d <= '9') ? d - '0' : hex && d >= 'a' && d <= 'f' ? d - 'a' + 10
                              : hex && d >= 'A' && d <= 'F' ? d - 'A' + 10 : -1;
                if (v < 0) { valid = false; break; }
                value = value * (hex ? 16 : 10) + static_cast<unsigned long>(v);
            }
            if (valid && value > 0 && value <= 0x10FFFF) AppendUtf8(out, value);
            else done = false;
        } else {
            done = false;
        }
        if (done) i = end;
        else out += s[i];
    }
    return out;
}

// html.unescape, then whitespace runs (Unicode spaces too) become one space and the ends are trimmed.
inline std::string Clean(const std::string& raw) {
    const std::string s = Unescape(raw);
    std::string out;
    bool pending = false;
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t n = SpaceLength(s, i);
        if (n != 0) {
            pending = !out.empty();
            i += n;
            continue;
        }
        if (pending) out += ' ';
        pending = false;
        out += s[i++];
    }
    return out;
}

inline std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline std::string Upper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

inline std::string Norm(const std::string& s) {
    std::string out;
    for (char c : Lower(s)) if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    return out;
}

inline std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && SpaceLength(s, a) != 0) a += SpaceLength(s, a);
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

inline bool StartsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
inline bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t at = text.find('\n', start);
        if (at == std::string::npos) { lines.push_back(text.substr(start)); break; }
        lines.push_back(text.substr(start, at - start));
        start = at + 1;
    }
    return lines;
}

inline std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t at = line.find('\t', start);
        if (at == std::string::npos) { parts.push_back(line.substr(start)); break; }
        parts.push_back(line.substr(start, at - start));
        start = at + 1;
    }
    return parts;
}

// Case-insensitive find.
inline std::size_t FindCI(const std::string& hay, const std::string& needle, std::size_t from = 0) {
    if (needle.empty() || hay.size() < needle.size()) return std::string::npos;
    const std::string lower = Lower(needle);
    for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
        std::size_t k = 0;
        while (k < needle.size()) {
            char c = hay[i + k];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != lower[k]) break;
            ++k;
        }
        if (k == needle.size()) return i;
    }
    return std::string::npos;
}

// Text between tags, as tag-free pieces: re.split(r"<[^>]+>", s) with each piece cleaned, empty ones dropped.
inline std::vector<std::string> Pieces(const std::string& cell) {
    std::vector<std::string> out;
    std::string current;
    for (std::size_t i = 0; i < cell.size();) {
        if (cell[i] == '<') {
            const std::size_t end = cell.find('>', i + 1);
            if (end != std::string::npos && end > i + 1) {
                const std::string text = Clean(current);
                if (!text.empty()) out.push_back(text);
                current.clear();
                i = end + 1;
                continue;
            }
        }
        current += cell[i++];
    }
    const std::string text = Clean(current);
    if (!text.empty()) out.push_back(text);
    return out;
}

// re.sub(r"<[^>]+>", " ", s), cleaned.
inline std::string TextOf(const std::string& cell) {
    std::string spaced;
    for (std::size_t i = 0; i < cell.size();) {
        if (cell[i] == '<') {
            const std::size_t end = cell.find('>', i + 1);
            if (end != std::string::npos && end > i + 1) {
                spaced += ' ';
                i = end + 1;
                continue;
            }
        }
        spaced += cell[i++];
    }
    return Clean(spaced);
}

// "(10 to 20)" -> "10-20"
inline bool Number(const std::string& s, std::size_t& i) {
    std::size_t k = i;
    if (k < s.size() && s[k] == '-') ++k;
    const std::size_t digits = k;
    while (k < s.size() && s[k] >= '0' && s[k] <= '9') ++k;
    if (k == digits) return false;
    if (k + 1 < s.size() && s[k] == '.' && s[k + 1] >= '0' && s[k + 1] <= '9') {
        ++k;
        while (k < s.size() && s[k] >= '0' && s[k] <= '9') ++k;
    }
    i = k;
    return true;
}

inline std::string DashRanges(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '(') {
            std::size_t k = i + 1;
            const std::size_t a0 = k;
            if (Number(s, k) && s.compare(k, 4, " to ") == 0) {
                const std::size_t a1 = k;
                k += 4;
                const std::size_t b0 = k;
                if (Number(s, k) && k < s.size() && s[k] == ')') {
                    out += s.substr(a0, a1 - a0) + "-" + s.substr(b0, k - b0);
                    i = k + 1;
                    continue;
                }
            }
        }
        out += s[i++];
    }
    return out;
}

inline bool HiddenLine(const std::string& text) {
    static const std::string kHidden = "tinkering points";
    if (text.size() < kHidden.size() || Lower(text.substr(0, kHidden.size())) != kHidden) return false;
    if (text.size() == kHidden.size()) return true;
    const char c = text[kHidden.size()];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
}

struct Line {
    char color;
    std::string text;
};

inline char ColorOf(const std::string& name) {
    const std::string upper = Upper(name);
    if (upper == "4850B8") return '3';
    if (upper == "ORANGE") return '8';
    if (upper == "WHITE") return '0';
    if (upper == "RED") return '1';
    return '3';
}

// `dash` writes "(10 to 20)" as "10-20". `gaps` also reports each empty line as a Line with color kGap and no text.
constexpr char kGap = '\0';

inline std::vector<Line> PropertyLines(const std::string& cell, bool dash = true, bool gaps = false) {
    std::vector<Line> lines;
    std::vector<char> stack;  // 0 = no colour of its own
    std::string current;
    char lineColor = '3';
    auto color = [&]() {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) if (*it != 0) return *it;
        return '3';
    };
    bool final = false;
    auto flush = [&]() {
        const std::string text = Clean(current);
        current.clear();
        if (!text.empty()) lines.push_back({lineColor, text});
        else if (gaps && !final) lines.push_back({kGap, std::string()});
    };
    for (std::size_t i = 0; i < cell.size();) {
        if (cell[i] == '<') {
            const std::size_t end = cell.find('>', i);
            if (end != std::string::npos) {
                const std::string token = cell.substr(i, end - i + 1);
                std::size_t k = 1;
                const bool closing = k < token.size() && token[k] == '/';
                if (closing) ++k;
                while (k < token.size() && (token[k] == ' ' || token[k] == '\t' || token[k] == '\n' || token[k] == '\r')) ++k;
                std::string name;
                while (k < token.size() && ((token[k] >= 'a' && token[k] <= 'z') || (token[k] >= 'A' && token[k] <= 'Z') ||
                                            (token[k] >= '0' && token[k] <= '9')))
                    name += token[k++];
                name = Lower(name);
                if (name == "br") {
                    flush();
                } else if (name == "font") {
                    if (closing) {
                        if (!stack.empty()) stack.pop_back();
                    } else {
                        char value = 0;
                        const std::size_t at = FindCI(token, "color");
                        if (at != std::string::npos) {
                            std::size_t p = at + 5;
                            while (p < token.size() && (token[p] == ' ' || token[p] == '\t')) ++p;
                            if (p < token.size() && token[p] == '=') {
                                ++p;
                                while (p < token.size() && (token[p] == ' ' || token[p] == '\t')) ++p;
                                if (p < token.size() && token[p] == '"') ++p;
                                if (p < token.size() && token[p] == '#') ++p;
                                std::string word;
                                while (p < token.size() && ((token[p] >= '0' && token[p] <= '9') || (token[p] >= 'a' && token[p] <= 'z') ||
                                                            (token[p] >= 'A' && token[p] <= 'Z')))
                                    word += token[p++];
                                if (!word.empty()) value = ColorOf(word);
                            }
                        }
                        stack.push_back(value);
                    }
                }
                i = end + 1;
                continue;
            }
        }
        // A run of text up to the next tag.
        std::size_t stop = cell.find('<', i + 1);
        if (stop == std::string::npos) stop = cell.size();
        const std::string token = cell.substr(i, stop - i);
        if (!Clean(token).empty() && Clean(current).empty()) lineColor = color();
        current += token;
        i = stop;
    }
    final = true;
    flush();
    std::vector<Line> out;
    for (const Line& line : lines) {
        if (line.color == kGap) {
            out.push_back(line);
            continue;
        }
        const std::string text = dash ? DashRanges(line.text) : line.text;
        if (HiddenLine(text)) continue;
        out.push_back({line.color, text});
    }
    return out;
}

// The text of each <tag ...>...</tag> found in `html`, in order. `from`/`to` are positions of the inner text.
struct Span {
    std::size_t begin, end;
};

inline std::vector<Span> Elements(const std::string& html, const std::string& open, const std::string& close) {
    std::vector<Span> out;
    std::size_t pos = 0;
    for (;;) {
        const std::size_t start = FindCI(html, open, pos);
        if (start == std::string::npos) break;
        const std::size_t gt = html.find('>', start);
        if (gt == std::string::npos) break;
        const std::size_t stop = FindCI(html, close, gt + 1);
        if (stop == std::string::npos) break;
        out.push_back({gt + 1, stop});
        pos = stop + close.size();
    }
    return out;
}

inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// re.search(r"Req\s*Lvl:?\s*(\d+)", plain, re.I): where "Req" starts and the digits; false when there is none.
inline bool FindReqLvl(const std::string& plain, std::size_t& at, std::string& level) {
    std::size_t from = 0;
    for (;;) {
        at = FindCI(plain, "req", from);
        if (at == std::string::npos) return false;
        std::size_t k = at + 3;
        while (k < plain.size() && IsSpace(plain[k])) ++k;
        if (FindCI(plain, "lvl", k) == k) {
            k += 3;
            if (k < plain.size() && plain[k] == ':') ++k;
            while (k < plain.size() && IsSpace(plain[k])) ++k;
            const std::size_t digits = k;
            while (k < plain.size() && plain[k] >= '0' && plain[k] <= '9') ++k;
            if (k > digits) {
                level = plain.substr(digits, k - digits);
                return true;
            }
            // "Req Lvl" without a number: Python's name split still stops here, but the rune has no level.
            level.clear();
            return true;
        }
        from = at + 1;
    }
}

// re.search(r"height\s*=", row, re.I)
inline bool HasHeightAttribute(const std::string& row) {
    std::size_t from = 0;
    for (;;) {
        const std::size_t at = FindCI(row, "height", from);
        if (at == std::string::npos) return false;
        std::size_t k = at + 6;
        while (k < row.size() && IsSpace(row[k])) ++k;
        if (k < row.size() && row[k] == '=') return true;
        from = at + 1;
    }
}

// re.search(r"<b>\s*<font[^>]*color\s*=\s*\"?([#0-9A-Za-z]+)", cell, re.I)
inline std::string NameColor(const std::string& cell) {
    std::size_t from = 0;
    for (;;) {
        const std::size_t b = FindCI(cell, "<b>", from);
        if (b == std::string::npos) return std::string();
        std::size_t k = b + 3;
        while (k < cell.size() && IsSpace(cell[k])) ++k;
        if (FindCI(cell, "<font", k) == k) {
            const std::size_t end = cell.find('>', k);
            if (end != std::string::npos) {
                const std::string tag = cell.substr(k, end - k);
                // [^>]* is greedy, so the last "color" followed by = wins.
                std::size_t found = std::string::npos;
                for (std::size_t c = FindCI(tag, "color"); c != std::string::npos; c = FindCI(tag, "color", c + 1)) {
                    std::size_t p = c + 5;
                    while (p < tag.size() && IsSpace(tag[p])) ++p;
                    if (p < tag.size() && tag[p] == '=') found = p + 1;
                }
                if (found != std::string::npos) {
                    std::size_t p = found;
                    while (p < tag.size() && IsSpace(tag[p])) ++p;
                    if (p < tag.size() && tag[p] == '"') ++p;
                    std::string word;
                    while (p < tag.size() && (tag[p] == '#' || (tag[p] >= '0' && tag[p] <= '9') || (tag[p] >= 'a' && tag[p] <= 'z') ||
                                              (tag[p] >= 'A' && tag[p] <= 'Z')))
                        word += tag[p++];
                    if (!word.empty()) return word;
                }
            }
        }
        from = b + 1;
    }
}

// re.split(r"<br\s*/?>", cell, flags=re.I)
inline std::vector<std::string> SplitBr(const std::string& cell) {
    std::vector<std::string> parts;
    std::size_t start = 0, i = 0;
    while (i < cell.size()) {
        if (cell[i] == '<' && FindCI(cell, "<br", i) == i) {
            std::size_t k = i + 3;
            while (k < cell.size() && IsSpace(cell[k])) ++k;
            if (k < cell.size() && cell[k] == '/') ++k;
            if (k < cell.size() && cell[k] == '>') {
                parts.push_back(cell.substr(start, i - start));
                start = i = k + 1;
                continue;
            }
        }
        ++i;
    }
    parts.push_back(cell.substr(start));
    return parts;
}

inline std::string Join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += separator;
        out += parts[i];
    }
    return out;
}

// Replaces the page with this id; data without one gets it after the page `after` (else last).
inline std::string Splice(const std::string& current, const std::vector<std::string>& page, const std::string& id, const std::string& after) {
    std::vector<std::string> lines = SplitLines(current);
    const bool trailing = !lines.empty() && lines.back().empty();
    if (trailing) lines.pop_back();
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < lines.size(); ++i) if (lines[i].compare(0, 5, "PAGE\t") == 0) starts.push_back(i);
    auto spanOf = [&](const std::string& wanted, std::size_t& begin, std::size_t& end) {
        for (std::size_t n = 0; n < starts.size(); ++n) {
            const std::vector<std::string> parts = SplitTabs(lines[starts[n]]);
            if (parts.size() > 1 && parts[1] == wanted) {
                begin = starts[n];
                end = n + 1 < starts.size() ? starts[n + 1] : lines.size();
                return true;
            }
        }
        return false;
    };
    std::size_t begin = 0, end = 0;
    std::vector<std::string> kept;
    if (spanOf(id, begin, end)) {
        kept.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(begin));
        kept.insert(kept.end(), page.begin(), page.end());
        kept.insert(kept.end(), lines.begin() + static_cast<std::ptrdiff_t>(end), lines.end());
    } else {
        std::size_t at = lines.size(), b2 = 0, e2 = 0;
        if (spanOf(after, b2, e2)) at = e2;
        kept.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(at));
        kept.insert(kept.end(), page.begin(), page.end());
        kept.insert(kept.end(), lines.begin() + static_cast<std::ptrdiff_t>(at), lines.end());
    }
    std::string text = Join(kept, "\n");
    if (trailing) text += '\n';
    return text;
}

}  // namespace esrwiki::web
