// Win32 window for the ESR wiki overlay: tab row per wiki page, search box,
// paged list of entry titles, and a scrollable detail pane. Items are tiles
// (three per row, six per row on the compact rune page) under their section
// headers, with stats in the tooltip. Search narrows the items shown, matching
// their tooltip text too. While open, a full-screen blocker takes all mouse input
// and the panel keeps keyboard focus, so the game sees neither.
//
// Sizes are written at 96 DPI and scaled by DPI (S()) and by window size
// (uiScale for fonts). The window is sized from the game's client area whenever
// it is shown or the game window moves.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>
// GDI+ needs the COM types (IStream, PROPID) that objidl.h declares. Include it first.
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "overlay.hpp"
#include "debug_log.hpp"
#include "cursor_art.hpp"
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <map>

#include "wiki_model.hpp"

namespace esrwiki {
static std::string NarrowForLog(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

namespace {
std::map<std::wstring, std::wstring> gUiStrings;  // English text -> the loader language's text
std::set<std::wstring> gUiValues;  // the translations themselves (text already in the language is not 'missing')
}

std::size_t LoadUiStrings(const std::wstring& path) {
    gUiStrings.clear();
    gUiValues.clear();
    if (path.empty()) return 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;
    const auto toWide = [](const std::string& utf8) {
        if (utf8.empty()) return std::wstring();
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring out(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
        return out;
    };
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t tab = line.find('\t');
        if (tab == 0 || tab == std::string::npos) continue;
        gUiStrings[toWide(line.substr(0, tab))] = toWide(line.substr(tab + 1));
        gUiValues.insert(toWide(line.substr(tab + 1)));
    }
    return gUiStrings.size();
}

std::wstring UiText(const std::wstring& english) {
    const auto found = gUiStrings.find(english);
    if (found != gUiStrings.end()) return found->second;
    if (!gUiStrings.empty()) {  // a language is loaded: say which labels it has no text for
        bool plainEnglish = true;
        for (const wchar_t c : english) {
            if (c > 127) plainEnglish = false;
        }
        if (gUiValues.count(english) != 0) plainEnglish = false;  // already a translation (text built from localized data)
        if (plainEnglish) DebugLogOnce("ui-missing:" + NarrowForLog(english), "no translation for UI text: " + NarrowForLog(english));
    }
    return english;
}

// UiText for names that come from the page data (entry titles, field labels): the data is already localized where
// the website is, so a name with no entry here is normal and is not logged.
static std::wstring UiDataText(const std::wstring& text) {
    const auto found = gUiStrings.find(text);
    return found != gUiStrings.end() ? found->second : text;
}

std::wstring UiFormat(const std::wstring& english, const std::vector<std::wstring>& args) {
    std::wstring text = UiText(english);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::wstring token = L"{" + std::to_wstring(i) + L"}";
        for (std::size_t at = text.find(token); at != std::wstring::npos; at = text.find(token, at + args[i].size())) {
            text.replace(at, token.size(), args[i]);
        }
    }
    return text;
}

// The counts the wiki data builders write under an entry heading ("215 items", "4448 affixes, 16 item types").
static std::wstring TranslateCounts(const std::wstring& text) {
    std::size_t digits = 0;
    while (digits < text.size() && text[digits] >= L'0' && text[digits] <= L'9') ++digits;
    if (digits == 0) return UiText(text);
    const std::wstring first = text.substr(0, digits), rest = text.substr(digits);
    if (rest == L" items" || rest == L" item") return UiFormat(L"{0}" + rest, {first});
    const std::size_t comma = rest.find(L", ");
    if (rest.rfind(L" affixes", 0) == 0 && comma != std::wstring::npos) {
        std::size_t d2 = comma + 2, end = d2;
        while (end < rest.size() && rest[end] >= L'0' && rest[end] <= L'9') ++end;
        if (end > d2 && rest.compare(end, std::wstring::npos, L" item types") == 0)
            return UiFormat(L"{0} affixes, {1} item types", {first, rest.substr(d2, end - d2)});
    }
    return UiText(text);
}

namespace {

constexpr int kIdSearch = 1001;
constexpr UINT kMsgToggle = WM_APP + 1;
constexpr UINT kMsgShutdown = WM_APP + 2;
constexpr UINT kMsgOpen = WM_APP + 3;
constexpr UINT kMsgReload = WM_APP + 4;  // swap in a new database (see Overlay::ReplaceDatabase)
constexpr UINT_PTR kTimerFocus = 1;
constexpr UINT_PTR kTimerHandoff = 2;
constexpr UINT kHandoffPollMs = 8;
constexpr ULONGLONG kHandoffTimeoutMs = 600;
// Foreground window to return to, handed from one overlay to the other when switching panels.
HWND gHandoffForeground = nullptr;
// While a panel switch is in progress the incoming panel's backdrop starts invisible and is raised
// only when the outgoing one goes, so the two never dim the game twice over.
std::atomic<bool> gHandoffActive{false};
// Set by the incoming panel once it is visible and painted; the outgoing panel hides only after that.
std::atomic<bool> gHandoffReady{false};
std::atomic<HWND> gHandoffBlocker{nullptr};
constexpr UINT kMsgCursorShow = WM_APP + 10;  // to the cursor thread
constexpr UINT kMsgCursorHide = WM_APP + 11;
constexpr UINT kTimerInterval = 150;
// The background picture has its own frame. These are the sizes of its parts, in the picture's own pixels:
// the frame on the left, right and bottom, and the header band on top, whose last row is a light line.
constexpr int kFrameSlice = 22;
constexpr int kHeaderSlice = 128;

constexpr BYTE kBlockerAlpha = 1;  // practically clear; keep above 0 so the layer still catches clicks meant for the game

void ReleaseHandoffBlocker() {
    gHandoffActive = false;
    const HWND next = gHandoffBlocker.exchange(nullptr);
    if (next != nullptr && IsWindow(next)) SetLayeredWindowAttributes(next, 0, kBlockerAlpha, LWA_ALPHA);
}

// Palette taken from the Eastern Sun Resurrected launcher: near-black warm brown panels, thin bronze borders,
// gold headings, cream text and a muted tan for labels.
constexpr COLORREF kBackground = RGB(12, 8, 5);
constexpr COLORREF kPanel = RGB(24, 17, 11);
constexpr COLORREF kTile = RGB(20, 14, 9);
constexpr COLORREF kSearchFill = RGB(38, 28, 18);
constexpr COLORREF kSelected = RGB(64, 46, 22);
constexpr COLORREF kBorder = RGB(122, 92, 48);
constexpr COLORREF kGold = RGB(224, 182, 96);  // headers and accents: launcher gold
constexpr COLORREF kHeaderGold = RGB(208, 166, 82);  // tab titles, category titles and section headings
constexpr COLORREF kText = RGB(244, 236, 220);
constexpr COLORREF kDim = RGB(168, 152, 124);
constexpr COLORREF kBody = RGB(228, 218, 198);

std::wstring Wide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
    return out;
}

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), length, nullptr,
                        nullptr);
    return out;
}

// D2 color codes mapped to the item-database palette (beige uniques, green set
// lines, blue magic, gray stats, red requirements).
COLORREF CodeColor(char code) {
    switch (code) {
    case '0': return RGB(235, 235, 235);  // white
    case '1': return RGB(255, 90, 90);    // red, requirements
    case '2': return RGB(90, 220, 90);    // green
    case '3': return RGB(110, 140, 255);  // blue, magic properties
    case '4': return RGB(201, 185, 150);  // beige, uniques and runewords
    case '5': return RGB(150, 150, 150);  // gray, base lines
    case '6': return RGB(130, 130, 130);  // black in D2, lifted for a dark theme
    case '7': return RGB(190, 170, 120);  // tan, rune names
    case '8': return RGB(255, 150, 60);   // orange
    case '9': return RGB(240, 240, 110);  // yellow
    case ';': return RGB(190, 120, 235);  // purple
    case ':': return RGB(110, 210, 110);  // dark green
    case 'C': return RGB(110, 230, 110);  // set items
    case 'H': return RGB(112, 104, 80);   // website headings (#706850)
    case 'R': return RGB(255, 0, 0);      // website red, ability names
    case 'P': return RGB(128, 0, 128);    // website purple, rewards
    case 'O': return RGB(255, 165, 0);    // website orange
    case 'G': return RGB(0, 128, 0);      // website green
    case 'D': return RGB(139, 0, 0);      // website dark red
    case 'B': return RGB(72, 80, 184);    // website blue (#4850B8)
    case 'K': return RGB(241, 229, 172);  // website beige (#F1E5AC)
    case 'L': return RGB(0, 0, 255);      // website blue (#0000FF)
    case 'Y': return RGB(128, 128, 128);  // website gray (#808080)
    case 'W': return RGB(255, 255, 255);  // website white
    case 'N': return kBody;               // back to the default body colour
    default: return kBody;
    }
}

struct PageView {
    std::size_t selected = 0;  // index into the filtered list
    std::size_t listPage = 0;  // page of the result list being shown
    int detailScroll = 0;
    std::size_t socketFilter = 0;  // runewords: 0 shows all, otherwise only this many runes
    std::string tierFilter;    // uniques: "" shows all tiers, otherwise Normal, Exceptional or Elite
    std::string familyFilter;  // uniques: "" shows all weapon types, otherwise one family such as "Axes"
    BlockSort sort = BlockSort::Default;
};

// A filter or sort button above the detail pane.
enum class ChipKind { Socket, Sort, Tier, Family };
struct Chip {
    RECT rect{};
    ChipKind kind = ChipKind::Socket;
    std::size_t value = 0;  // socket count or BlockSort
    std::string text;       // tier or family name for those filters
};

struct Layout {
    std::vector<RECT> tabs;  // one per page, in database order
    RECT title{};      // the window title, in the header band
    int separatorY = 0;  // where the background picture's header line is drawn
    RECT close{};
    RECT search{};
    RECT clearQuery{};  // the X beside the search box; clears the query
    RECT matchPrev{};   // steps the selection back to the previous match
    RECT matchNext{};   // steps the selection forward to the next match
    RECT count{};
    RECT prev{};
    RECT next{};
    RECT pageLabel{};
    RECT detail{};
    std::vector<RECT> switchButtons;  // one per switch button, the first leftmost
    RECT scrollTrack{};
    RECT scrollThumb{};
    bool scrollable = false;
    std::vector<Chip> chips;
};

// Tile geometry for one detail pane width.
struct TileStyle {
    int columns = 3;
    int gap = 12;
    int tileWidth = 100;
    int artBox = 60;
    int cell = 30;      // width of one rune icon slot
    int padding = 6;
    HFONT nameFont = nullptr;
};

bool Inside(const RECT& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

RECT Rect(int left, int top, int right, int bottom) {
    RECT rect{left, top, right, bottom};
    return rect;
}

// The frame drawn for the search box: from the left edge to the clear button.
RECT SearchBoxRect(const Layout& layout) {
    return Rect(layout.search.left, layout.search.top, layout.clearQuery.left, layout.search.bottom);
}

// The edit control sits inside that frame, one text line tall and centred vertically, because a
// single-line edit control always draws its text and caret at the top of its client area.
RECT SearchEditRect(const Layout& layout, int lineHeight, int pad) {
    const RECT box = SearchBoxRect(layout);
    const int top = box.top + (box.bottom - box.top - lineHeight) / 2;
    return Rect(box.left + 1 + pad, top, box.right - 1 - pad, top + lineHeight);
}

int Bounded(int value, int low, int high) { return value < low ? low : (value > high ? high : value); }

// Translucent fill so the background art shows through the panels.
void FillPanel(HDC dc, const RECT& rect, COLORREF color, BYTE alpha) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return;
    const HDC source = CreateCompatibleDC(dc);
    const HBITMAP pixel = CreateCompatibleBitmap(dc, 1, 1);
    const HGDIOBJ previous = SelectObject(source, pixel);
    SetPixel(source, 0, 0, color);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(dc, rect.left, rect.top, width, height, source, 0, 0, 1, 1, blend);
    SelectObject(source, previous);
    DeleteObject(pixel);
    DeleteDC(source);
}

void FillColor(HDC dc, const RECT& rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void FrameColor(HDC dc, const RECT& rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FrameRect(dc, &rect, brush);
    DeleteObject(brush);
}

// Kanji rune names are coloured by their place in the Kanji Runes list. I through Yo keep the
// default colour. Ki through Chi red, Sa through O yellow, Ho through Ha orange, Na through Mo green,
// No through Re gold, Su through Wo and Null purple. A name counts only as a whole word: "Ki Rune".
struct RuneNameGroup { const wchar_t* name; char group; };
// Off while the LoD Runes list is drawn: its "Ko Rune" is a different rune from the Kanji one.
bool gKanjiRuneColors = true;
// Names of the LoD runes on the card whose tooltip is being drawn ("Ko"). They are coloured orange
// wherever they appear as "<name> Rune", and take priority over a Kanji rune with the same name.
std::vector<std::wstring> gLodRuneNames;
const RuneNameGroup kRuneNameGroups[] = {
    {L"Ki", 'r'},
    {L"Ri", 'r'},
    {L"Mi", 'r'},
    {L"Ya", 'r'},
    {L"A", 'r'},
    {L"Tsu", 'r'},
    {L"Chi", 'r'},
    {L"Sa", 'y'},
    {L"Yu", 'y'},
    {L"Ke", 'y'},
    {L"E", 'y'},
    {L"Ko", 'y'},
    {L"Ra", 'y'},
    {L"O", 'y'},
    {L"Ho", 'o'},
    {L"Me", 'o'},
    {L"Ru", 'o'},
    {L"Ta", 'o'},
    {L"To", 'o'},
    {L"Wa", 'o'},
    {L"Ha", 'o'},
    {L"Na", 'g'},
    {L"Ni", 'g'},
    {L"Se", 'g'},
    {L"Fu", 'g'},
    {L"Ma", 'g'},
    {L"Hi", 'g'},
    {L"Mo", 'g'},
    {L"No", 'd'},
    {L"Te", 'd'},
    {L"Ro", 'd'},
    {L"So", 'd'},
    {L"Mu", 'd'},
    {L"Ne", 'd'},
    {L"Re", 'd'},
    {L"Su", 'p'},
    {L"He", 'p'},
    {L"Nu", 'p'},
    {L"Wo", 'p'},
    {L"Null", 'p'},
};

COLORREF RuneGroupColor(char group) {
    switch (group) {
    case 'r': return CodeColor('1');                // red
    case 'y': return CodeColor('9');                // yellow
    case 'o': return CodeColor('8');                // orange
    case 'g': return CodeColor('2');                // green
    case 'd': return RGB(230, 190, 60);             // gold
    case 'p': return CodeColor(';');                // purple
    default: return kBody;
    }
}

// Colour of a bare rune label such as "Ki" (the name without " Rune"), or the fallback.
COLORREF KanjiLabelColor(const std::wstring& label, COLORREF fallback) {
    if (!gKanjiRuneColors) return fallback;
    for (const RuneNameGroup& rune : kRuneNameGroups) {
        if (label == rune.name) return RuneGroupColor(rune.group);
    }
    return fallback;
}

// One piece of a line: plain text in the line's colour, or a rune name in its own colour.
struct RunePiece { std::wstring text; COLORREF color; bool named; bool colored = false; bool highlight = false; bool continues = false; };

// The search text being highlighted, lower-case; empty when nothing is searched.
std::wstring gHighlight;
constexpr COLORREF kHighlightFill = RGB(104, 74, 20);

std::wstring LowerWide(std::wstring s) {
    for (wchar_t& c : s) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
    }
    return s;
}

// Splits pieces at each search match so the matched part is drawn on a highlight. Rune-name
// pieces are kept whole.
std::vector<RunePiece> HighlightPieces(const std::vector<RunePiece>& input) {
    if (gHighlight.empty()) return input;
    std::vector<RunePiece> out;
    for (const RunePiece& piece : input) {
        if (piece.named) {
            out.push_back(piece);
            continue;
        }
        const std::wstring lower = LowerWide(piece.text);
        // A part that starts right after a letter continues that word, so it cannot start a new line.
        auto add = [&](std::size_t start, std::size_t length, bool mark) {
            const bool continues = start == 0 ? piece.continues : piece.text[start - 1] != L' ';
            out.push_back({piece.text.substr(start, length), piece.color, false, piece.colored, mark, continues});
        };
        std::size_t pos = 0;
        while (pos < piece.text.size()) {
            const std::size_t hit = lower.find(gHighlight, pos);
            if (hit == std::wstring::npos) {
                add(pos, piece.text.size() - pos, false);
                break;
            }
            if (hit > pos) add(pos, hit - pos, false);
            add(hit, gHighlight.size(), true);
            pos = hit + gHighlight.size();
        }
    }
    return out;
}

bool IsWordChar(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

// True when the rune name starts at text[i] and ends on a word boundary. Compares in place, so
// scanning allocates nothing.
bool RuneNameAt(const std::wstring& text, std::size_t i, const wchar_t* name, std::size_t& after) {
    std::size_t k = i;
    for (const wchar_t* c = name; *c != L'\0'; ++c, ++k) {
        if (k >= text.size() || text[k] != *c) return false;
    }
    if (text.compare(k, 5, L" Rune") != 0) return false;
    k += 5;
    if (k < text.size() && IsWordChar(text[k])) return false;
    after = k;
    return true;
}

// Splits text into pieces, each kanji rune name ("Ki Rune") as its own named piece.
// Returns false when the text has no rune name, so callers can use the plain DrawText path.
bool FindRuneNames(const std::wstring& text, std::vector<RunePiece>& pieces) {
    pieces.clear();
    if (!gKanjiRuneColors) return false;
    if (text.find(L" Rune") == std::wstring::npos) return false;  // the common case: nothing to colour
    bool any = false;
    std::wstring plain;
    auto flush = [&]() {
        if (!plain.empty()) pieces.push_back({plain, kBody, false});
        plain.clear();
    };
    std::size_t i = 0;
    while (i < text.size()) {
        bool matched = false;
        if (i == 0 || !IsWordChar(text[i - 1])) {
            for (const std::wstring& lodName : gLodRuneNames) {
                std::size_t after = 0;
                if (RuneNameAt(text, i, lodName.c_str(), after)) {
                    flush();
                    pieces.push_back({text.substr(i, after - i), CodeColor('8'), true});
                    i = after;
                    matched = any = true;
                    break;
                }
            }
        }
        if (!matched && (i == 0 || !IsWordChar(text[i - 1]))) {
            for (const RuneNameGroup& rune : kRuneNameGroups) {
                std::size_t after = 0;
                if (RuneNameAt(text, i, rune.name, after)) {
                    flush();
                    pieces.push_back({text.substr(i, after - i), RuneGroupColor(rune.group), true});
                    i = after;
                    matched = any = true;
                    break;
                }
            }
        }
        if (!matched) {
            plain += text[i];
            ++i;
        }
    }
    flush();
    return any;
}

// Lays the pieces out wrapped to the rectangle's width, as DrawText does for words, and draws them
// when `draw` is set. Returns the height used.
int LayoutPieces(HDC dc, const std::vector<RunePiece>& source, COLORREF base, RECT rect, HFONT font,
                 UINT format, bool draw) {
    const std::vector<RunePiece> pieces = HighlightPieces(source);
    const HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    TEXTMETRICW metrics = {};
    GetTextMetricsW(dc, &metrics);
    const int lineHeight = std::max(1, static_cast<int>(metrics.tmHeight));
    const int maxWidth = std::max(1, static_cast<int>(rect.right - rect.left));

    struct Placed { int x; int line; int width; std::wstring text; COLORREF color; bool highlight = false; };
    std::vector<Placed> placed;
    std::vector<int> lineWidths;
    int x = 0;
    int line = 0;
    bool wordStart = true;
    std::vector<Placed> word;  // the current word; pieces that continue a word join it
    bool wordIsSpace = false;
    // Places the buffered word, moving the whole word to a new line first when it does not fit.
    auto flush = [&]() {
        if (word.empty()) return;
        int total = 0;
        for (const Placed& p : word) total += p.width;
        if (!wordIsSpace && x > 0 && x + total > maxWidth) {
            lineWidths.push_back(x);
            x = 0;
            ++line;
        }
        for (Placed& p : word) {
            p.x = x;
            p.line = line;
            x += p.width;
            placed.push_back(p);
        }
        word.clear();
    };
    for (const RunePiece& piece : pieces) {
        wordStart = !piece.continues;  // a continuation joins the word before it
        const COLORREF color = (piece.named || piece.colored) ? piece.color : base;
        std::size_t start = 0;
        while (start < piece.text.size()) {
            std::size_t end = start + 1;
            if (!piece.named) {
                if (piece.text[start] == L'\n') {
                    end = start + 1;
                } else {
                    const bool space = piece.text[start] == L' ';
                    while (end < piece.text.size() && piece.text[end] != L'\n' && (piece.text[end] == L' ') == space) ++end;
                }
            } else {
                end = piece.text.size();
            }
            const std::wstring token = piece.text.substr(start, end - start);
            start = end;
            if (token == L"\n") {
                flush();
                lineWidths.push_back(x);
                x = 0;
                ++line;
                wordStart = true;
                continue;
            }
            const bool isSpace = !piece.named && token[0] == L' ';
            SIZE size = {};
            GetTextExtentPoint32W(dc, token.c_str(), static_cast<int>(token.size()), &size);
            if (isSpace) {
                flush();
                if (x == 0) {  // no leading spaces on a wrapped line
                    wordStart = true;
                    continue;
                }
                wordIsSpace = true;
                word.push_back({0, line, static_cast<int>(size.cx), token, color, piece.highlight});
                flush();
            } else {
                if (wordStart) {
                    flush();
                    wordIsSpace = false;
                }
                word.push_back({0, line, static_cast<int>(size.cx), token, color, piece.highlight});
            }
            wordStart = isSpace;
        }
    }
    flush();
    lineWidths.push_back(x);
    const int height = placed.empty() ? 0 : (line + 1) * lineHeight;

    HBRUSH highlightBrush = CreateSolidBrush(kHighlightFill);
    if (draw && !placed.empty()) {
        int top = rect.top;
        if (format & DT_VCENTER) top += (static_cast<int>(rect.bottom - rect.top) - height) / 2;
        SetBkMode(dc, TRANSPARENT);
        for (const Placed& item : placed) {
            int offset = 0;
            const int used = lineWidths[static_cast<std::size_t>(item.line)];
            if (format & DT_CENTER) offset = (maxWidth - used) / 2;
            else if (format & DT_RIGHT) offset = maxWidth - used;
            if (item.highlight) {
                const int boxLeft = rect.left + offset + item.x;
                const int boxTop = top + item.line * lineHeight;
                RECT box = {boxLeft, boxTop, boxLeft + item.width, boxTop + lineHeight};
                FillRect(dc, &box, highlightBrush);
            }
            SetTextColor(dc, item.color);
            TextOutW(dc, rect.left + offset + item.x, top + item.line * lineHeight, item.text.c_str(),
                     static_cast<int>(item.text.size()));
        }
    }
    DeleteObject(highlightBrush);
    SelectObject(dc, previous);
    return height;
}

// True when the text has colour codes, so each run needs its own colour.
bool HasColorCodes(const std::string& text) {
    return text.find("\xC3\xBF" "c") != std::string::npos;
}

// The text as drawable pieces: each coded run takes its own colour; uncoded runs keep the base
// colour, with kanji rune names still coloured.
std::vector<RunePiece> LinePieces(const std::string& text, COLORREF base) {
    std::vector<RunePiece> pieces;
    for (const ColorRun& run : SplitColorCodes(text)) {
        if (run.text.empty()) continue;
        const std::wstring wide = Wide(run.text);
        if (run.code != '\0') {
            pieces.push_back({wide, CodeColor(run.code), false, true});
            continue;
        }
        std::vector<RunePiece> named;
        if (FindRuneNames(wide, named)) pieces.insert(pieces.end(), named.begin(), named.end());
        else pieces.push_back({wide, base, false, false});
    }
    return pieces;
}

void Text(HDC dc, const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format) {
    std::vector<RunePiece> pieces;
    const bool runes = FindRuneNames(text, pieces);
    if (runes || (!gHighlight.empty() && LowerWide(text).find(gHighlight) != std::wstring::npos)) {
        if (!runes) {
            // FindRuneNames may leave its own plain pieces behind when it finds no rune names.
            pieces.clear();
            pieces.push_back({text, color, false, false, false});
        }
        LayoutPieces(dc, pieces, color, rect, font, format, true);
        return;
    }
    const HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
    SelectObject(dc, previous);
}

// Heights already measured, keyed by text, width and font. Measuring runs GDI and the same text
// is measured on every repaint, so remembering the results keeps scrolling light. Cleared when
// the fonts change.
struct MeasureKey {
    std::size_t textHash;
    int width;
    HFONT font;
    bool operator==(const MeasureKey& other) const {
        return textHash == other.textHash && width == other.width && font == other.font;
    }
};
struct MeasureKeyHash {
    std::size_t operator()(const MeasureKey& key) const {
        return key.textHash ^ (static_cast<std::size_t>(key.width) * 1000003u) ^ std::hash<HFONT>()(key.font);
    }
};
std::unordered_map<MeasureKey, int, MeasureKeyHash> gMeasureCache;

// Height that `text` needs when wrapped to `width` in `font`.
int MeasureHeightUncached(HDC dc, const std::wstring& text, int width, HFONT font) {
    if (text.empty()) return 0;
    std::vector<RunePiece> pieces;
    if (FindRuneNames(text, pieces)) return LayoutPieces(dc, pieces, kBody, Rect(0, 0, width, 0), font, DT_LEFT, false);
    RECT measured = Rect(0, 0, width, 0);
    const HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measured,
              DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, previous);
    return measured.bottom - measured.top;
}

// Width of `text` on one line in `font` (no wrapping), used to fit a tooltip to its longest line.
int NaturalWidth(HDC dc, const std::wstring& text, HFONT font) {
    if (text.empty()) return 0;
    RECT measured = Rect(0, 0, 0, 0);
    const HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measured,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, previous);
    return measured.right - measured.left;
}

// Height that `text` needs when wrapped to `width` in `font`, remembered for repeat measures.
int MeasureHeight(HDC dc, const std::wstring& text, int width, HFONT font) {
    if (text.empty()) return 0;
    const MeasureKey key{std::hash<std::wstring>{}(text), width, font};
    const auto cached = gMeasureCache.find(key);
    if (cached != gMeasureCache.end()) return cached->second;
    const int height = MeasureHeightUncached(dc, text, width, font);
    if (gMeasureCache.size() > 50000) gMeasureCache.clear();
    gMeasureCache.emplace(key, height);
    return height;
}

// Width of `text` on one line in `font`.
// Drop shadows: a dark band under a panel (fading out), and a dark offset copy under header text.
void DropShadow(HDC dc, const RECT& rect, int px) {
    static const BYTE alphas[] = {150, 100, 60, 30};
    for (int k = 0; k < 4; ++k) {
        const RECT strip = Rect(rect.left + 2 * px, rect.bottom + k * px, rect.right - 2 * px, rect.bottom + (k + 1) * px);
        FillPanel(dc, strip, RGB(0, 0, 0), alphas[k]);
    }
}

void ShadowText(HDC dc, const std::wstring& text, RECT rect, HFONT font, UINT format, int px) {
    Text(dc, text, Rect(rect.left + px, rect.top + px, rect.right + px, rect.bottom + px), RGB(0, 0, 0), font, format);
}

// A shadow cast toward the bottom right: L-shaped bands just outside the rect, so the panel itself stays clear.
void CornerShadow(HDC dc, const RECT& rect, int px) {
    static const BYTE alphas[] = {150, 100, 60, 30};
    for (int k = 0; k < 4; ++k) {
        const int right = rect.right + k * px;   // this band's inner edge
        const int bottom = rect.bottom + k * px;
        const RECT side = Rect(right, rect.top + (k + 1) * px, right + px, bottom + px);
        const RECT foot = Rect(rect.left + (k + 1) * px, bottom, right + px, bottom + px);
        FillPanel(dc, side, RGB(0, 0, 0), alphas[k]);
        FillPanel(dc, foot, RGB(0, 0, 0), alphas[k]);
    }
}

int MeasureWidth(HDC dc, const std::wstring& text, HFONT font) {
    if (text.empty()) return 0;
    RECT measured = Rect(0, 0, 0, 0);
    const HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measured,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, previous);
    return measured.right - measured.left;
}

// Height of one line of text in `font`.
int LineHeight(HDC dc, HFONT font) {
    TEXTMETRICW metrics{};
    const HFONT previous = static_cast<HFONT>(SelectObject(dc, font));
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    return metrics.tmHeight;
}

// Largest visible top-level window of this process, which is the game window.
struct GameCandidate { HWND excluded = nullptr; HWND best = nullptr; long long area = 0; };

BOOL CALLBACK FindGameCallback(HWND window, LPARAM value) {
    auto& candidate = *reinterpret_cast<GameCandidate*>(value);
    if (window == candidate.excluded || !IsWindowVisible(window) || GetAncestor(window, GA_ROOT) != window) return TRUE;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    RECT client{};
    if (!GetClientRect(window, &client)) return TRUE;
    const long long area = static_cast<long long>(client.right - client.left) * (client.bottom - client.top);
    if (area > candidate.area) { candidate.area = area; candidate.best = window; }
    return TRUE;
}

HWND FindGameWindow(HWND overlay) {
    GameCandidate candidate{overlay};
    EnumWindows(FindGameCallback, reinterpret_cast<LPARAM>(&candidate));
    return candidate.best;
}

// Screen rectangle of the game's client area.
RECT GameScreenRect(HWND game) {
    RECT client{};
    GetClientRect(game, &client);
    POINT origin{0, 0};
    ClientToScreen(game, &origin);
    return Rect(origin.x, origin.y, origin.x + client.right, origin.y + client.bottom);
}

// Plain text of a color-coded string, with the color of its first code.
void PlainText(const std::string& text, std::string& plain, COLORREF& color, COLORREF fallback) {
    plain.clear();
    color = fallback;
    bool colored = false;
    for (const ColorRun& run : SplitColorCodes(text)) {
        if (!colored && run.code != '\0') {
            color = CodeColor(run.code);
            colored = true;
        }
        plain += run.text;
    }
}

// A hover target in the detail pane: a rune icon (rune >= 0) or a whole item tile (rune == -1).
struct DetailHit { RECT rect; const Entry* entry; const Block* block; int rune; };

// What the pointer is over; only item blocks and their runes ever get a hover.
struct Hover { const Entry* entry = nullptr; const Block* block = nullptr; int rune = -1; POINT at{0, 0}; RECT target{0, 0, 0, 0}; };

// Clicks on the blocker must not activate it (that would move keyboard focus off
// the panel), and closing it is ignored so Alt+F4 cannot remove the input block.
// The game keeps hiding and showing the system cursor, which flickers. Over the wiki the system
// cursor is hidden instead, and a drawn copy of the game's cursor (CursorProc) follows the pointer.
// True while a window of this process (the game or the wiki) is in front. A screenshot tool such as
// Win+Shift+S puts its own window in front; then the system cursor must show and the drawn one must go.
bool OwnWindowInFront() {
    const HWND front = GetForegroundWindow();
    if (front == nullptr) return true;
    DWORD process = 0;
    GetWindowThreadProcessId(front, &process);
    return process == GetCurrentProcessId();
}

// The game's own cursor, if the game has a system cursor to copy (null when it draws its own). It is
// remembered while the wiki is closed; its picture is drawn by the cursor window (RenderCursorImage).
// Showing the game's cursor handle itself does not work: the game keeps hiding it, so it flickers and
// vanishes while the pointer is still. Set to false to always draw the built-in gauntlet instead.
constexpr bool kUseGameCursor = true;
std::atomic<HCURSOR> gGameCursor{nullptr};
std::atomic<int> gCursorHotX{0};  // hot spot of the cursor image now drawn, in pixels
std::atomic<int> gCursorHotY{0};
std::atomic<int> gCursorDrawWidth{0};   // size of the cursor image now drawn, so tooltips can keep clear of it
std::atomic<int> gCursorDrawHeight{0};
// The screen area the wiki covers (the game window), for the drawn cursor: outside it the pointer belongs to other
// programs and shows their own cursor. Empty (all zero) means no limit.
std::atomic<int> gCoverLeft{0}, gCoverTop{0}, gCoverRight{0}, gCoverBottom{0};
bool PointInCover(const POINT& p) {
    const int r = gCoverRight.load(), b = gCoverBottom.load();
    if (r <= gCoverLeft.load() || b <= gCoverTop.load()) return true;
    return p.x >= gCoverLeft.load() && p.x < r && p.y >= gCoverTop.load() && p.y < b;
}

// Puts the input blocker exactly over the game window (its client area), so every click outside the game goes to
// the program under it as usual. Without a game window it covers all monitors, as before.
void PlaceBlocker(HWND blocker, HWND game) {
    if (!IsWindow(game)) {
        gCoverLeft = gCoverTop = gCoverRight = gCoverBottom = 0;
        if (IsWindow(blocker))
            SetWindowPos(blocker, nullptr, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                         GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    const RECT cover = GameScreenRect(game);
    gCoverLeft = cover.left; gCoverTop = cover.top; gCoverRight = cover.right; gCoverBottom = cover.bottom;
    if (IsWindow(blocker))
        SetWindowPos(blocker, nullptr, cover.left, cover.top, cover.right - cover.left, cover.bottom - cover.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

// Over the wiki the system cursor is hidden (the drawn one replaces it); with another program in front
// the normal arrow shows.
void HideSystemCursor() {
    SetCursor(OwnWindowInFront() ? nullptr : LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));  // IDC_ARROW
}

// While the wiki is closed: remembers the last cursor the game showed over its own window.
void RememberGameCursor() {
    if (!kUseGameCursor || !OwnWindowInFront()) return;
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING) || info.hCursor == nullptr) return;
    const HWND under = WindowFromPoint(info.ptScreenPos);
    DWORD process = 0;
    if (under != nullptr) GetWindowThreadProcessId(under, &process);
    if (process != GetCurrentProcessId()) return;  // the pointer is not over the game
    if (info.hCursor == LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))) return;  // the plain Windows arrow is not the game's
    gGameCursor = info.hCursor;
}

// The picture of a system cursor as premultiplied 0xAARRGGBB pixels, with its hot spot. False when it
// cannot be read (for example a black and white cursor).
bool CursorPixels(HCURSOR cursor, std::vector<std::uint32_t>& pixels, int& width, int& height, int& hotX, int& hotY) {
    ICONINFO icon{};
    if (!GetIconInfo(cursor, &icon)) return false;
    bool ok = false;
    BITMAP color{};
    if (icon.hbmColor != nullptr && GetObjectW(icon.hbmColor, sizeof(color), &color) != 0 && color.bmWidth > 0 &&
        color.bmHeight > 0 && color.bmWidth <= 512 && color.bmHeight <= 512) {
        width = color.bmWidth;
        height = color.bmHeight;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;  // top row first
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const HDC screen = GetDC(nullptr);
        pixels.assign(static_cast<std::size_t>(width) * height, 0);
        if (GetDIBits(screen, icon.hbmColor, 0, height, pixels.data(), &info, DIB_RGB_COLORS) == height) {
            bool hasAlpha = false;
            for (const std::uint32_t pixel : pixels) hasAlpha = hasAlpha || (pixel >> 24) != 0;
            if (hasAlpha) {
                for (std::uint32_t& pixel : pixels) {  // colours were not multiplied by alpha
                    const std::uint32_t a = pixel >> 24;
                    pixel = (a << 24) | ((((pixel >> 16) & 255) * a / 255) << 16) | ((((pixel >> 8) & 255) * a / 255) << 8) |
                            ((pixel & 255) * a / 255);
                }
                ok = true;
            } else if (icon.hbmMask != nullptr) {  // no alpha: the mask says which pixels are drawn
                std::vector<std::uint32_t> mask(pixels.size(), 0);
                if (GetDIBits(screen, icon.hbmMask, 0, height, mask.data(), &info, DIB_RGB_COLORS) == height) {
                    for (std::size_t i = 0; i < pixels.size(); ++i)
                        pixels[i] = (mask[i] & 0xFFFFFF) == 0 ? (pixels[i] | 0xFF000000u) : 0u;
                    ok = true;
                }
            }
        }
        ReleaseDC(nullptr, screen);
        hotX = static_cast<int>(icon.xHotspot);
        hotY = static_cast<int>(icon.yHotspot);
    }
    if (icon.hbmColor != nullptr) DeleteObject(icon.hbmColor);
    if (icon.hbmMask != nullptr) DeleteObject(icon.hbmMask);
    return ok;
}

// Cursor size as a percent of kCursorWidth (before display scaling). The wiki sets it from the game
// window's height, so the cursor grows and shrinks with the game's resolution.
std::atomic<int> gCursorScale{100};
int gRenderedCursorScale = 0;  // scale the cursor image was last drawn at (cursor thread only)

int CursorWidth(int dpi) { return std::max(1, MulDiv(MulDiv(kCursorWidth, gCursorScale.load(), 100), dpi, 96)); }
int CursorHeight(int dpi) { return std::max(1, MulDiv(MulDiv(kCursorHeight, gCursorScale.load(), 100), dpi, 96)); }

// Moves the cursor window to the pointer, but only when the pointer has moved.
void MoveCursorToPointer(HWND window) {
    POINT point{};
    if (!GetCursorPos(&point)) return;
    POINT* last = reinterpret_cast<POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (last != nullptr) {
        if (last->x == point.x && last->y == point.y) return;
        *last = point;
    }
    // The window sits up and to the left of the pointer so the cursor's hot spot is on the pointer.
    SetWindowPos(window, HWND_TOPMOST, point.x - gCursorHotX.load(), point.y - gCursorHotY.load(), 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
}

// Draws the cursor into the cursor window with per-pixel alpha: the game's own cursor picture when one was
// remembered (RememberGameCursor), at its own size, otherwise the built-in gauntlet (cursor_art.hpp, made
// by tools/make_cursor.py), sized for the dpi and the game's height. Done each time the window is shown.
void RenderCursorImage(HWND window) {
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    gRenderedCursorScale = gCursorScale.load();
    std::vector<std::uint32_t> game;
    int sourceWidth = kCursorWidth, sourceHeight = kCursorHeight;
    const std::uint32_t* source = kCursorPixels;
    int hotX = MulDiv(MulDiv(kCursorHotX, gCursorScale.load(), 100), dpi, 96);
    int hotY = MulDiv(MulDiv(kCursorHotY, gCursorScale.load(), 100), dpi, 96);
    int width = CursorWidth(dpi);
    int height = CursorHeight(dpi);
    const HCURSOR remembered = kUseGameCursor ? gGameCursor.load() : nullptr;
    int gameHotX = 0, gameHotY = 0;
    if (remembered != nullptr && CursorPixels(remembered, game, sourceWidth, sourceHeight, gameHotX, gameHotY)) {
        source = game.data();
        width = sourceWidth;  // the game's cursor is drawn at the size the game uses
        height = sourceHeight;
        hotX = gameHotX;
        hotY = gameHotY;
    } else {
        sourceWidth = kCursorWidth;
        sourceHeight = kCursorHeight;
    }
    gCursorHotX = hotX;
    gCursorHotY = hotY;
    gCursorDrawWidth = width;
    gCursorDrawHeight = height;
    const HDC screen = GetDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top row first
    void* bits = nullptr;
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap != nullptr && bits != nullptr) {
        // Scale by averaging the source pixels under each target pixel (one to one for the game's cursor).
        // The colours are already multiplied by alpha, so averaging every channel is correct. GDI scaling is
        // not used because it can clear the alpha channel.
        auto* out = static_cast<std::uint32_t*>(bits);
        for (int y = 0; y < height; ++y) {
            const int y0 = y * sourceHeight / height;
            const int y1 = std::max(y0 + 1, std::min(sourceHeight, ((y + 1) * sourceHeight + height - 1) / height));
            for (int x = 0; x < width; ++x) {
                const int x0 = x * sourceWidth / width;
                const int x1 = std::max(x0 + 1, std::min(sourceWidth, ((x + 1) * sourceWidth + width - 1) / width));
                std::uint32_t sum[4] = {0, 0, 0, 0};
                for (int sy = y0; sy < y1; ++sy) {
                    for (int sx = x0; sx < x1; ++sx) {
                        const std::uint32_t pixel = source[sy * sourceWidth + sx];
                        sum[0] += pixel >> 24;
                        sum[1] += (pixel >> 16) & 255;
                        sum[2] += (pixel >> 8) & 255;
                        sum[3] += pixel & 255;
                    }
                }
                const std::uint32_t count = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
                out[y * width + x] = ((sum[0] / count) << 24) | ((sum[1] / count) << 16) | ((sum[2] / count) << 8) |
                                     (sum[3] / count);
            }
        }
        const HDC memory = CreateCompatibleDC(screen);
        const HGDIOBJ old = SelectObject(memory, bitmap);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        POINT origin{0, 0};
        SIZE size{width, height};
        UpdateLayeredWindow(window, screen, nullptr, &size, memory, &origin, 0, &blend, ULW_ALPHA);
        SelectObject(memory, old);
        DeleteDC(memory);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    ReleaseDC(nullptr, screen);
}

// Click-through window that draws the game's cursor at the pointer. It lives on its own thread (CursorThread),
// so repaints of the wiki cannot delay it.
LRESULT CALLBACK CursorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    thread_local bool wanted = false;  // the wiki is open; only this thread reads or writes it
    switch (message) {
    case kMsgCursorShow:
        wanted = true;
        if (!OwnWindowInFront()) return 0;  // the timer shows it once the game is back in front
        if (IsWindow(window)) {
            POINT* last = reinterpret_cast<POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (last != nullptr) *last = POINT{-1, -1};
        }
        RenderCursorImage(window);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        MoveCursorToPointer(window);
        return 0;
    case kMsgCursorHide:
        wanted = false;
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_TIMER: {
        thread_local int polls = 0;
        if (!wanted && ++polls % 5 == 0) RememberGameCursor();  // about every 40 ms while the wiki is closed
        POINT pointer{};
        const bool inside = !GetCursorPos(&pointer) || PointInCover(pointer);
        const bool show = wanted && OwnWindowInFront() && inside;
        if (show && !IsWindowVisible(window)) {
                POINT* last = reinterpret_cast<POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (last != nullptr) *last = POINT{-1, -1};
            RenderCursorImage(window);
            ShowWindow(window, SW_SHOWNOACTIVATE);
        } else if (!show && IsWindowVisible(window)) {
            ShowWindow(window, SW_HIDE);
        }
        if (IsWindowVisible(window) && gRenderedCursorScale != gCursorScale.load()) {
            RenderCursorImage(window);  // the wiki's scale changed (window resized)
            POINT* last = reinterpret_cast<POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (last != nullptr) *last = POINT{-1, -1};
        }
        if (IsWindowVisible(window)) MoveCursorToPointer(window);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        HideSystemCursor();
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK BlockerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SETCURSOR) {
        HideSystemCursor();
        return TRUE;
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_CLOSE) return 0;
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

// One entry of a drop-down list on the IAS calculator page.
struct CalcOption {
    std::string label;
    int value = -1;
    std::string key;  // the weapon type, for the type lists
};

struct OverlayState {
    std::function<bool(unsigned int)> isCloseKey;  // see Overlay::SetCloseKey
    const Database* database = nullptr;
    std::mutex pendingLock;                  // guards pendingDb
    std::unique_ptr<Database> pendingDb;     // handed over by ReplaceDatabase, taken on the UI thread
    std::unique_ptr<Database> ownedDb;       // the database now shown, when it came from ReplaceDatabase
    std::wstring dataDir;
    HANDLE thread = nullptr;
    HANDLE ready = nullptr;
    HWND window = nullptr;
    HWND blocker = nullptr;
    HWND cursorWindow = nullptr;
    HDC backgroundDc = nullptr;  // background art composed once per window size
    HBITMAP backgroundBitmap = nullptr;
    HGDIOBJ backgroundPrevious = nullptr;
    int backgroundWidth = 0;
    int backgroundHeight = 0;
    int backgroundSplit = 0;  // header line position the cached background was built for
    POINT lastCursor{-1, -1};
    HANDLE cursorThread = nullptr;
    DWORD cursorThreadId = 0;
    HWND search = nullptr;
    HWND previousForeground = nullptr;
    ULONG_PTR gdiplusToken = 0;
    HFONT fontBody = nullptr;
    HFONT fontSmall = nullptr;
    HFONT fontBold = nullptr;
    HFONT fontTitle = nullptr;
    HBRUSH searchBrush = nullptr;
    HBRUSH backgroundBrush = nullptr;
    bool firstPainted = false;   // false until the current show has painted once
    ULONGLONG handoffStarted = 0;
    bool handoffSettled = false;
    bool handingOff = false;     // the other panel is taking over; this one is waiting to hide
    int dpi = 96;
    double uiScale = 1.0;  // font and text scale from the window size, on top of DPI
    int bodyLine = 16;     // line heights in pixels for the current fonts
    int smallLine = 14;
    int boldLine = 18;
    int width = 0;
    int height = 0;
    // Entry list geometry, set by ComputeLayout. EnsurePages splits the filtered entries into list pages,
    // so a row can grow to fit a wrapped title.
    int listLeftX = 0;
    int listRightX = 0;
    int listTopY = 0;
    int listBottomY = 0;
    int listRowGap = 0;
    int listTextWidth = 1;
    bool pagesDirty = true;
    std::vector<std::size_t> pageStarts{0};  // first filtered entry on each list page
    std::vector<int> rowHeights;             // row height for each filtered entry
    std::size_t pageIndex = 0;
    std::vector<PageView> views;
    std::string query;
    std::vector<std::size_t> filtered;
    std::vector<std::size_t> tabMatches;  // matching entries on each page, for the tab highlights
    std::size_t totalMatches = 0;         // matching entries across all pages
    bool hasPendingSelect = false;        // after clearing a search: keep this entry selected on this tab
    std::size_t pendingPage = 0;
    std::size_t pendingSelect = 0;
    std::size_t searchAnchor = 0;         // the entry being viewed when the search started
    std::size_t searchAnchorPage = 0;     // the tab that search started on
    std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> images;
    Layout layout;
    bool started = false;
    bool open = false;  // user wants it open; it only shows while the game is active
    std::wstring title = L"ESR Wiki";
    std::wstring windowClassName;
    std::wstring blockerClassName;
    std::wstring cursorClassName;
    std::vector<SwitchButton> switches;  // buttons that open the other panels, left of the close button
    void DrawSwitchButton(HDC dc, const RECT& rect, const std::wstring& label, bool current);
    void BeginSwitch(const std::function<void()>& onSwitch);
    bool greenNames = false;  // set-item pages draw names and headers in green
    bool gemwordCards = false;          // the Gemwords page: its cards are three per row
    COLORREF itemNameColor = kGold;     // item names on this page: gold uniques/runewords/gemwords, green sets, orange class crafting
    COLORREF tooltipNameColor = kGold;  // tooltip heading colour on this page (sets keep the plain gold)
    int titleLine = 18;       // line height of fontTitle
    bool trackingMouse = false;
    HWND gameWindow = nullptr;
    RECT fittedGameRect{};
    std::vector<DetailHit> detailHits;
    Hover hover;
    int clipTop = 0;        // visible band of the detail content, in client coordinates
    int clipBottom = 0;
    int scrollTotal = 0;    // content height and visible height from the last paint
    int scrollVisible = 0;
    std::vector<int> matchTops;  // scroll offsets of the blocks that match the search, from the last layout
    int matchBase = 0;           // the top of the detail band, for those offsets
    bool dragging = false;  // dragging the scrollbar thumb
    int dragStartY = 0;
    int dragStartScroll = 0;

    // The IAS calculator page. calcPending is handed over by Overlay::SetCalculatorData from another thread; the rest
    // belongs to the UI thread.
    std::mutex calcLock;
    std::shared_ptr<const ias::Data> calcPending;
    std::shared_ptr<const ias::Data> calcData;
    ias::Setup calcSetup;
    std::string calcMainType, calcOffType;  // the weapon type lists narrow the weapon lists
    int calcOpen = -1;        // the drop-down whose list is showing
    int calcListScroll = 0;   // its first visible row
    int calcNumFocus = -1;    // the number field being typed in
    std::string calcNumText;
    bool calcNumFresh = false;  // the next character replaces the whole value
    struct CalcHit { RECT rect; int kind; int index; };  // kind: 0 drop-down, 1 number field, 2 list row
    std::vector<CalcHit> calcHits;
    RECT calcListRect{};
    bool CalcActive() const;
    bool CalcClick(int x, int y);
    bool CalcWheel(int x, int y, int steps);
    bool CalcChar(wchar_t ch);
    int* CalcNumber(int id);
    void CalcChoose(int field, const CalcOption& option);
    int RenderCalculator(HDC dc, int top, int scroll, bool draw);

    const Page& CurrentPage() const { return database->pages[pageIndex]; }
    PageView& CurrentView() { return views[pageIndex]; }
    int S(int value) const { return MulDiv(value, dpi, 96); }
    int RuneRowHeight(const TileStyle& style) const { return style.cell + smallLine + S(4); }
    int RuneRows(std::size_t count, const TileStyle& style) const;
    TileStyle MakeTileStyle(int width_, bool compact, bool fiveAcross = false) const;

    HFONT MakeFont(int points, int weight) const;
    void CreateFonts();
    void ApplyFonts();
    Layout ComputeLayout();
    void FitToGame();
    void Refilter();
    void SetPage(std::size_t index);
    void SelectFiltered(std::size_t index);
    void EnsurePages();
    std::size_t PageOf(std::size_t index);
    std::vector<RECT> PageRects(std::size_t page);
    void MovePage(int direction);
    void StepMatch(int direction);
    bool StepWithinEntry(int direction);
    void Show();
    void Hide();
    void ApplyPendingDatabase();
    void FinishHandoff();
    void HandoffTick();
    void Toggle();
    void SyncVisibility();
    void ShowBlocker(bool show);
    void ShowCursorWindow(bool show);
    void HandleClick(int x, int y);
    void HandleWheel(int x, int y, int delta);
    void HandleKey(WPARAM key);
    void HandleMove(int x, int y);
    void EndDrag();
    void ClearHover();
    void QueryChanged();
    void Paint(HDC target);
    void PaintList(HDC dc);
    int BuildControls(HDC dc, const Entry& entry, PageView& view);
    void DrawScrollbar(HDC dc, int top, int visible, int total, int scroll);
    int TileHeight(HDC dc, const Block& block, const TileStyle& style);
    int TagRowHeight(HDC dc, const Block& block, const TileStyle& style);
    int RenderTiles(HDC dc, Gdiplus::Graphics* graphics, const Entry& entry, const std::vector<std::size_t>& order,
                    std::size_t begin, std::size_t end, int left, int width_, int y, bool compact, bool draw);
    int RenderWideTiles(HDC dc, Gdiplus::Graphics* graphics, const Entry& entry, const std::vector<std::size_t>& order,
                        std::size_t begin, std::size_t end, int left, int width_, int y, bool draw);
    int RenderGuideBlock(HDC dc, Gdiplus::Graphics* graphics, const Block& block, int left, int width_, int y,
                         bool draw);
    // An ascendancy: the tier lines in one half, the name and picture in the other, like the website.
    int RenderSplitBlock(HDC dc, Gdiplus::Graphics* graphics, const Block& block, int left, int width_, int y, bool draw);
    int splitNow = 0;  // the split layout of the entry being drawn (Entry::splitLayout), 0 for none
    std::string splitTitleNow;
    int RenderTable(HDC dc, const std::vector<Line>& lines, std::size_t first, std::size_t last, int left,
                    int width_, int y, bool draw);
    // Row heights per table, keyed by width and text, so scrolling does not re-measure every cell.
    std::unordered_map<std::string, std::vector<int>> tableHeightCache;
    // The width each column needs for its longest word, per table, for the same reason.
    std::unordered_map<std::string, std::vector<int>> tableNeedCache;
    // Table rows in the order of the active row sort, per block, so painting does not sort again.
    BlockSort rowSortNow = BlockSort::Default;
    BlockSort sortedRowsFor = BlockSort::Default;
    const Database* sortedRowsDb = nullptr;
    std::unordered_map<const Block*, std::vector<Line>> sortedRows;
    // The block order of the detail pane, reused until one of its inputs changes.
    struct ArrangeKey {
        const Database* db;
        std::size_t page;
        std::size_t entry;
        std::size_t socket;
        std::string tier;
        std::string family;
        std::string query;
        BlockSort sort;
        bool operator==(const ArrangeKey& other) const {
            return db == other.db && page == other.page && entry == other.entry && socket == other.socket &&
                   sort == other.sort && tier == other.tier && family == other.family && query == other.query;
        }
    };
    ArrangeKey arrangeKey{};
    std::vector<std::size_t> arrangeOrder;
    bool arrangeValid = false;
    std::vector<std::size_t> ArrangedOrder(const Entry& entry, std::size_t entryIndex, const PageView& view);
    int RenderDetail(HDC dc, const Entry& entry, const std::vector<std::size_t>& order, bool headers, int top,
                     int scroll, bool compact, bool draw);
    void DrawImageKey(Gdiplus::Graphics& graphics, const std::string& key, RECT box);
    void DrawBackground(Gdiplus::Graphics& graphics, RECT client, int headerLineY);
    void EnsureBackgroundCache(HDC target, int width, int height);
    void ReleaseBackgroundCache();
    void DrawTooltip(HDC dc);
    Gdiplus::Bitmap* FindImage(const std::string& key);
};

int OverlayState::RuneRows(std::size_t count, const TileStyle& style) const {
    const std::size_t perRow = static_cast<std::size_t>(std::max(1, (style.tileWidth - S(12)) / style.cell));
    return static_cast<int>((count + perRow - 1) / perRow);
}

// Tiles follow the detail pane width: normal item tiles are three per row with
// art at three quarters of the tile width; compact rune tiles are small, with
// fixed-size art (about a quarter of the normal size) and up to six per row.
TileStyle OverlayState::MakeTileStyle(int width_, bool compact, bool fiveAcross) const {
    TileStyle style;
    style.columns = fiveAcross ? 5 : gemwordCards ? 3 : compact ? (width_ >= S(560) ? 6 : 4) : 3;
    style.gap = compact || fiveAcross ? S(6) : S(12);
    style.tileWidth = std::max(S(40), (width_ - style.gap * (style.columns - 1)) / style.columns);
    if (gemwordCards) {
        // Three per row, but each card keeps the width it has at the compact six (or four) per row, so the
        // row is left-aligned with space to its right.
        const int compactColumns = width_ >= S(560) ? 6 : 4;
        style.tileWidth = std::max(S(40), (width_ - S(6) * (compactColumns - 1)) / compactColumns);
    }
    style.padding = S(6);
    style.nameFont = compact || fiveAcross ? fontSmall : fontBold;
    if (compact) {
        style.artBox = std::max(S(20), std::min(S(35), style.tileWidth - S(12)));
        style.cell = S(30);
    } else {
        style.artBox = std::max(S(30), std::min(style.tileWidth / 2 - S(10), style.tileWidth * 3 / 8));
        style.cell = std::max(S(30), std::min(S(40), (style.tileWidth - S(12)) / 6));
    }
    return style;
}

HFONT OverlayState::MakeFont(int points, int weight) const {
    const int pixelHeight = -static_cast<int>(std::lround(points * uiScale * dpi / 72.0));
    return CreateFontW(pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void OverlayState::CreateFonts() {
    const HDC screen = GetDC(nullptr);
    dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    uiScale = 1.0;
    ApplyFonts();
}

// Rebuilds the fonts for the current uiScale and re-measures their line heights.
void OverlayState::ApplyFonts() {
    for (HFONT* font : {&fontBody, &fontSmall, &fontBold, &fontTitle}) {
        if (*font != nullptr) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
    tableHeightCache.clear();
    gMeasureCache.clear();
    fontBody = MakeFont(10, FW_NORMAL);
    fontSmall = MakeFont(8, FW_NORMAL);
    fontBold = MakeFont(10, FW_SEMIBOLD);
    fontTitle = MakeFont(13, FW_SEMIBOLD);

    const HDC screen = GetDC(nullptr);
    bodyLine = LineHeight(screen, fontBody);
    smallLine = LineHeight(screen, fontSmall);
    boldLine = LineHeight(screen, fontBold);
    titleLine = LineHeight(screen, fontTitle);
    ReleaseDC(nullptr, screen);
    if (IsWindow(search)) SendMessageW(search, WM_SETFONT, reinterpret_cast<WPARAM>(fontBody), TRUE);
}

// Positions everything for the current width and height. Row heights come from
// the bold font, so two-line titles fit at any scale, and the list length follows
// the window height.
Layout OverlayState::ComputeLayout() {
    Layout computed;
    const auto& pages = database->pages;
    // Content sits inside the background picture's frame, with room to breathe.
    const int margin = kFrameSlice + S(10);

    // Pages are grouped into tab rows by their group name, in order of appearance.
    std::vector<std::string> rowNames;
    std::vector<int> rowOfPage(pages.size(), 0);
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto found = std::find(rowNames.begin(), rowNames.end(), pages[i].group);
        if (found == rowNames.end()) {
            rowNames.push_back(pages[i].group);
            rowOfPage[i] = static_cast<int>(rowNames.size()) - 1;
        } else {
            rowOfPage[i] = static_cast<int>(found - rowNames.begin());
        }
    }
    std::vector<int> pagesInRow(rowNames.size(), 0);
    for (int row : rowOfPage) ++pagesInRow[static_cast<std::size_t>(row)];

    // One spacing scale: 16 outer margin, 8 between neighbouring blocks, 4 between rows inside a block.
    const int gap = S(8);
    const int tight = S(4);
    const int right = width - margin;

    // Header: the switch and close buttons share one band, right-aligned with the tabs.
    // The header is raised 10 px from its centred spot, so it sits closer to the picture's top frame. The header
    // line and the tabs stay where they were.
    const int headerLine = kFrameSlice + S(6);  // where the header would sit with the header line below it
    const int headerTop = headerLine - S(10);  // clear of the picture's top frame, which grows with the window
    const int headerHeight = S(28);
    // The title, switch buttons and close button share one row, 3 px below the header top, so their centres line up.
    const int rowTop = headerTop + S(3);
    // The buttons sit on the title's row, centred with the title's text.
    const int buttonTop = rowTop + S(1);
    computed.close = Rect(right - S(32), buttonTop, right, buttonTop + headerHeight);
    // The switch buttons sit left of the close button, the first one leftmost; the title takes the rest.
    computed.switchButtons.resize(switches.size());
    int switchRight = computed.close.left - gap;
    for (std::size_t i = switches.size(); i-- > 0;) {
        computed.switchButtons[i] = Rect(switchRight - S(140), buttonTop, switchRight, buttonTop + headerHeight);
        switchRight = computed.switchButtons[i].left - gap;
    }
    computed.title = Rect(margin, rowTop, switchRight, rowTop + headerHeight);

    // Tabs: full width between the margins, equal gaps across and down.
    const int tabRowHeight = std::max(S(30), bodyLine + S(10));
    const int tabGap = gap;
    const int tabTop = headerLine + headerHeight + gap + S(2);
    computed.tabs.resize(pages.size());
    std::vector<int> seenInRow(rowNames.size(), 0);
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto row = static_cast<std::size_t>(rowOfPage[i]);
        const int count = pagesInRow[row];
        const int column = seenInRow[row]++;
        const int span = right - margin - (count - 1) * tabGap;
        const int left = margin + span * column / count + column * tabGap;
        const int tabRight = margin + span * (column + 1) / count + column * tabGap;
        const int top = tabTop + static_cast<int>(row) * (tabRowHeight + tabGap);
        computed.tabs[i] = Rect(left, top, tabRight, top + tabRowHeight);
    }

    const int contentTop = tabTop + static_cast<int>(rowNames.size()) * (tabRowHeight + tabGap);
    // The picture's header line sits under the title row, so the tabs sit on the plain content
    // background rather than inside the header art.
    computed.separatorY = headerLine + headerHeight + S(6);
    const int bottom = height - margin;
    const int listWidth = Bounded(static_cast<int>(S(340) * uiScale), S(260), std::max(S(260), width * 38 / 100)) * 85 / 100 * 70 / 100;
    const int listRight = margin + listWidth;

    // List column: search, match count and buttons, rows, then paging pinned to the bottom edge.
    // Tall enough for the body font plus the control border, so the text caret fits inside the box.
    const int searchHeight = std::max(S(28), bodyLine + S(10));
    const int countHeight = S(22);
    const int buttonWidth = S(24);
    const int countTop = contentTop + searchHeight + tight;
    computed.search = Rect(margin, contentTop, listRight, contentTop + searchHeight);
    computed.clearQuery = Rect(listRight - searchHeight, contentTop, listRight, contentTop + searchHeight);
    computed.matchNext = Rect(listRight - buttonWidth, countTop, listRight, countTop + countHeight);
    computed.matchPrev = Rect(computed.matchNext.left - tight - buttonWidth, countTop,
                              computed.matchNext.left - tight, countTop + countHeight);
    computed.count = Rect(margin, countTop, computed.matchPrev.left - tight, countTop + countHeight);

    const int pagingHeight = std::max(S(30), bodyLine + S(10));
    const int listTop = countTop + countHeight + gap;
    const int pagingTop = bottom - pagingHeight;
    // The rows themselves are placed per list page once their titles are measured (see EnsurePages).
    listLeftX = margin;
    listRightX = listRight;
    listTopY = listTop;
    listBottomY = pagingTop - gap;
    listRowGap = tight;
    listTextWidth = std::max(S(20), listWidth - S(24));
    pagesDirty = true;
    const int pagingButton = S(80);
    computed.prev = Rect(margin, pagingTop, margin + pagingButton, bottom);
    computed.next = Rect(listRight - pagingButton, pagingTop, listRight, bottom);
    computed.pageLabel = Rect(computed.prev.right + gap, pagingTop, computed.next.left - gap, bottom);

    // Detail pane: one margin right of the list, same top and bottom edges as the list column.
    computed.detail = Rect(listRight + margin, contentTop, right, bottom);
    return computed;
}

// Sizes the window to the game: about 80% of the game's width and 90% of its
// height, clamped to a sensible range and centered over the game. Fonts scale with
// the resulting width. Without a game window it is centered on the screen.
void OverlayState::FitToGame() {
    if (!IsWindow(gameWindow)) gameWindow = FindGameWindow(window);
    int left = 0;
    int top = 0;
    int w = S(960);
    int h = S(620);
    if (IsWindow(gameWindow)) {
        const RECT game = GameScreenRect(gameWindow);
        const int cw = game.right - game.left;
        const int ch = game.bottom - game.top;
        w = std::min(Bounded(cw * 4 / 5, S(960), S(1500)), cw);
        h = std::min(Bounded(ch * 9 / 10, S(620), S(1000)), ch);
        left = game.left + (cw - w) / 2;
        top = game.top + (ch - h) / 2;
        fittedGameRect = game;
    } else {
        left = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        top = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        fittedGameRect = RECT{};
    }

    PlaceBlocker(blocker, gameWindow);

    const double scale = std::clamp(static_cast<double>(w) / S(960), 0.85, 1.5);
    if (std::fabs(scale - uiScale) > 0.02) {
        uiScale = scale;
        ApplyFonts();
    }
    // The cursor follows the game window's height, relative to the display scaling. Tuned on a 150% display:
    // 150 px wide in a 2160 px high window and 62 px wide at 1080p, which is a little more than linear.
    // Display scaling does not change it (the 1440 is a height in 96 dpi units).
    gCursorScale = std::clamp(static_cast<int>(std::lround(80.0 * std::pow(static_cast<double>(h) / S(1440), 1.27))), 20, 150);

    width = w;
    height = h;
    RECT current{};
    GetWindowRect(window, &current);
    if (current.left != left || current.top != top || current.right - current.left != w ||
        current.bottom - current.top != h) {
        SetWindowPos(window, nullptr, left, top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    layout = ComputeLayout();
    if (IsWindow(search)) {
        const RECT edit = SearchEditRect(layout, bodyLine, S(6));
        MoveWindow(search, edit.left, edit.top, edit.right - edit.left, edit.bottom - edit.top, TRUE);
    }
}

Gdiplus::Bitmap* OverlayState::FindImage(const std::string& key) {
    const auto cached = images.find(key);
    if (cached != images.end()) return cached->second.get();
    const std::wstring path = dataDir + L"\\images\\" + Wide(key) + L".png";
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str());
    if (bitmap->GetLastStatus() != Gdiplus::Ok) bitmap.reset();
    Gdiplus::Bitmap* raw = bitmap.get();
    images.emplace(key, std::move(bitmap));
    return raw;
}

// Background art: the picture's frame is drawn at its own pixel size and only the middle is stretched, so the
// frame stays thin at any window size. The picture's header band is stretched so its bottom line lands at
// `headerLineY`, between the tabs and the content. A dark tint keeps the text readable.
void OverlayState::DrawBackground(Gdiplus::Graphics& graphics, RECT client, int headerLineY) {
    Gdiplus::Bitmap* bitmap = FindImage("background");
    const int areaWidth = client.right - client.left;
    const int areaHeight = client.bottom - client.top;
    if (bitmap == nullptr || areaWidth <= 0 || areaHeight <= 0) return;
    const int sourceWidth = static_cast<int>(bitmap->GetWidth());
    const int sourceHeight = static_cast<int>(bitmap->GetHeight());
    const int side = std::min(kFrameSlice, sourceWidth / 4);
    const int headerSource = std::min(kHeaderSlice, sourceHeight / 2);
    const int footer = side;
    if (sourceWidth <= 0 || sourceHeight <= 0 || areaWidth < 4 * side || areaHeight < headerSource / 2 + 3 * side) return;
    const int headerDest = std::clamp(headerLineY + 1, side, areaHeight - footer - side);

    const int sourceX[4] = {0, side, sourceWidth - side, sourceWidth};
    const int sourceY[4] = {0, headerSource, sourceHeight - footer, sourceHeight};
    const int destX[4] = {0, side, areaWidth - side, areaWidth};
    const int destY[4] = {0, headerDest, areaHeight - footer, areaHeight};

    Gdiplus::ImageAttributes attributes;
    attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);  // no seams where the pieces meet
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            graphics.DrawImage(bitmap,
                               Gdiplus::Rect(destX[column], destY[row], destX[column + 1] - destX[column],
                                             destY[row + 1] - destY[row]),
                               sourceX[column], sourceY[row], sourceX[column + 1] - sourceX[column],
                               sourceY[row + 1] - sourceY[row], Gdiplus::UnitPixel, &attributes);
        }
    }
    Gdiplus::SolidBrush tint(Gdiplus::Color(150, 14, 14, 14));
    graphics.FillRectangle(&tint, 0, 0, areaWidth, areaHeight);
}

// Builds the background once for the current window size, so repaints only copy it.
void OverlayState::EnsureBackgroundCache(HDC target, int areaWidth, int areaHeight) {
    if (backgroundBitmap != nullptr && areaWidth == backgroundWidth && areaHeight == backgroundHeight &&
        layout.separatorY == backgroundSplit) return;
    ReleaseBackgroundCache();
    backgroundDc = CreateCompatibleDC(target);
    backgroundBitmap = CreateCompatibleBitmap(target, areaWidth, areaHeight);
    backgroundPrevious = SelectObject(backgroundDc, backgroundBitmap);
    const RECT rect{0, 0, areaWidth, areaHeight};
    FillColor(backgroundDc, rect, kBackground);
    {
        Gdiplus::Graphics graphics(backgroundDc);
        DrawBackground(graphics, rect, layout.separatorY);
    }
    backgroundSplit = layout.separatorY;
    backgroundWidth = areaWidth;
    backgroundHeight = areaHeight;
}

void OverlayState::ReleaseBackgroundCache() {
    if (backgroundDc != nullptr) {
        SelectObject(backgroundDc, backgroundPrevious);
        DeleteDC(backgroundDc);
        backgroundDc = nullptr;
    }
    if (backgroundBitmap != nullptr) {
        DeleteObject(backgroundBitmap);
        backgroundBitmap = nullptr;
    }
    backgroundWidth = 0;
    backgroundHeight = 0;
}

// Display size of some mythical art, as a share of its fitted size (1.0 = unchanged).
static double ImageDisplayScale(const std::string& key) {
    if (key == "mythical_rathmaseternalskull") return 0.35;  // reduced 65%
    if (key == "mythical_eternalhourglassofthetimelesssands") return 0.70;  // reduced 30%
    if (key == "mythical_baalsvilewhisper") return 0.75;  // reduced 25%
    // Mythical rings, amulets and the Ember (jewel) of Creation: reduced 35%
    if (key == "mythical_mythicalhighlordswrath" || key == "mythical_thetiesthatbind" || key == "mythical_vesselofsouls" ||
        key == "mythical_emberofcreation" || key == "mythical_lichkingsessence") return 0.65;
    return 1.0;
}

void OverlayState::DrawImageKey(Gdiplus::Graphics& graphics, const std::string& key, RECT box) {
    Gdiplus::Bitmap* bitmap = FindImage(key);
    if (bitmap == nullptr) return;
    const double imageWidth = bitmap->GetWidth();
    const double imageHeight = bitmap->GetHeight();
    if (imageWidth <= 0 || imageHeight <= 0) return;
    const double boxWidth = box.right - box.left;
    const double boxHeight = box.bottom - box.top;
    const double scale = std::min(boxWidth / imageWidth, boxHeight / imageHeight) * ImageDisplayScale(key);
    const int drawWidth = static_cast<int>(imageWidth * scale);
    const int drawHeight = static_cast<int>(imageHeight * scale);
    const int left = box.left + (static_cast<int>(boxWidth) - drawWidth) / 2;
    const int top = box.top + (static_cast<int>(boxHeight) - drawHeight) / 2;
    graphics.DrawImage(bitmap, left, top, drawWidth, drawHeight);
}

// Moves the selection to the next or previous matching entry, wrapping at either end.
void OverlayState::StepMatch(int direction) {
    if (query.empty() || totalMatches == 0) return;
    // First the matches inside the entry being viewed; past the last one, the next entry with matches.
    if (StepWithinEntry(direction)) {
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    PageView& view = CurrentView();
    if (direction > 0 && view.selected + 1 < filtered.size()) {
        SelectFiltered(view.selected + 1);
    } else if (direction < 0 && view.selected > 0) {
        SelectFiltered(view.selected - 1);
    } else {
        // Past the end of this tab: go to the next tab with matches, wrapping around the panel.
        const std::size_t pages = database->pages.size();
        for (std::size_t step = 1; step <= pages; ++step) {
            const std::size_t index = direction > 0 ? (pageIndex + step) % pages : (pageIndex + pages - step) % pages;
            if (tabMatches[index] == 0) continue;
            if (index == pageIndex) {
                SelectFiltered(direction > 0 ? 0 : filtered.size() - 1);
            } else {
                SetPage(index);  // Refilter selects the first match on the new tab
                if (direction < 0) SelectFiltered(filtered.size() - 1);
            }
            break;
        }
    }
    InvalidateRect(window, nullptr, FALSE);
}

// Moves the open entry to its next matching block in the direction given, scrolling that block to the top of the
// view. False when no match is left that way, so the caller moves on to another entry.
bool OverlayState::StepWithinEntry(int direction) {
    PageView& view = CurrentView();
    const int current = view.detailScroll;
    const int maxScroll = std::max(0, scrollTotal - scrollVisible);
    bool found = false;
    int target = 0;
    for (const int offset : matchTops) {
        const int scroll = std::min(offset, maxScroll);  // the last matches cannot scroll past the end
        if (direction > 0 && scroll > current && (!found || scroll < target)) {
            target = scroll;
            found = true;
        } else if (direction < 0 && scroll < current && (!found || scroll > target)) {
            target = scroll;
            found = true;
        }
    }
    if (!found) return false;
    view.detailScroll = target;
    return true;
}

// The search box passes Enter to the overlay as the next match, and Shift+Enter as the previous.
WNDPROC gSearchProc = nullptr;

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if ((message == WM_KEYDOWN || message == WM_CHAR) && wParam == VK_RETURN) {
        if (message == WM_KEYDOWN) {
            auto* self = reinterpret_cast<OverlayState*>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
            if (self && self->CalcActive() && self->calcNumFocus >= 0) {
                self->calcNumFocus = -1;  // Enter finishes the number being typed
                InvalidateRect(self->window, nullptr, FALSE);
            } else if (self) {
                self->StepMatch((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            }
        }
        return 0;
    }
    if (message == WM_CHAR) {
        // A number field of the IAS calculator page takes the typing instead of the search box.
        auto* self = reinterpret_cast<OverlayState*>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
        if (self != nullptr && self->CalcChar(static_cast<wchar_t>(wParam))) return 0;
    }
    return CallWindowProcW(gSearchProc, hwnd, message, wParam, lParam);
}

void OverlayState::Refilter() {
    wchar_t buffer[256] = {};
    GetWindowTextW(search, buffer, 255);
    const bool wasSearching = !query.empty();
    const PageView& before = CurrentView();
    const std::size_t selectedEntry = before.selected < filtered.size() ? filtered[before.selected] : 0;
    query = Narrow(buffer);
    if (!query.empty()) {
        hasPendingSelect = false;
        if (!wasSearching) {  // a new search begins where the user is looking
            searchAnchor = selectedEntry;
            searchAnchorPage = pageIndex;
        }
    } else if (wasSearching) {
        // Clearing the search keeps the tab and the entry being viewed in the search results.
        hasPendingSelect = true;
        pendingPage = pageIndex;
        pendingSelect = selectedEntry;
    }
    gHighlight = LowerWide(Wide(query));
    filtered = FilterEntries(CurrentPage(), query);
    pagesDirty = true;
    tabMatches.assign(database->pages.size(), 0);
    totalMatches = 0;
    for (std::size_t i = 0; i < database->pages.size(); ++i) {
        tabMatches[i] = i == pageIndex ? filtered.size() : FilterEntries(database->pages[i], query).size();
        totalMatches += tabMatches[i];
    }
    PageView& view = CurrentView();
    view.selected = 0;
    view.listPage = 0;
    view.detailScroll = 0;
    // While searching on the tab the search began on, select the first match at or after the entry that was being
    // viewed (wrapping to the first match when there is none after it).
    if (!query.empty() && searchAnchorPage == pageIndex && !filtered.empty()) {
        const auto at = std::find_if(filtered.begin(), filtered.end(),
                                     [this](std::size_t entry) { return entry >= searchAnchor; });
        view.selected = at == filtered.end() ? 0 : static_cast<std::size_t>(at - filtered.begin());
        view.listPage = PageOf(view.selected);
    }
    // Keeps the viewed entry selected once the search is cleared. Applied on every refilter until
    // the search is typed in again, so a second refilter cannot reset it.
    if (query.empty() && hasPendingSelect && pendingPage == pageIndex && !filtered.empty()) {
        view.selected = std::min(pendingSelect, filtered.size() - 1);
        view.listPage = PageOf(view.selected);
    }
}

void OverlayState::SetPage(std::size_t index) {
    if (index >= database->pages.size() || index == pageIndex) return;
    pageIndex = index;
    calcOpen = -1;
    calcNumFocus = -1;
    Refilter();
}

// Splits the filtered entries into list pages. A row is at least the usual height and grows to fit its
// wrapped title; a page holds the rows that fit above the paging buttons.
void OverlayState::EnsurePages() {
    if (!pagesDirty) return;
    pagesDirty = false;
    const int baseHeight = std::max(S(48), boldLine * 2 + S(10));
    rowHeights.assign(filtered.size(), baseHeight);
    HDC dc = GetDC(window);
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        const Entry& entry = CurrentPage().entries[filtered[i]];
        const int titleHeight = MeasureHeight(dc, UiDataText(Wide(entry.title)), listTextWidth, fontBold);
        rowHeights[i] = std::max(baseHeight, titleHeight + S(8));
    }
    ReleaseDC(window, dc);
    pageStarts.assign(1, 0);
    int y = listTopY;
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        if (y != listTopY && y + rowHeights[i] > listBottomY) {
            pageStarts.push_back(i);
            y = listTopY;
        }
        y += rowHeights[i] + listRowGap;
    }
}

std::size_t OverlayState::PageOf(std::size_t index) {
    EnsurePages();
    const auto after = std::upper_bound(pageStarts.begin(), pageStarts.end(), index);
    return static_cast<std::size_t>(after - pageStarts.begin()) - 1;
}

// The rectangles of the rows on one list page, top to bottom.
std::vector<RECT> OverlayState::PageRects(std::size_t page) {
    EnsurePages();
    std::vector<RECT> rects;
    const std::size_t first = pageStarts[page];
    const std::size_t last = page + 1 < pageStarts.size() ? pageStarts[page + 1] : filtered.size();
    int y = listTopY;
    for (std::size_t i = first; i < last; ++i) {
        rects.push_back(Rect(listLeftX, y, listRightX, y + rowHeights[i]));
        y += rowHeights[i] + listRowGap;
    }
    return rects;
}

void OverlayState::SelectFiltered(std::size_t index) {
    if (index >= filtered.size()) return;
    PageView& view = CurrentView();
    view.selected = index;
    view.listPage = PageOf(index);
    view.detailScroll = 0;
}

void OverlayState::MovePage(int direction) {
    if (filtered.empty()) return;
    PageView& view = CurrentView();
    EnsurePages();
    const auto pages = pageStarts.size();
    const auto current = view.listPage;
    if (direction < 0 && current == 0) return;
    if (direction > 0 && current + 1 >= pages) return;
    view.listPage = direction < 0 ? current - 1 : current + 1;
    view.selected = pageStarts[view.listPage];
    view.detailScroll = 0;
}

void OverlayState::Show() {
    if (handingOff) {  // switched back before the handoff finished
        KillTimer(window, kTimerHandoff);
        handingOff = false;
        ReleaseHandoffBlocker();
    }
    open = true;
    if (gHandoffForeground != nullptr) {
        previousForeground = gHandoffForeground;
        gHandoffForeground = nullptr;
    } else {
        previousForeground = GetForegroundWindow();
    }
    FitToGame();
    SyncVisibility();
    if (IsWindowVisible(window)) {
        SetForegroundWindow(window);
        SetFocus(search);
    }
    InvalidateRect(window, nullptr, FALSE);
    if (IsWindowVisible(window)) {
        UpdateWindow(window);  // painted before the outgoing panel is allowed to hide
        gHandoffReady = true;
    }
}

// Called after the other panel has had time to appear: hides this panel and its blocker.
// Polls until the incoming panel reports it is painted, waits one more tick so the compositor has
// presented it, then lets this panel go. A timeout keeps a failed handoff from leaving a stuck panel.
void OverlayState::HandoffTick() {
    if (!handingOff) {
        KillTimer(window, kTimerHandoff);
        return;
    }
    const bool timedOut = GetTickCount64() - handoffStarted > kHandoffTimeoutMs;
    if (!timedOut) {
        if (!gHandoffReady.load()) return;
        if (!handoffSettled) {
            handoffSettled = true;
            return;
        }
    }
    FinishHandoff();
}

void OverlayState::FinishHandoff() {
    KillTimer(window, kTimerHandoff);
    if (!handingOff) return;
    handingOff = false;
    ReleaseHandoffBlocker();
    ShowBlocker(false);
    ShowWindow(window, SW_HIDE);
    firstPainted = false;
}

void OverlayState::Hide() {
    if (handingOff) {
        KillTimer(window, kTimerHandoff);
        handingOff = false;
        ReleaseHandoffBlocker();
    }
    open = false;
    firstPainted = false;
    EndDrag();
    ClearHover();
    ShowBlocker(false);
    ShowWindow(window, SW_HIDE);
    if (previousForeground != nullptr && previousForeground != window && IsWindow(previousForeground)) {
        SetForegroundWindow(previousForeground);
    }
}

// Swaps in the database handed over by Overlay::ReplaceDatabase. Runs on the UI thread only, so nothing
// is drawing while the pointers change. Everything that points into the old data is dropped.
void OverlayState::ApplyPendingDatabase() {
    std::unique_ptr<Database> next;
    {
        std::lock_guard<std::mutex> guard(pendingLock);
        next = std::move(pendingDb);
    }
    if (!next || next->pages.empty()) return;
    EndDrag();
    ClearHover();
    detailHits.clear();
    tableHeightCache.clear();
    arrangeKey = ArrangeKey{};
    ownedDb = std::move(next);
    database = ownedDb.get();
    views.assign(database->pages.size(), PageView{});
    if (pageIndex >= database->pages.size()) pageIndex = 0;
    hasPendingSelect = false;
    FitToGame();  // the tab layout depends on the pages
    Refilter();
    InvalidateRect(window, nullptr, FALSE);
}

void OverlayState::Toggle() {
    if (open) Hide();
    else Show();
}

void OverlayState::ShowCursorWindow(bool show) {
    PostMessageW(cursorWindow, show ? kMsgCursorShow : kMsgCursorHide, 0, 0);
}

void OverlayState::ShowBlocker(bool show) {
    ShowCursorWindow(show);
    if (!IsWindow(blocker)) return;
    if (!show) {
        ShowWindow(blocker, SW_HIDE);
        return;
    }
    if (IsWindow(gameWindow) && GetWindow(blocker, GW_OWNER) != gameWindow)
        SetWindowLongPtrW(blocker, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(gameWindow));
    PlaceBlocker(blocker, gameWindow);
    // Shown fully transparent and then faded to its strength, so the first frame is never opaque black.
    SetLayeredWindowAttributes(blocker, 0, 0, LWA_ALPHA);
    SetWindowPos(blocker, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (gHandoffActive) gHandoffBlocker = blocker;  // raised when the outgoing panel lets go
    else SetLayeredWindowAttributes(blocker, 0, kBlockerAlpha, LWA_ALPHA);
}

// Runs on a timer. The overlay is owned by the game window, so it stays above the
// game without being topmost. While it is open and the game is active, the
// blocker covers the screen and the panel holds keyboard focus. It is hidden
// whenever another app is in focus, and it follows the game when the game moves.
void OverlayState::SyncVisibility() {
    if (!IsWindow(window)) return;
    if (!IsWindow(gameWindow)) gameWindow = FindGameWindow(window);
    bool gameActive = false;
    if (IsWindow(gameWindow)) {
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        gameActive = foregroundProcess == GetCurrentProcessId();
        if (GetWindow(window, GW_OWNER) != gameWindow)
            SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(gameWindow));
    }
    if (handingOff) return;  // the other panel is taking over; FinishHandoff hides this one
    const bool shouldShow = open && gameActive;
    const bool shown = IsWindowVisible(window) != FALSE;
    if (!shouldShow) {
        if (shown) {
            EndDrag();
            ClearHover();
            ShowBlocker(false);
            ShowWindow(window, SW_HIDE);
            firstPainted = false;
        }
        return;
    }
    if (!shown) {
        FitToGame();
        ShowBlocker(true);
        // Shown fully transparent, painted once, then made opaque: no unpainted frame is ever visible.
        SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(window, nullptr, FALSE);
        UpdateWindow(window);
        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
    } else if (IsWindow(gameWindow)) {
        const RECT game = GameScreenRect(gameWindow);
        if (game.left != fittedGameRect.left || game.top != fittedGameRect.top ||
            game.right != fittedGameRect.right || game.bottom != fittedGameRect.bottom) {
            FitToGame();
            InvalidateRect(window, nullptr, FALSE);
        }
    }
    // Keep keyboard focus here, so the game does not receive keys while the overlay is open.
    const HWND foreground = GetForegroundWindow();
    if (foreground != window && foreground != blocker) SetForegroundWindow(window);
}

void OverlayState::EndDrag() {
    if (!dragging) return;
    dragging = false;
    ReleaseCapture();
}

void OverlayState::HandleMove(int x, int y) {
    if (!trackingMouse) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = window;
        TrackMouseEvent(&track);
        trackingMouse = true;
    }
    if (dragging) {
        const int trackHeight = layout.scrollTrack.bottom - layout.scrollTrack.top;
        const int thumbHeight = layout.scrollThumb.bottom - layout.scrollThumb.top;
        const int span = trackHeight - thumbHeight;
        const int maxScroll = std::max(0, scrollTotal - scrollVisible);
        if (span > 0) {
            const long long offset = static_cast<long long>(y - dragStartY) * maxScroll / span;
            const long long target = std::clamp<long long>(dragStartScroll + offset, 0, maxScroll);
            CurrentView().detailScroll = static_cast<int>(target);
            InvalidateRect(window, nullptr, FALSE);
        }
        return;
    }
    if (!Inside(layout.detail, x, y)) {
        ClearHover();
        return;
    }
    for (const DetailHit& hit : detailHits) {
        if (!Inside(hit.rect, x, y)) continue;
        // Same target: keep the tooltip where it is and do not repaint on every pixel of movement.
        if (hover.entry == hit.entry && hover.block == hit.block && hover.rune == hit.rune) return;
        hover.entry = hit.entry;
        hover.block = hit.block;
        hover.rune = hit.rune;
        hover.at = POINT{x, y};
        hover.target = hit.rect;
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    ClearHover();
}

void OverlayState::ClearHover() {
    if (hover.block == nullptr) return;
    hover = Hover{};
    InvalidateRect(window, nullptr, FALSE);
}

// Tooltip for an item block (all of its fields and lines), or for a rune icon.
void OverlayState::DrawTooltip(HDC dc) {
    if (hover.block == nullptr) return;
    const Block& block = *hover.block;

    struct TipRow { std::wstring text; COLORREF color; HFONT font; };
    std::vector<TipRow> rows;
    if (hover.rune >= 0 && static_cast<std::size_t>(hover.rune) < block.runes.size()) {
        // Gem and jewel sockets show only "Any Gem" or "Any Jewel". Other runes show their name.
        const std::wstring socket = Wide(block.runes[hover.rune].label);
        if (socket.rfind(L"Any ", 0) == 0) {
            rows.push_back({UiText(socket), kGold, fontBold});
        } else {
            rows.push_back({UiFormat(L"{0} Rune", {socket}), block.runes[hover.rune].lod ? CodeColor('8') : kGold, fontBold});
            // The rune's own stat lines for this kind of item (Runes page), instead of a "Socketed in" line.
            const RuneIcon& rune = block.runes[hover.rune];
            for (const Line& line : RuneBonusLines(*database, rune.label, rune.lod, hover.entry->title)) {
                std::string plain;
                COLORREF color = kBody;
                PlainText(line.text, plain, color, kBody);
                rows.push_back({Wide(plain), color, fontBody});
            }
        }
    } else {
        const std::string heading = block.title.empty() ? hover.entry->title : block.title;
        // Mythicals (the wide cards) are purple, set items green; the rest follow the page's name colour.
        COLORREF headingColor = tooltipNameColor;
        if (hover.entry->wide) headingColor = CodeColor(';');
        else if (hover.entry->title == "LoD Runes") headingColor = CodeColor('8');
        else if (CurrentPage().id == "set-items") headingColor = CodeColor('2');
        rows.push_back({Wide(heading), headingColor, fontBold});
        if (!block.subtitle.empty()) rows.push_back({Wide(block.subtitle), kDim, fontSmall});
        for (const Field& field : block.fields) {
            rows.push_back({UiDataText(Wide(field.label)) + L": " + Wide(field.value), kBody, fontBody});
        }
        for (const Line& line : block.lines) {
            std::string plain;
            COLORREF color = kBody;
            PlainText(line.text, plain, color, kBody);
            const bool isHeading = line.kind == LineKind::Heading;
            rows.push_back({Wide(plain), isHeading ? kGold : color, isHeading ? fontBold : fontBody});
        }
    }

    // A LoD rune's name must not take the Kanji rune colour ("Ko" exists in both sets), so name
    // colouring is off while this tip is measured and drawn, and put back afterwards.
    struct KanjiColorGuard {
        bool previous;
        explicit KanjiColorGuard(bool value) : previous(gKanjiRuneColors) { gKanjiRuneColors = value; }
        ~KanjiColorGuard() { gKanjiRuneColors = previous; }
    } kanjiGuard(!(hover.rune >= 0 && static_cast<std::size_t>(hover.rune) < block.runes.size() &&
                   block.runes[hover.rune].lod));

    struct LodNamesGuard {
        std::vector<std::wstring> previous;
        explicit LodNamesGuard(std::vector<std::wstring> names) : previous(std::move(gLodRuneNames)) {
            gLodRuneNames = std::move(names);
        }
        ~LodNamesGuard() { gLodRuneNames = std::move(previous); }
    };
    std::vector<std::wstring> lodNames;
    for (const RuneIcon& rune : block.runes) {
        if (rune.lod && rune.label.rfind("Any ", 0) != 0) lodNames.push_back(Wide(rune.label));
    }
    LodNamesGuard lodGuard(std::move(lodNames));

    const int gap = S(4);
    RECT client{};
    GetClientRect(window, &client);
    // Start at the normal width and widen only when the text would run past the bottom of the
    // window (long weapon gemwords), so most tips keep their usual shape.
    const int availableHeight = static_cast<int>(client.bottom) - S(16);
    const int maxTipWidth = std::max(S(340), static_cast<int>(client.right) - S(16));
    int tipWidth = S(340);
    std::vector<int> heights;
    int totalHeight = S(16);
    // Mythical cards (wide entries) start at the wider size.
    // The tip is as wide as its longest line needs (within the window), so it has no empty margin. Wider sizes
    // follow only if the text does not fit the bottom of the window at that width.
    int widest = 0;
    for (const TipRow& row : rows) widest = std::max(widest, NaturalWidth(dc, row.text, row.font));
    const int fitWidth = std::min(std::max(widest + S(24), S(160)), maxTipWidth);
    std::vector<int> widths{fitWidth};
    for (const int candidate : {S(340), S(420), S(520), S(640)}) {
        if (candidate > fitWidth) widths.push_back(candidate);
    }
    for (const int candidate : widths) {
        tipWidth = std::min(candidate, maxTipWidth);
        const int inner = tipWidth - S(24);
        heights.clear();
        totalHeight = S(16);
        for (const TipRow& row : rows) {
            const int rowHeight = MeasureHeight(dc, row.text, inner, row.font);
            heights.push_back(rowHeight);
            totalHeight += rowHeight + gap;
        }
        if (totalHeight <= availableHeight) break;
    }

    // Rows are placed in one column unless even the widest tip is taller than the window (a long mythical): then
    // the rows flow into two to four columns, each row whole, so nothing is cut off at the bottom.
    // columnWidth is the width of the text in one column; columns are separated by one padding unit (S(12)) and
    // the tip has one padding unit at each outside edge, the same as a single column.
    int columnWidth = tipWidth - S(24);
    std::vector<int> rowColumn(rows.size(), 0);
    std::vector<int> rowY(rows.size(), 0);  // from the top of the tip's text area
    {
        int y = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) { rowY[i] = y; y += heights[i] + gap; }
    }
    if (totalHeight > availableHeight && !rows.empty()) {
        for (int columns = 2; columns <= 4; ++columns) {
            const int spare = maxTipWidth - S(24) - (columns - 1) * S(12);
            columnWidth = std::min({S(420), spare / columns, widest});
            const int inner = columnWidth;
            int column = 0, y = 0, tallest = 0;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                heights[i] = MeasureHeight(dc, rows[i].text, inner, rows[i].font);
                if (y > 0 && y + heights[i] + S(16) > availableHeight) { column++; y = 0; }
                rowColumn[i] = column;
                rowY[i] = y;
                y += heights[i] + gap;
                tallest = std::max(tallest, y);
            }
            tipWidth = S(24) + columns * columnWidth + (columns - 1) * S(12);
            totalHeight = tallest + S(16);
            if (column < columns) break;  // everything fits in this many columns
        }
    }

    // The tooltip sits beside the thing hovered, not at the pointer, and clear of the drawn cursor: the
    // cursor reaches right and down from its hot spot, so the tip starts past the target's edge plus the
    // cursor's width. A pointer anywhere on the target can then never cover the tooltip.
    const RECT& target = hover.target;
    const int cursorRight = std::max(gCursorDrawWidth.load() - gCursorHotX.load(), 0) + S(6);
    const int cursorLeft = gCursorHotX.load() + S(6);
    int left = target.right + cursorRight;
    if (left + tipWidth > client.right - S(8)) {
        const int leftSide = target.left - cursorLeft - tipWidth;
        left = leftSide >= S(8) ? leftSide : std::max(S(8), static_cast<int>(client.right) - S(8) - tipWidth);
    }
    int top = target.top;
    if (top + totalHeight > client.bottom - S(8)) top = client.bottom - totalHeight - S(8);
    top = std::max(S(8), top);

    // Long items are clipped to the window rather than pushed off screen.
    const int visibleHeight = std::min(totalHeight, static_cast<int>(client.bottom) - top - S(8));
    const RECT box = Rect(left, top, left + tipWidth, top + visibleHeight);
    // Tooltips sit on a see-through black so the wiki behind them still shows.
    CornerShadow(dc, box, S(2));
    FillPanel(dc, box, RGB(10, 7, 4), 214);
    FrameColor(dc, box, kGold);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, box.left, box.top, box.right, box.bottom);
    int y = top + S(8);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int columnLeft = left + S(12) + rowColumn[i] * (columnWidth + S(12));
        const int rowTop = y + rowY[i];
        const RECT lineRect = Rect(columnLeft, rowTop, columnLeft + columnWidth, rowTop + heights[i]);
        Text(dc, rows[i].text, lineRect, rows[i].color, rows[i].font, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    RestoreDC(dc, saved);
}

void OverlayState::QueryChanged() {
    Refilter();
    InvalidateRect(window, nullptr, FALSE);
}

void OverlayState::HandleClick(int x, int y) {
    PageView& view = CurrentView();
    if (!query.empty() && Inside(layout.clearQuery, x, y)) {
        SetWindowTextW(search, L"");
        QueryChanged();
        SetFocus(search);
        return;
    }
    if (!query.empty() && Inside(layout.matchPrev, x, y)) {
        StepMatch(-1);
        return;
    }
    if (!query.empty() && Inside(layout.matchNext, x, y)) {
        StepMatch(1);
        return;
    }
    if (layout.scrollable && Inside(layout.scrollTrack, x, y)) {
        if (Inside(layout.scrollThumb, x, y)) {
            dragging = true;
            dragStartY = y;
            dragStartScroll = view.detailScroll;
            SetCapture(window);
        } else {
            view.detailScroll += (y < layout.scrollThumb.top) ? -scrollVisible : scrollVisible;
            view.detailScroll = std::clamp(view.detailScroll, 0, std::max(0, scrollTotal - scrollVisible));
        }
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (CalcClick(x, y)) return;
    for (const Chip& chip : layout.chips) {
        if (!Inside(chip.rect, x, y)) continue;
        switch (chip.kind) {
            case ChipKind::Socket: view.socketFilter = chip.value; break;
            case ChipKind::Sort: {
                const BlockSort picked = static_cast<BlockSort>(chip.value);
                view.sort = (IsRowSort(picked) && view.sort == picked) ? BlockSort::Default : picked;  // again: back to names
                break;
            }
            case ChipKind::Tier: view.tierFilter = chip.text; break;
            case ChipKind::Family: view.familyFilter = chip.text; break;
        }
        view.detailScroll = 0;
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    for (std::size_t i = 0; i < switches.size(); ++i) {
        if (Inside(layout.switchButtons[i], x, y)) {
            if (switches[i].current) return;
            if (switches[i].available && !switches[i].available()) return;
            BeginSwitch(switches[i].onSwitch);
            return;
        }
    }
    if (Inside(layout.close, x, y)) {
        Hide();
        return;
    }
    for (std::size_t i = 0; i < layout.tabs.size(); ++i) {
        if (Inside(layout.tabs[i], x, y)) {
            SetPage(i);
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
    }
    const std::vector<RECT> pageRows = PageRects(view.listPage);
    for (std::size_t i = 0; i < pageRows.size(); ++i) {
        if (!Inside(pageRows[i], x, y)) continue;
        SelectFiltered(pageStarts[view.listPage] + i);
        break;
    }
    if (Inside(layout.prev, x, y)) MovePage(-1);
    if (Inside(layout.next, x, y)) MovePage(1);
    InvalidateRect(window, nullptr, FALSE);
}

void OverlayState::HandleWheel(int x, int y, int delta) {
    PageView& view = CurrentView();
    const int steps = delta / WHEEL_DELTA;
    if (Inside(layout.detail, x, y)) {
        if (CalcWheel(x, y, steps)) return;
        view.detailScroll = std::max(0, view.detailScroll - steps * S(40));
    } else if (x < layout.detail.left && y >= layout.search.top && !filtered.empty()) {
        // Wheel down selects the next entry, wheel up the previous one.
        const long long target = static_cast<long long>(view.selected) - steps;
        const std::size_t last = filtered.size() - 1;
        SelectFiltered(static_cast<std::size_t>(std::clamp(target, 0LL, static_cast<long long>(last))));
    }
    InvalidateRect(window, nullptr, FALSE);
}

void OverlayState::HandleKey(WPARAM key) {
    PageView& view = CurrentView();
    if (filtered.empty()) return;
    switch (key) {
    case VK_DOWN:
        SelectFiltered(std::min(view.selected + 1, filtered.size() - 1));
        break;
    case VK_UP:
        SelectFiltered(view.selected == 0 ? 0 : view.selected - 1);
        break;
    case VK_NEXT:
        MovePage(1);
        break;
    case VK_PRIOR:
        MovePage(-1);
        break;
    default:
        return;
    }
    InvalidateRect(window, nullptr, FALSE);
}

// The list shows only each entry's title, wrapped to the row.
void OverlayState::PaintList(HDC dc) {
    const Page& page = CurrentPage();
    PageView& view = CurrentView();
    EnsurePages();
    const std::size_t pages = pageStarts.size();
    view.listPage = std::min(view.listPage, pages - 1);
    const std::vector<RECT> pageRows = PageRects(view.listPage);

    std::wstring countText = UiFormat(L"{0} of {1} entries", {std::to_wstring(filtered.size()), std::to_wstring(page.entries.size())});
    if (!query.empty()) {
        const std::wstring position = filtered.empty() ? L"0" : std::to_wstring(view.selected + 1);
        countText = UiFormat(L"{0} of {1} here | {2} total", {position, std::to_wstring(filtered.size()), std::to_wstring(totalMatches)});
    }
    Text(dc, countText, layout.count, kDim, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (!query.empty()) {
        CornerShadow(dc, layout.matchPrev, S(2));
        FillPanel(dc, layout.matchPrev, kPanel, 200);
        FrameColor(dc, layout.matchPrev, kBorder);
        Text(dc, L"<", layout.matchPrev, totalMatches == 0 ? kDim : kText, fontBody, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        CornerShadow(dc, layout.matchNext, S(2));
        FillPanel(dc, layout.matchNext, kPanel, 200);
        FrameColor(dc, layout.matchNext, kBorder);
        Text(dc, L">", layout.matchNext, totalMatches == 0 ? kDim : kText, fontBody, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    if (filtered.empty()) {
        Text(dc, UiText(L"No matches"), Rect(listLeftX, listTopY, listRightX, listTopY + std::max(S(24), bodyLine + S(6))), kDim, fontBody, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    for (std::size_t i = 0; i < pageRows.size(); ++i) {
        const std::size_t position = pageStarts[view.listPage] + i;
        if (position >= filtered.size()) break;
        const Entry& entry = page.entries[filtered[position]];
        const RECT row = pageRows[i];
        const bool selected = position == view.selected;
        CornerShadow(dc, row, S(2));
        FillPanel(dc, row, selected ? kSelected : kPanel, 185);
        if (selected) FrameColor(dc, row, kGold);
        const RECT titleRect = Rect(row.left + S(12), row.top + S(4), row.right - S(12), row.bottom - S(4));
        // Wrapped lines, left-aligned, with the block centred vertically in the row.
        const std::wstring rowTitle = UiDataText(Wide(entry.title));
        const int titleHeight = MeasureHeight(dc, rowTitle, titleRect.right - titleRect.left, fontBold);
        RECT centred = titleRect;
        centred.top += std::max<int>(0, (static_cast<int>(titleRect.bottom - titleRect.top) - titleHeight) / 2);
        Text(dc, rowTitle, centred, kHeaderGold, fontBold, DT_LEFT | DT_WORDBREAK);
    }

    const std::wstring pageText = std::to_wstring(view.listPage + 1) + L"/" + std::to_wstring(pages);
    Text(dc, pageText, layout.pageLabel, kText, fontBody, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    CornerShadow(dc, layout.prev, S(2));
    FillPanel(dc, layout.prev, kPanel, 200);
    FrameColor(dc, layout.prev, kBorder);
    Text(dc, UiText(L"< Prev"), layout.prev, view.listPage > 0 ? kText : kDim, fontBody,
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    CornerShadow(dc, layout.next, S(2));
    FillPanel(dc, layout.next, kPanel, 200);
    FrameColor(dc, layout.next, kBorder);
    Text(dc, UiText(L"Next >"), layout.next, view.listPage + 1 < pages ? kText : kDim, fontBody,
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

// True when some block in the entry has `value` in `member` (for example a tier name).
static bool AnyBlock(const Entry& entry, std::string Block::*member, const std::string& value) {
    for (const Block& block : entry.blocks) {
        if (block.*member == value) return true;
    }
    return false;
}

// The filter value if this entry has blocks with it, otherwise "" (show all). Keeps a
// filter chosen on one category from hiding everything in another.
static std::string ActiveFilter(const Entry& entry, std::string Block::*member, const std::string& value) {
    return (!value.empty() && AnyBlock(entry, member, value)) ? value : std::string();
}

// Weapon families present in an entry, in alphabetical order.
static std::vector<std::string> DistinctFamilies(const Entry& entry) {
    std::set<std::string> families;
    std::vector<std::string> ordered;
    for (const Block& block : entry.blocks) {
        if (!block.family.empty() && families.insert(block.family).second) ordered.push_back(block.family);
    }
    if (entry.keepOrder) return ordered;
    return std::vector<std::string>(families.begin(), families.end());
}

// Filter and sort bars. Runewords get a socket filter row and socket sorts. Sortable
// item lists (uniques) get Tier and Type filter rows, then level and name sorts. Chips
// wrap onto another line when a row is full.
int OverlayState::BuildControls(HDC dc, const Entry& entry, PageView& view) {
    layout.chips.clear();
    // Class Crafting has no filters or sorts: its cards stay in the order the page gives them.
    if (CurrentPage().id == "class-crafting") return layout.detail.top + S(12);
    const bool runes = HasRuneBlocks(entry) || entry.socketSort;  // gemwords have a Sockets filter but no runes
    // Tier names (unique tiers, or Hireling difficulties) need the controls even without runes or sorting.
    const bool hasTiers = std::any_of(entry.blocks.begin(), entry.blocks.end(),
                                      [](const Block& block) { return !block.tier.empty(); });
    const bool hasFamilies = std::any_of(entry.blocks.begin(), entry.blocks.end(),
                                         [](const Block& block) { return !block.family.empty(); });
    if (!runes && !entry.sortable && !hasTiers && !hasFamilies) return layout.detail.top + S(16);

    const BlockSort active = ResolveBlockSort(entry, view.sort);
    const std::string tierNow = ActiveFilter(entry, &Block::tier, view.tierFilter);
    const std::string familyNow = ActiveFilter(entry, &Block::family, view.familyFilter);
    const std::vector<std::string> families = DistinctFamilies(entry);

    const int left = layout.detail.left + S(16);
    const int top = layout.detail.top + S(16);
    const int rowHeight = std::max(S(26), smallLine + S(10));
    const int right = layout.detail.right - S(22);
    int labelWidth = 0;
    for (const wchar_t* text : {L"Sockets:", L"Tier:", L"Type:", L"Sort:"}) {
        labelWidth = std::max(labelWidth, MeasureWidth(dc, UiText(text), fontSmall));
    }
    labelWidth += S(12);
    const int chipStart = left + labelWidth;

    auto label = [&](int rowY, const wchar_t* text) {
        Text(dc, UiText(text), Rect(left, rowY, left + labelWidth, rowY + rowHeight), kDim, fontSmall,
             DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    };
    auto chip = [&](int& rowY, int& cursor, const std::wstring& text, int minWidth, ChipKind kind,
                    std::size_t value, const std::string& name, bool selected) {
        const int chipWidth = std::max(minWidth, MeasureWidth(dc, text, fontSmall) + S(16));
        if (cursor > chipStart && cursor + chipWidth > right) {
            cursor = chipStart;
            rowY += rowHeight + S(6);
        }
        const RECT rect = Rect(cursor, rowY, cursor + chipWidth, rowY + rowHeight);
        layout.chips.push_back(Chip{rect, kind, value, name});
        CornerShadow(dc, rect, S(2));
        FillPanel(dc, rect, selected ? kSelected : kPanel, 200);
        FrameColor(dc, rect, selected ? kGold : kBorder);
        Text(dc, text, rect, selected ? kGold : kText, fontSmall, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        cursor += chipWidth + S(6);
    };

    int y = top;
    if (runes) {
        label(y, L"Sockets:");
        int rowY = y, cursor = chipStart;
        chip(rowY, cursor, UiText(L"All"), S(44), ChipKind::Socket, 0, "", view.socketFilter == 0);
        for (std::size_t count : SocketCounts(entry)) {
            chip(rowY, cursor, std::to_wstring(count), S(36), ChipKind::Socket, count, "",
                 view.socketFilter == count);
        }
        y = rowY + rowHeight + S(6);
    }
    // Tier filter: unique tiers, or Hireling difficulties. Only the names present are listed.
    const char* const tierNames[] = {"Normal", "Exceptional", "Elite", "Nightmare", "Hell", "Mythic"};
    {
        if (hasTiers) {
            // The known tiers in their order, then any other name the blocks carry (Item Affixes: Prefix, Suffix, Superior).
            std::vector<std::string> tiers;
            for (const char* name : tierNames)
                if (AnyBlock(entry, &Block::tier, name)) tiers.push_back(name);
            const std::size_t known = tiers.size();
            for (const Block& block : entry.blocks)
                if (!block.tier.empty() && std::find(tiers.begin(), tiers.end(), block.tier) == tiers.end()) tiers.push_back(block.tier);
            label(y, tiers.size() > known ? L"Affix:" : L"Tier:");
            int rowY = y, cursor = chipStart;
            chip(rowY, cursor, UiText(L"All"), S(44), ChipKind::Tier, 0, "", tierNow.empty());
            for (const std::string& name : tiers) chip(rowY, cursor, UiText(Wide(name)), S(80), ChipKind::Tier, 0, name, tierNow == name);
            y = rowY + rowHeight + S(6);
        }
    }
    {
        if (!families.empty()) {
            label(y, L"Type:");
            int rowY = y, cursor = chipStart;
            chip(rowY, cursor, UiText(L"All"), S(44), ChipKind::Family, 0, "", familyNow.empty());
            // Helms on the Gemwords page: the country types go in a row of their own at the top, the other
            // types on a second row below.
            static const std::set<std::string> kCountryTypes = {
                "America", "Austria", "Britain", "Canada", "China", "France", "Germany", "Italy", "Japan", "Portugal", "Russia"};
            std::vector<std::string> ordered = families;
            const bool countryRows = CurrentPage().id == "gemwords" && entry.title == "Helms";
            std::size_t countries = 0;
            if (countryRows) {
                std::stable_partition(ordered.begin(), ordered.end(),
                                      [](const std::string& family) { return kCountryTypes.count(family) > 0; });
                countries = static_cast<std::size_t>(std::count_if(ordered.begin(), ordered.end(),
                                      [](const std::string& family) { return kCountryTypes.count(family) > 0; }));
            }
            for (std::size_t i = 0; i < ordered.size(); ++i) {
                if (countryRows && countries > 0 && i == countries) {
                    rowY += rowHeight + S(6);  // the other types start a second row
                    cursor = chipStart;
                }
                chip(rowY, cursor, UiText(Wide(ordered[i])), S(70), ChipKind::Family, 0, ordered[i], familyNow == ordered[i]);
            }
            y = rowY + rowHeight + S(6);
        }
    }

    // Sort chips only do something for level-sorted lists or titled blocks.
    const bool hasTitles = std::any_of(entry.blocks.begin(), entry.blocks.end(),
                                       [](const Block& block) { return !block.title.empty(); });
    if (entry.socketSort || (!entry.sortable && !hasTitles)) return y;  // gemwords: the Sockets filter only
    label(y, L"Sort:");
    int rowY = y, cursor = chipStart;
    if (entry.rowSort) {  // table rows: each chip cycles ascending, descending, then back to names
        const bool ilvlOn = active == BlockSort::RowMinIlvl || active == BlockSort::RowMaxIlvl;
        const BlockSort ilvlNext = active == BlockSort::RowMinIlvl ? BlockSort::RowMaxIlvl
                                 : active == BlockSort::RowMaxIlvl ? BlockSort::Default : BlockSort::RowMinIlvl;
        const std::wstring ilvlText = active == BlockSort::RowMinIlvl ? UiText(L"iLvl \u2191")
                                    : active == BlockSort::RowMaxIlvl ? UiText(L"iLvl \u2193") : UiText(L"iLvl");
        chip(rowY, cursor, ilvlText, S(80), ChipKind::Sort, static_cast<std::size_t>(ilvlNext), "", ilvlOn);
        const bool rarityOn = active == BlockSort::RowRarity || active == BlockSort::RowRarityHigh;
        const BlockSort rarityNext = active == BlockSort::RowRarity ? BlockSort::RowRarityHigh
                                   : active == BlockSort::RowRarityHigh ? BlockSort::Default : BlockSort::RowRarity;
        const std::wstring rarityText = active == BlockSort::RowRarity ? UiText(L"Rarity \u2191")
                                      : active == BlockSort::RowRarityHigh ? UiText(L"Rarity \u2193") : UiText(L"Rarity");
        chip(rowY, cursor, rarityText, S(90), ChipKind::Sort, static_cast<std::size_t>(rarityNext), "", rarityOn);
        return rowY + rowHeight + S(10);
    }
    if (entry.sortable) {
        chip(rowY, cursor, UiText(L"Lowest level"), S(110), ChipKind::Sort,
             static_cast<std::size_t>(BlockSort::LevelLowFirst), "", active == BlockSort::LevelLowFirst);
        chip(rowY, cursor, UiText(L"Highest level"), S(110), ChipKind::Sort,
             static_cast<std::size_t>(BlockSort::LevelHighFirst), "", active == BlockSort::LevelHighFirst);
    }
    chip(rowY, cursor, UiText(L"A-Z"), S(56), ChipKind::Sort, static_cast<std::size_t>(BlockSort::Name), "",
         active == BlockSort::Name);

    return rowY + rowHeight + S(10);
}

// Scrollbar along the right edge of the detail pane. Records the track and thumb
// so mouse input can drag or page through the content.
void OverlayState::DrawScrollbar(HDC dc, int top, int visible, int total, int scroll) {
    layout.scrollable = total > visible && visible > 0;
    layout.scrollTrack = Rect(layout.detail.right - S(16), top, layout.detail.right - S(8), layout.detail.bottom - S(8));
    if (!layout.scrollable) return;
    const RECT track = layout.scrollTrack;
    const int trackHeight = track.bottom - track.top;
    const int thumbHeight = std::max(S(28), trackHeight * visible / total);
    const int maxScroll = total - visible;
    const int thumbTop = track.top + static_cast<int>(static_cast<long long>(trackHeight - thumbHeight) * scroll / maxScroll);
    layout.scrollThumb = Rect(track.left, thumbTop, track.right, thumbTop + thumbHeight);
    CornerShadow(dc, track, S(2));
    FillPanel(dc, track, kSearchFill, 200);
    FillColor(dc, layout.scrollThumb, dragging ? kGold : kBorder);
}

// Height of one item tile: name, art, then its rune icons.
// Height of the tile's tag under the art (for example "(Coupon only)"), with its gap; 0 when there is none.
int OverlayState::TagRowHeight(HDC dc, const Block& block, const TileStyle& style) {
    if (block.tag.empty()) return 0;
    return MeasureHeight(dc, Wide(block.tag), style.tileWidth - S(12), fontSmall) + S(4);
}

// Height of the picture area of a tile: rings, amulets, jewels and small and medium charms use 50% of it.
static int ArtBoxFor(const Block& block, const TileStyle& style) {
    return block.tinyArt ? std::max(1, style.artBox * 50 / 100) : style.artBox;
}

int OverlayState::TileHeight(HDC dc, const Block& block, const TileStyle& style) {
    const int nameHeight = MeasureHeight(dc, Wide(block.title), style.tileWidth - S(12), style.nameFont);
    const int runeRows = RuneRows(block.runes.size(), style);
    return style.padding + nameHeight + style.padding + ArtBoxFor(block, style) + TagRowHeight(dc, block, style) +
           (runeRows > 0 ? S(8) + runeRows * RuneRowHeight(style) : 0) + S(10);
}

// Lays out item blocks as tiles: name over art, with the rune sequence under it.
// Tile hit boxes are added after the tile's rune icons, so a rune icon shows the
// rune tooltip and the rest of the tile shows the item tooltip. Hit boxes that are
// scrolled out of the visible band are skipped.
int OverlayState::RenderTiles(HDC dc, Gdiplus::Graphics* graphics, const Entry& entry,
                              const std::vector<std::size_t>& order, std::size_t begin, std::size_t end, int left,
                              int width_, int y, bool compact, bool draw) {
    gKanjiRuneColors = entry.title != "LoD Runes";
    if (entry.wide) return RenderWideTiles(dc, graphics, entry, order, begin, end, left, width_, y, draw);
    const TileStyle style = MakeTileStyle(width_, compact, entry.fiveAcross);
    const std::size_t step = static_cast<std::size_t>(style.columns);

    for (std::size_t first = begin; first < end; first += step) {
        if (draw && y > clipBottom) break;  // rows below the band are not drawn
        const std::size_t count = std::min<std::size_t>(step, end - first);
        int rowHeight = 0;
        for (std::size_t c = 0; c < count; ++c) {
            rowHeight = std::max(rowHeight, TileHeight(dc, entry.blocks[order[first + c]], style));
        }
        for (std::size_t c = 0; c < count; ++c) {
            const std::size_t index = order[first + c];
            const Block& block = entry.blocks[index];
            const int tileLeft = left + static_cast<int>(c) * (style.tileWidth + style.gap);
            const RECT tile = Rect(tileLeft, y, tileLeft + style.tileWidth, y + rowHeight);
            if (!draw && !query.empty() && BlockMatches(block, query)) matchTops.push_back(y - matchBase);
            const bool onScreen = tile.bottom > clipTop && tile.top < clipBottom;
            if (draw && onScreen) {
                CornerShadow(dc, tile, S(2));
                FillPanel(dc, tile, kTile, 150);
                FrameColor(dc, tile, kBorder);
            }

            const int nameHeight = MeasureHeight(dc, Wide(block.title), style.tileWidth - S(12), style.nameFont);
            // Rings and amulets are shorter than the tallest tile in their row, so their contents sit in the middle.
            const int cy = y + (block.tinyArt ? (rowHeight - TileHeight(dc, block, style)) / 2 : 0);
            if (draw && onScreen && nameHeight > 0) {
                Text(dc, Wide(block.title),
                     Rect(tileLeft + S(6), cy + style.padding, tileLeft + style.tileWidth - S(6),
                          cy + style.padding + nameHeight),
                     entry.title == "LoD Runes" ? CodeColor('8') : itemNameColor, style.nameFont,
                     DT_CENTER | DT_WORDBREAK);
            }

            const int artTop = cy + style.padding + nameHeight + style.padding;
            const int box = ArtBoxFor(block, style);
            const int artSize = block.smallArt ? box / 2 : box;
            const int artLeft = tileLeft + (style.tileWidth - artSize) / 2;
            const int artDrawTop = artTop + (box - artSize) / 2;
            const RECT art = Rect(artLeft, artDrawTop, artLeft + artSize, artDrawTop + artSize);
            if (draw && onScreen) {
                for (const std::string& key : block.images) DrawImageKey(*graphics, key, art);
            }
            const int tagRow = TagRowHeight(dc, block, style);
            if (draw && onScreen && !block.tag.empty()) {
                const RECT tagBox = Rect(tileLeft + S(6), artTop + box + S(4),
                                         tileLeft + style.tileWidth - S(6), artTop + box + tagRow);
                Text(dc, Wide(block.tag), tagBox, kGold, fontSmall, DT_CENTER | DT_WORDBREAK);
            }

            if (!block.runes.empty()) {
                const int inner = style.tileWidth - S(12);
                const int perRow = std::max(1, inner / style.cell);
                const int icon = style.cell - S(4);
                const int runeTop = artTop + box + S(8) + tagRow;
                const int total = static_cast<int>(block.runes.size());
                for (int r = 0; r < total; ++r) {
                    const int row = r / perRow;
                    const int col = r % perRow;
                    const int inRow = std::min(perRow, total - row * perRow);
                    const int x = tileLeft + (style.tileWidth - inRow * style.cell) / 2 + col * style.cell;
                    const int rowTop = runeTop + row * RuneRowHeight(style);
                    if (draw && onScreen) {
                        const RECT iconBox = Rect(x + S(2), rowTop, x + S(2) + icon, rowTop + icon);
                        detailHits.push_back(DetailHit{iconBox, &entry, &block, r});
                        DrawImageKey(*graphics, block.runes[r].imageKey, iconBox);
                        const std::wstring label = Wide(block.runes[r].label);
                        // Jewel and gem sockets show their name in the tooltip only.
                        if (label.rfind(L"Any ", 0) != 0) {
                            Text(dc, label, Rect(x, rowTop + icon, x + style.cell, rowTop + icon + smallLine),
                                 block.runes[r].lod ? CodeColor('8') : KanjiLabelColor(label, kGold), fontSmall,
                                 DT_CENTER | DT_SINGLELINE);
                        }
                    }
                }
            }

            if (draw && tile.bottom > clipTop && tile.top < clipBottom) {
                detailHits.push_back(DetailHit{art, &entry, &block, -1});  // the picture itself, not the whole tile
            }
        }
        y += rowHeight + style.gap;
    }
    return y;
}

// Wide entries (mythical uniques): one card per row, with the card's stats and notes
// in a column beside it. The name is drawn in purple to mark the rarity.
int OverlayState::RenderWideTiles(HDC dc, Gdiplus::Graphics* graphics, const Entry& entry,
                                  const std::vector<std::size_t>& order, std::size_t begin, std::size_t end,
                                  int left, int width_, int y, bool draw) {
    const int gap = S(12);
    const int pad = S(6);
    const int sideWidth = std::max(S(220), width_ * 2 / 5);
    const int tileWidth = std::max(S(160), width_ - sideWidth - gap);
    const int sideLeft = left + tileWidth + gap;
    const int inner = tileWidth - S(12);
    const int artBox = std::min(inner, 2 * std::min(S(128), std::max(S(64), inner / 2)));  // twice the usual art size

    for (std::size_t i = begin; i < end; ++i) {
        if (draw && y > clipBottom) break;  // rows below the band are not drawn
        const Block& block = entry.blocks[order[i]];
        const int nameHeight = MeasureHeight(dc, Wide(block.title), inner, fontBold);

        // The side column holds only the notes, laid out like a guide block. Stats are in the tooltip.
        Block side;
        if (!block.note.empty()) side.lines.push_back(Line{LineKind::Text, block.note});
        const int sideTop = y + pad;
        const int sideHeight = RenderGuideBlock(dc, graphics, side, sideLeft + pad, sideWidth - 2 * pad, sideTop, false)
                               - sideTop + pad;
        const int cardHeight = pad + nameHeight + pad + artBox + pad;
        const int rowHeight = std::max(cardHeight, sideHeight);

        const RECT tile = Rect(left, y, left + tileWidth, y + rowHeight);
        const RECT column = Rect(sideLeft, y, sideLeft + sideWidth, y + rowHeight);
        const bool onScreen = tile.bottom > clipTop && tile.top < clipBottom;
        if (draw && onScreen) {
            CornerShadow(dc, tile, S(2));
            FillPanel(dc, tile, kTile, 150);
            FrameColor(dc, tile, kBorder);
            CornerShadow(dc, column, S(2));
            FillPanel(dc, column, kTile, 150);
            FrameColor(dc, column, kBorder);
            Text(dc, Wide(block.title), Rect(left + pad, y + pad, left + tileWidth - pad, y + pad + nameHeight),
                 CodeColor(';'), fontBold, DT_CENTER | DT_WORDBREAK);
            const int artSize = block.smallArt ? artBox / 2 : artBox;
            const int artLeft = left + (tileWidth - artSize) / 2;
            const int artTop = y + pad + nameHeight + pad + (artBox - artSize) / 2;
            const RECT art = Rect(artLeft, artTop, artLeft + artSize, artTop + artSize);
            for (const std::string& key : block.images) DrawImageKey(*graphics, key, art);
            RenderGuideBlock(dc, graphics, side, sideLeft + pad, sideWidth - 2 * pad, sideTop, true);
        }
        if (draw && onScreen) {
            const int artSize = block.smallArt ? artBox / 2 : artBox;
            const int artLeft = left + (tileWidth - artSize) / 2;
            const int artTop = y + pad + nameHeight + pad + (artBox - artSize) / 2;
            detailHits.push_back(DetailHit{Rect(artLeft, artTop, artLeft + artSize, artTop + artSize), &entry, &block, -1});
        }
        y += rowHeight + gap;
    }
    return y;
}

// Non-item guide block: its title, subtitle, images, fields and text lines.
// Splits a table row into its cells (U+001F separates them; U+001E inside a cell is a line break).
std::vector<std::string> SplitCells(const std::string& text) {
    std::vector<std::string> cells(1);
    for (const char c : text) {
        if (c == '\x1f') cells.emplace_back();
        else if (c == '\x1e') cells.back().push_back('\n');
        else cells.back().push_back(c);
    }
    return cells;
}

// Draws consecutive table rows as a bordered grid. Each column is as wide as its longest
// cell, so long recipe text gets the room; the header row uses the bold font and gold.
int OverlayState::RenderTable(HDC dc, const std::vector<Line>& lines, std::size_t first, std::size_t last, int left,
                              int width_, int y, bool draw) {
    std::vector<std::vector<std::string>> rows;
    std::size_t columns = 0;
    for (std::size_t i = first; i < last; ++i) {
        rows.push_back(SplitCells(lines[i].text));
        columns = std::max(columns, rows.back().size());
    }
    if (columns == 0) return y;

    std::vector<std::size_t> weight(columns, 1);
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < row.size(); ++c) {
            // Weighted by the longest visible line of the cell (colour codes take no room), since line breaks
            // stack text vertically.
            std::string visible;
            COLORREF ignored = kBody;
            PlainText(row[c], visible, ignored, kBody);
            std::size_t longest = 0, current = 0;
            for (const char ch : visible) {
                if (ch == '\n') current = 0;
                else longest = std::max(longest, ++current);
            }
            weight[c] = std::max(weight[c], longest);
        }
    }
    std::size_t total = 0;
    for (const std::size_t w : weight) total += w;
    std::vector<int> colWidth(columns, 0);
    int used = 0;
    for (std::size_t c = 0; c + 1 < columns; ++c) {
        colWidth[c] = static_cast<int>(static_cast<std::size_t>(width_) * weight[c] / total);
        used += colWidth[c];
    }
    colWidth[columns - 1] = width_ - used;

    const int pad = S(4);
    // Text and colour of one cell; the header row uses gold.
    auto cellText = [&](std::size_t r, std::size_t c, COLORREF& color) {
        const bool header = lines[first + r].kind == LineKind::Header;
        color = header ? kGold : kBody;
        std::string plain;
        if (c < rows[r].size()) PlainText(rows[r][c], plain, color, header ? kGold : kBody);
        if (header) color = kGold;
        return header ? UiText(Wide(plain)) : Wide(plain);
    };

    std::string key = std::to_string(width_);
    for (std::size_t i = first; i < last; ++i) {
        key += '|';
        key += static_cast<char>(lines[i].kind);
        key += lines[i].text;
    }
    // Widen any column whose longest unbreakable word would otherwise be clipped. The extra
    // room comes out of the wider columns in proportion to their width.
    {
        std::vector<int> need(columns, 0);
        const auto knownNeed = tableNeedCache.find(key);
        const bool measureNeed = knownNeed == tableNeedCache.end();
        if (!measureNeed) need = knownNeed->second;
        const HFONT previous = static_cast<HFONT>(SelectObject(dc, fontBody));
        for (std::size_t r = 0; measureNeed && r < rows.size(); ++r) {
            for (std::size_t c = 0; c < columns; ++c) {
                COLORREF color = kBody;
                const std::wstring text = cellText(r, c, color);
                // Each line of the cell stays on one line when it fits in part of the table (up to two fifths
                // of its width), so a short ingredient such as a token name does not wrap.
                std::size_t lineStart = 0;
                while (lineStart <= text.size()) {
                    std::size_t lineEnd = text.find(L'\n', lineStart);
                    if (lineEnd == std::wstring::npos) lineEnd = text.size();
                    if (lineEnd > lineStart) {
                        SIZE size{};
                        GetTextExtentPoint32W(dc, text.c_str() + lineStart, static_cast<int>(lineEnd - lineStart), &size);
                        const int wanted = std::min(static_cast<int>(size.cx) + 2 * pad + S(4), width_ * 2 / 5);
                        need[c] = std::max(need[c], wanted);
                    }
                    lineStart = lineEnd + 1;
                }
                std::size_t begin = 0;
                while (begin <= text.size()) {
                    std::size_t end = text.find(L' ', begin);
                    if (end == std::wstring::npos) end = text.size();
                    if (end > begin) {
                        SIZE size{};
                        GetTextExtentPoint32W(dc, text.c_str() + begin, static_cast<int>(end - begin), &size);
                        need[c] = std::max(need[c], static_cast<int>(size.cx) + 2 * pad + S(4));
                    }
                    begin = end + 1;
                }
            }
        }
        SelectObject(dc, previous);
        if (measureNeed) tableNeedCache.emplace(key, need);
        int deficit = 0;
        for (std::size_t c = 0; c < columns; ++c) {
            if (colWidth[c] < need[c]) { deficit += need[c] - colWidth[c]; colWidth[c] = need[c]; }
        }
        int spare = 0;
        for (std::size_t c = 0; c < columns; ++c) {
            if (colWidth[c] > need[c]) spare += colWidth[c] - need[c];
        }
        if (deficit > 0 && spare > 0) {
            for (std::size_t c = 0; c < columns; ++c) {
                if (colWidth[c] > need[c]) {
                    const int give = std::min(colWidth[c] - need[c], deficit * (colWidth[c] - need[c]) / spare);
                    colWidth[c] -= give;
                }
            }
            int sum = 0;
            for (const int w : colWidth) sum += w;
            colWidth[columns - 1] += width_ - sum;
        }
    }

    // Raw cell text with its colour codes. Non-header cells with colour codes are laid out piece by
    // piece, so each word can keep its own colour (as paragraphs do), not one colour per cell.
    auto rawCell = [&](std::size_t r, std::size_t c) -> std::string {
        return c < rows[r].size() ? rows[r][c] : std::string();
    };
    auto multiColour = [&](std::size_t r, std::size_t c) {
        return lines[first + r].kind != LineKind::Header && HasColorCodes(rawCell(r, c));
    };
    auto cellHeight = [&](std::size_t r, std::size_t c, int w, HFONT f) -> int {
        if (multiColour(r, c)) return LayoutPieces(dc, LinePieces(rawCell(r, c), kBody), kBody, Rect(0, 0, w, 0), f, DT_LEFT, false);
        COLORREF color = kBody;
        return MeasureHeight(dc, cellText(r, c, color), w, f);
    };

    auto cached = tableHeightCache.find(key);
    if (cached == tableHeightCache.end()) {
        std::vector<int> measured;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const HFONT font = lines[first + r].kind == LineKind::Header ? fontBold : fontBody;
            int rowHeight = 0;
            for (std::size_t c = 0; c < columns; ++c) {
                rowHeight = std::max(rowHeight, cellHeight(r, c, std::max(S(20), colWidth[c] - 2 * pad), font));
            }
            measured.push_back(rowHeight + 2 * pad);
        }
        cached = tableHeightCache.emplace(key, std::move(measured)).first;
    }
    const std::vector<int>& heights = cached->second;

    for (std::size_t r = 0; r < rows.size(); ++r) {
        const int rowHeight = heights[r];
        // Rows outside the visible band are positioned but not drawn.
        if (draw && y + rowHeight > clipTop && y < clipBottom) {
            const HFONT font = lines[first + r].kind == LineKind::Header ? fontBold : fontBody;
            int x = left;
            for (std::size_t c = 0; c < columns; ++c) {
                COLORREF color = kBody;
                const std::wstring text = cellText(r, c, color);
                const RECT cell = Rect(x, y, x + colWidth[c], y + rowHeight);
                FrameColor(dc, cell, kBorder);
                RECT inner = Rect(x + pad, y + pad, x + colWidth[c] - pad, y + rowHeight - pad);
                // Centre the cell's text vertically, so a short cell next to a tall one sits mid-row, not at the top.
                const int contentHeight = cellHeight(r, c, std::max(S(20), colWidth[c] - 2 * pad), font);
                const int spare = (inner.bottom - inner.top) - contentHeight;
                if (spare > 0) inner.top += spare / 2;
                if (multiColour(r, c)) LayoutPieces(dc, LinePieces(rawCell(r, c), kBody), kBody, inner, font, DT_LEFT, true);
                else Text(dc, text, inner, color, font, DT_LEFT | DT_WORDBREAK);
                x += colWidth[c];
            }
        }
        y += rowHeight;
    }
    return y + S(6);
}

int OverlayState::RenderGuideBlock(HDC dc, Gdiplus::Graphics* graphics, const Block& block, int left, int width_,
                                   int y, bool draw) {
    if (splitNow != 0 && !block.images.empty()) return RenderSplitBlock(dc, graphics, block, left, width_, y, draw);
    if (!block.title.empty()) {
        const std::wstring wide = Wide(block.title);
        const int h = MeasureHeight(dc, wide, width_, fontBold);
        if (draw) Text(dc, wide, Rect(left, y, left + width_, y + h), block.item ? itemNameColor : kGold, fontBold,
                       DT_LEFT | DT_WORDBREAK);
        y += h + S(2);
    }
    if (!block.subtitle.empty()) {
        const std::wstring wide = Wide(block.subtitle);
        const int h = MeasureHeight(dc, wide, width_, fontSmall);
        if (draw) Text(dc, wide, Rect(left, y, left + width_, y + h), kDim, fontSmall, DT_LEFT | DT_WORDBREAK);
        y += h + S(4);
    }

    if (!block.images.empty() && block.originalSize) {
        // Drawn at the picture's own pixel size (scaled for the display DPI), one per row, and shrunk
        // only when it is wider than the detail panel.
        for (const std::string& key : block.images) {
            Gdiplus::Bitmap* bitmap = FindImage(key);
            int imageW = S(128);
            int imageH = S(128);
            if (bitmap != nullptr && bitmap->GetWidth() > 0 && bitmap->GetHeight() > 0) {
                imageW = S(static_cast<int>(bitmap->GetWidth()));
                imageH = S(static_cast<int>(bitmap->GetHeight()));
                if (imageW > width_) {
                    imageH = imageH * width_ / imageW;
                    imageW = width_;
                }
            }
            const int x = left + (width_ - imageW) / 2;
            if (draw && y + imageH > clipTop && y < clipBottom) DrawImageKey(*graphics, key, Rect(x, y, x + imageW, y + imageH));
            y += imageH + S(8);
        }
        y += S(4);
    } else if (!block.images.empty()) {
        const int imageBox = std::min(S(128), std::max(S(64), width_ / 3));
        const int perRow = std::max(1, width_ / (imageBox + S(10)));
        for (std::size_t i = 0; i < block.images.size(); ++i) {
            const int col = static_cast<int>(i) % perRow;
            const int row = static_cast<int>(i) / perRow;
            const int x = left + col * (imageBox + S(10));
            const int top = y + row * (imageBox + S(8));
            if (draw && top + imageBox > clipTop && top < clipBottom) {
                DrawImageKey(*graphics, block.images[i], Rect(x, top, x + imageBox, top + imageBox));
            }
        }
        const int rows = static_cast<int>((block.images.size() + perRow - 1) / perRow);
        y += rows * (imageBox + S(8)) + S(4);
    }

    for (const Field& field : block.fields) {
        const std::wstring wide = UiDataText(Wide(field.label)) + L": " + Wide(field.value);
        const int h = MeasureHeight(dc, wide, width_, fontBody);
        if (draw) Text(dc, wide, Rect(left, y, left + width_, y + h), kBody, fontBody, DT_LEFT | DT_WORDBREAK);
        y += h + S(3);
    }
    if (!block.fields.empty()) y += S(4);

    const std::vector<Line>* shownLines = &block.lines;
    if (IsRowSort(rowSortNow)) {
        if (sortedRowsFor != rowSortNow || sortedRowsDb != database) {
            sortedRows.clear();
            sortedRowsFor = rowSortNow;
            sortedRowsDb = database;
        }
        auto found = sortedRows.find(&block);
        if (found == sortedRows.end()) found = sortedRows.emplace(&block, SortTableRows(block.lines, rowSortNow)).first;
        shownLines = &found->second;
    }
    const std::vector<Line>& blockLines = *shownLines;
    for (std::size_t i = 0; i < blockLines.size();) {
        const Line& line = blockLines[i];
        if (line.kind == LineKind::Row || line.kind == LineKind::Header) {
            std::size_t end = i;
            while (end < blockLines.size() && (blockLines[end].kind == LineKind::Row ||
                                               blockLines[end].kind == LineKind::Header)) {
                ++end;
            }
            y = RenderTable(dc, blockLines, i, end, left, width_, y, draw);
            i = end;
            continue;
        }
        const bool heading = line.kind == LineKind::Heading;
        if (heading) y += S(6);
        const COLORREF base = heading ? kHeaderGold : kBody;
        const HFONT font = heading ? fontBold : fontBody;
        std::string plain;
        COLORREF unused = base;
        PlainText(line.text, plain, unused, base);
        const std::wstring wide = Wide(plain);
        const int h = MeasureHeight(dc, wide, width_, font);
        if (draw) {
            const RECT box = Rect(left, y, left + width_, y + h);
            if (HasColorCodes(line.text)) LayoutPieces(dc, LinePieces(line.text, base), base, box, font, DT_LEFT, true);
            else Text(dc, wide, box, base, font, DT_LEFT | DT_WORDBREAK);
        }
        y += h + S(5);
        ++i;
    }
    return y;
}

// Lays out one entry: its title, then its blocks in `order`. Item blocks become
// tile grids, split into runs by section header when `headers` is set. With draw ==
// false it only measures, so the caller can clamp the scroll offset first. The
// content right edge leaves room for the scrollbar.
int OverlayState::RenderDetail(HDC dc, const Entry& entry, const std::vector<std::size_t>& order, bool headers,
                               int top, int scroll, bool compact, bool draw) {
    if (!draw) matchTops.clear();
    if (CalcActive()) return RenderCalculator(dc, top, scroll, draw);
    const RECT area = layout.detail;
    const int left = area.left + S(16);
    const int width_ = area.right - S(22) - left;
    int y = top - scroll;
    const int origin = y;
    if (!draw) matchBase = origin;

    std::unique_ptr<Gdiplus::Graphics> owned;
    Gdiplus::Graphics* graphics = nullptr;
    if (draw) {
        owned = std::make_unique<Gdiplus::Graphics>(dc);
        owned->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        // GDI+ does not always honor the device context clip, so clip it here too.
        owned->SetClip(Gdiplus::Rect(area.left + 1, clipTop, area.right - S(18) - area.left - 1, clipBottom - clipTop));
        graphics = owned.get();
    }

    const int titleHeight = titleLine + S(8);
    if (draw) Text(dc, UiDataText(Wide(entry.title)), Rect(left, y, left + width_, y + titleHeight), kHeaderGold, fontTitle,
                   DT_LEFT | DT_SINGLELINE);
    y += titleHeight + S(2);
    if (!entry.subtitle.empty()) {
        if (draw) Text(dc, TranslateCounts(Wide(entry.subtitle)), Rect(left, y, left + width_, y + S(20)), kDim, fontSmall,
                       DT_LEFT | DT_SINGLELINE);
        y += S(24);
    }
    y += S(4);

    if (order.empty()) {
        // Tall enough for the font's full line, so the text is not cut off at the bottom.
        const int noMatchHeight = std::max(S(24), bodyLine + S(6));
        if (draw) Text(dc, UiText(L"Nothing matches this filter"), Rect(left, y, left + width_, y + noMatchHeight), kDim, fontBody,
                       DT_LEFT | DT_SINGLELINE);
        return y + noMatchHeight - origin;
    }

    bool firstBlock = true;
    std::size_t i = 0;
    while (i < order.size()) {
        if (draw && y > clipBottom) break;  // everything below is off screen
        const Block& first = entry.blocks[order[i]];
        if (!first.item) {
            if (!firstBlock) {
                if (draw) FillColor(dc, Rect(left, y, left + width_, y + 1), kBorder);
                y += S(12);
            }
            if (!draw && !query.empty() && BlockMatches(first, query)) matchTops.push_back(y - matchBase);
            y = RenderGuideBlock(dc, graphics, first, left, width_, y, draw);
            y += S(6);
            firstBlock = false;
            ++i;
            continue;
        }

        if (headers && !first.section.empty()) {
            const std::wstring header = UiText(Wide(first.section));
            const int h = MeasureHeight(dc, header, width_, fontBold);
            if (draw) Text(dc, header, Rect(left, y, left + width_, y + h), greenNames ? CodeColor('2') : kHeaderGold, fontBold, DT_LEFT | DT_WORDBREAK);
            y += h + S(2);
            if (draw) FillColor(dc, Rect(left, y, left + width_, y + 1), kBorder);
            y += S(8);
        }

        std::size_t end = i;
        while (end < order.size()) {
            const Block& candidate = entry.blocks[order[end]];
            if (!candidate.item) break;
            if (headers && candidate.section != first.section) break;
            ++end;
        }
        y = RenderTiles(dc, graphics, entry, order, i, end, left, width_, y, compact || entry.compact, draw);
        y += S(6);
        firstBlock = false;
        i = end;
    }
    return y - origin;
}

int OverlayState::RenderSplitBlock(HDC dc, Gdiplus::Graphics* graphics, const Block& block, int left, int width_, int y,
                                   bool draw) {
    const std::vector<Line>& lines = block.lines;
    std::size_t first = 0;
    while (first < lines.size() && lines[first].kind != LineKind::Stat) ++first;
    std::size_t last = first;
    while (last < lines.size() && lines[last].kind == LineKind::Stat) ++last;

    // One line, centred in a column. Returns the height it takes.
    const auto centred = [&](const Line& line, HFONT font, int x, int width, int top, bool paint) {
        std::string plain;
        COLORREF unused = kBody;
        PlainText(line.text, plain, unused, kBody);
        const std::wstring wide = Wide(plain);
        const int h = MeasureHeight(dc, wide, width, font);
        if (paint) {
            const RECT box = Rect(x, top, x + width, top + h);
            if (HasColorCodes(line.text)) LayoutPieces(dc, LinePieces(line.text, kBody), kBody, box, font, DT_CENTER, true);
            else Text(dc, wide, box, kBody, font, DT_CENTER | DT_WORDBREAK);
        }
        return h + S(3);
    };

    // Lines of the block outside the columns, full width and centred; an Input / Output table is centred and no wider
    // than needed.
    const auto flow = [&](std::size_t from, std::size_t to, HFONT font) {
        for (std::size_t i = from; i < to;) {
            if (lines[i].kind == LineKind::Row || lines[i].kind == LineKind::Header) {
                std::size_t end = i;
                while (end < to && (lines[end].kind == LineKind::Row || lines[end].kind == LineKind::Header)) ++end;
                const int tableW = std::min(width_, S(640));
                y = RenderTable(dc, lines, i, end, left + (width_ - tableW) / 2, tableW, y, draw);
                i = end;
            } else {
                y += centred(lines[i], font, left, width_, y, draw);
                ++i;
            }
        }
    };
    flow(0, first, fontBody);
    if (first > 0) y += S(8);

    const int gap = S(16);
    const int columnW = (width_ - gap) / 2;
    const bool pictureLeft = splitNow == 2;
    const int textX = pictureLeft ? left + columnW + gap : left;
    const int pictureX = pictureLeft ? left : left + columnW + gap;

    int textH = 0;
    for (std::size_t i = first; i < last; ++i) textH += centred(lines[i], fontBody, textX, columnW, 0, false);

    const std::wstring splitTitle = Wide(splitTitleNow);
    const int titleH = MeasureHeight(dc, splitTitle, columnW, fontBold);
    int pictureW = std::min(columnW, S(300));
    int pictureH = pictureW;
    Gdiplus::Bitmap* bitmap = FindImage(block.images[0]);
    if (bitmap != nullptr && bitmap->GetWidth() > 0 && bitmap->GetHeight() > 0) {
        pictureH = pictureW * static_cast<int>(bitmap->GetHeight()) / static_cast<int>(bitmap->GetWidth());
    }
    const int pictureColumnH = titleH + S(8) + pictureH;
    const int rowH = std::max(textH, pictureColumnH);

    if (draw) {
        int top = y + (rowH - textH) / 2;
        for (std::size_t i = first; i < last; ++i) top += centred(lines[i], fontBody, textX, columnW, top, true);
        const int pictureTop = y + (rowH - pictureColumnH) / 2;
        Text(dc, splitTitle, Rect(pictureX, pictureTop, pictureX + columnW, pictureTop + titleH), kHeaderGold, fontBold,
             DT_CENTER | DT_WORDBREAK);
        const int imageLeft = pictureX + (columnW - pictureW) / 2;
        const int imageTop = pictureTop + titleH + S(8);
        if (imageTop + pictureH > clipTop && imageTop < clipBottom) {
            DrawImageKey(*graphics, block.images[0], Rect(imageLeft, imageTop, imageLeft + pictureW, imageTop + pictureH));
        }
    }
    y += rowH + S(8);

    flow(last, lines.size(), fontSmall);  // footnotes, then the recipe table
    return y;
}

#include "overlay_calc.inc"
#include "overlay_switch.inc"

std::vector<std::size_t> OverlayState::ArrangedOrder(const Entry& entry, std::size_t entryIndex,
                                                    const PageView& view) {
    ArrangeKey key{database, pageIndex, entryIndex, view.socketFilter,
                   ActiveFilter(entry, &Block::tier, view.tierFilter),
                   ActiveFilter(entry, &Block::family, view.familyFilter), query, view.sort};
    if (!arrangeValid || !(key == arrangeKey)) {
        arrangeOrder = ArrangeBlocks(entry, key.socket, key.tier, key.family, key.sort, key.query);
        arrangeKey = key;
        arrangeValid = true;
    }
    return arrangeOrder;
}

void OverlayState::Paint(HDC target) {
    RECT client{};
    GetClientRect(window, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;

    const HDC dc = CreateCompatibleDC(target);
    const HBITMAP bitmap = CreateCompatibleBitmap(target, clientWidth, clientHeight);
    const HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    layout = ComputeLayout();
    EnsureBackgroundCache(target, clientWidth, clientHeight);
    BitBlt(dc, 0, 0, clientWidth, clientHeight, backgroundDc, 0, 0, SRCCOPY);

    const Page& page = CurrentPage();
    greenNames = (page.id == "set-items");
    gemwordCards = page.id == "gemwords";
    const bool goldItems = page.id == "uniques" || page.id == "runewords" || page.id == "gemwords";
    itemNameColor = page.id == "set-items" ? CodeColor('2') : page.id == "class-crafting" ? CodeColor('8')
                  : goldItems ? CodeColor('4') : kGold;
    tooltipNameColor = goldItems || page.id == "class-crafting" ? itemNameColor : kGold;

    // Stacked dark copies at growing offsets make a heavier shadow, which shows on the dark header.
    for (int offset : {S(1), S(2), S(3)}) ShadowText(dc, title, layout.title, fontTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER, offset);
    Text(dc, title, layout.title, kHeaderGold, fontTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    for (std::size_t i = 0; i < switches.size(); ++i) {
        DropShadow(dc, layout.switchButtons[i], S(2));
        DrawSwitchButton(dc, layout.switchButtons[i], switches[i].label, switches[i].current);
    }
    DropShadow(dc, layout.close, S(2));
    FillPanel(dc, layout.close, kPanel, 200);
    FrameColor(dc, layout.close, kBorder);
    Text(dc, L"X", layout.close, kText, fontBold, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    {
        const RECT box = SearchBoxRect(layout);
        CornerShadow(dc, box, S(2));
        FillColor(dc, box, kSearchFill);
        FrameColor(dc, box, kBorder);
    }
    if (!query.empty()) {
        CornerShadow(dc, layout.clearQuery, S(2));
        FillPanel(dc, layout.clearQuery, kPanel, 200);
        FrameColor(dc, layout.clearQuery, kBorder);
        Text(dc, L"X", layout.clearQuery, kText, fontBody, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    for (std::size_t i = 0; i < layout.tabs.size(); ++i) {
        const bool active = i == pageIndex;
        const bool matched = !query.empty() && !active && i < tabMatches.size() && tabMatches[i] > 0;
        CornerShadow(dc, layout.tabs[i], S(2));
        FillPanel(dc, layout.tabs[i], active ? kSelected : (matched ? kHighlightFill : kPanel), 200);
        FrameColor(dc, layout.tabs[i], active || matched ? kGold : kBorder);
        // A label that does not fit its tab is drawn in the smaller font, and with an ellipsis if even that is too wide.
        const std::wstring tabLabel = UiText(Wide(database->pages[i].title));
        HFONT tabFont = fontBody;
        UINT tabFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
        {
            const int have = layout.tabs[i].right - layout.tabs[i].left - S(8);
            if (MeasureWidth(dc, tabLabel, tabFont) > have) tabFont = fontSmall;
            const int need = MeasureWidth(dc, tabLabel, tabFont);
            if (need > have) {
                tabFlags |= DT_END_ELLIPSIS;
                DebugLogOnce("tab:" + database->pages[i].id, "tab label does not fit even in the small font: '" + database->pages[i].title +
                                                              "' needs " + std::to_string(need) + " px, the tab has " + std::to_string(have) + " px");
            }
        }
        Text(dc, tabLabel, Rect(layout.tabs[i].left + S(4), layout.tabs[i].top, layout.tabs[i].right - S(4), layout.tabs[i].bottom),
             kHeaderGold, tabFont, tabFlags);
        if (active) {  // the launcher marks the open tab with a gold underline
            FillColor(dc, Rect(layout.tabs[i].left + S(6), layout.tabs[i].bottom - S(4), layout.tabs[i].right - S(6),
                               layout.tabs[i].bottom - S(2)), kGold);
        }
    }

    PaintList(dc);

    detailHits.clear();
    CornerShadow(dc, layout.detail, S(2));
    FillPanel(dc, layout.detail, kPanel, 185);
    FrameColor(dc, layout.detail, kBorder);
    PageView& view = CurrentView();
    if (!filtered.empty() && view.selected < filtered.size()) {
        const Entry& entry = page.entries[filtered[view.selected]];
        const int top = BuildControls(dc, entry, view);
        clipTop = top - S(2);
        clipBottom = layout.detail.bottom - 1;
        const std::vector<std::size_t> order = ArrangedOrder(entry, filtered[view.selected], view);
        rowSortNow = ResolveBlockSort(entry, view.sort);
        splitNow = entry.splitLayout;
        splitTitleNow = entry.title + " Ascendancy";
        const bool headers = !entry.sortable && !(HasRuneBlocks(entry) && view.sort == BlockSort::Name);
        const int visible = layout.detail.bottom - top - S(10);
        const int total = RenderDetail(dc, entry, order, headers, top, 0, page.compact, false);
        view.detailScroll = std::clamp(view.detailScroll, 0, std::max(0, total - visible));
        scrollTotal = total;
        scrollVisible = visible;

        // Draw the cards onto a copy of the frame, then copy back only the detail band. A DC clip
        // is not reliable here: once GDI+ has drawn art into the DC, later card backgrounds ignore
        // it and spill over the tabs and chip bar.
        const RECT band{layout.detail.left + 1, clipTop, layout.detail.right - S(18), clipBottom};
        const HDC detailDc = CreateCompatibleDC(dc);
        const HBITMAP detailBitmap = CreateCompatibleBitmap(dc, clientWidth, clientHeight);
        const HGDIOBJ previousDetail = SelectObject(detailDc, detailBitmap);
        BitBlt(detailDc, 0, 0, clientWidth, clientHeight, dc, 0, 0, SRCCOPY);
        RenderDetail(detailDc, entry, order, headers, top, view.detailScroll, page.compact, true);
        BitBlt(dc, band.left, band.top, band.right - band.left, band.bottom - band.top, detailDc, band.left, band.top,
               SRCCOPY);
        SelectObject(detailDc, previousDetail);
        DeleteObject(detailBitmap);
        DeleteDC(detailDc);
        DrawScrollbar(dc, top, visible, total, view.detailScroll);
    } else {
        Text(dc, UiText(L"Nothing selected"), layout.detail, kDim, fontBody, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    DrawTooltip(dc);
    BitBlt(target, 0, 0, clientWidth, clientHeight, dc, 0, 0, SRCCOPY);
    SelectObject(dc, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window, message, wParam, lParam);
    }
    auto* self = reinterpret_cast<OverlayState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case kMsgToggle:
        self->Toggle();
        return 0;
    case kMsgOpen:
        self->Show();
        return 0;
    case kMsgReload:
        self->ApplyPendingDatabase();
        return 0;
    case kMsgShutdown:
        if (IsWindow(self->blocker)) DestroyWindow(self->blocker);
        self->blocker = nullptr;
        DestroyWindow(window);
        return 0;
    case WM_CLOSE:
        self->Hide();
        return 0;
    case WM_DESTROY:
        KillTimer(window, kTimerFocus);
        PostQuitMessage(0);
        return 0;
    case WM_SETCURSOR:
        HideSystemCursor();
        return TRUE;
    case WM_ERASEBKGND:
        if (!self->firstPainted && self->backgroundBrush != nullptr) {
            RECT area{};
            GetClientRect(window, &area);
            FillRect(reinterpret_cast<HDC>(wParam), &area, self->backgroundBrush);
        }
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        self->Paint(dc);
        EndPaint(window, &paint);
        self->firstPainted = true;
        return 0;
    }
    case WM_TIMER:
        if (wParam == kTimerFocus) self->SyncVisibility();
        else if (wParam == kTimerHandoff) self->HandoffTick();
        return 0;
    case WM_MOUSEMOVE:
        self->HandleMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSELEAVE:
        self->trackingMouse = false;
        if (!self->dragging) self->ClearHover();
        return 0;
    case WM_LBUTTONDOWN:
        self->HandleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        self->EndDrag();
        return 0;
    case WM_CAPTURECHANGED:
        self->dragging = false;
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        self->HandleWheel(point.x, point.y, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kIdSearch && HIWORD(wParam) == EN_CHANGE) {
            self->QueryChanged();
        }
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), kText);
        SetBkColor(reinterpret_cast<HDC>(wParam), kSearchFill);
        return reinterpret_cast<LRESULT>(self->searchBrush);
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

// Owns the drawn-cursor window and its message loop, separate from the wiki's UI thread.
DWORD WINAPI CursorThread(LPVOID parameter) {
    auto* self = static_cast<OverlayState*>(parameter);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW cursorClass{};
    cursorClass.cbSize = sizeof(cursorClass);
    cursorClass.lpfnWndProc = CursorProc;
    cursorClass.hInstance = instance;
    cursorClass.lpszClassName = self->cursorClassName.c_str();
    RegisterClassExW(&cursorClass);
    self->cursorWindow = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                                             WS_EX_TOPMOST,
                                         self->cursorClassName.c_str(), L"", WS_POPUP, 0, 0, self->S(kCursorWidth), self->S(kCursorHeight),
                                         nullptr, nullptr, instance, nullptr);
    if (self->cursorWindow != nullptr) {
        SetWindowLongPtrW(self->cursorWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&self->lastCursor));
        SetTimer(self->cursorWindow, 1, 8, nullptr);  // about 120 updates a second while shown
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(self->cursorWindow)) DestroyWindow(self->cursorWindow);
    self->cursorWindow = nullptr;
    UnregisterClassW(self->cursorClassName.c_str(), instance);
    return 0;
}

DWORD WINAPI UiThread(LPVOID parameter) {
    auto* self = static_cast<OverlayState*>(parameter);
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&self->gdiplusToken, &startup, nullptr) != Gdiplus::Ok) {
        SetEvent(self->ready);
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    windowClass.hbrBackground = CreateSolidBrush(kBackground);
    windowClass.lpszClassName = self->windowClassName.c_str();
    RegisterClassExW(&windowClass);

    WNDCLASSEXW blockerClass{};
    blockerClass.cbSize = sizeof(blockerClass);
    blockerClass.lpfnWndProc = BlockerProc;
    blockerClass.hInstance = windowClass.hInstance;
    blockerClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    blockerClass.lpszClassName = self->blockerClassName.c_str();
    RegisterClassExW(&blockerClass);

    self->CreateFonts();
    self->searchBrush = CreateSolidBrush(kSearchFill);
    self->backgroundBrush = CreateSolidBrush(kBackground);
    self->views.assign(self->database->pages.size(), PageView{});

    self->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, self->windowClassName.c_str(), self->title.c_str(),
                                   WS_POPUP | WS_CLIPCHILDREN, 0, 0, self->S(960), self->S(620),
                                   nullptr, nullptr, windowClass.hInstance, self);
    if (self->window == nullptr) {
        UnregisterClassW(self->blockerClassName.c_str(), blockerClass.hInstance);
        UnregisterClassW(self->windowClassName.c_str(), windowClass.hInstance);
        Gdiplus::GdiplusShutdown(self->gdiplusToken);
        SetEvent(self->ready);
        return 1;
    }

    SetLayeredWindowAttributes(self->window, 0, 255, LWA_ALPHA);

    // Covers every monitor. Hidden until the overlay opens; a missing blocker only
    // loses the input capture, the overlay still works.
    self->blocker = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, self->blockerClassName.c_str(), L"", WS_POPUP, 0, 0, 0, 0,
                                    nullptr, nullptr, windowClass.hInstance, nullptr);
    if (self->blocker != nullptr) {
        SetLayeredWindowAttributes(self->blocker, 0, kBlockerAlpha, LWA_ALPHA);
        SetWindowPos(self->blocker, nullptr, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                     GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    SetTimer(self->window, kTimerFocus, kTimerInterval, nullptr);
    self->FitToGame();
    const RECT editRect = SearchEditRect(self->layout, self->bodyLine, self->S(6));
    self->search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   editRect.left, editRect.top, editRect.right - editRect.left,
                                   editRect.bottom - editRect.top, self->window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSearch)),
                                   windowClass.hInstance, nullptr);
    SendMessageW(self->search, WM_SETFONT, reinterpret_cast<WPARAM>(self->fontBody), TRUE);
    SendMessageW(self->search, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    gSearchProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(self->search, GWLP_WNDPROC,
                                                               reinterpret_cast<LONG_PTR>(SearchEditProc)));
    self->Refilter();
    self->started = true;
    SetEvent(self->ready);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // The wiki key toggles: pressing it again closes the overlay. Alt combinations arrive as
        // WM_SYSKEYDOWN. continue skips TranslateMessage, so the key does not also type into the search box.
        if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) && IsWindowVisible(self->window) &&
            self->isCloseKey && self->isCloseKey(static_cast<unsigned int>(message.wParam))) {
            self->Hide();
            continue;
        }
        if (message.message == WM_KEYDOWN && IsWindowVisible(self->window)) {
            if (message.wParam == VK_ESCAPE) {
                self->Hide();
                continue;
            }
            if (message.wParam == VK_UP || message.wParam == VK_DOWN || message.wParam == VK_NEXT ||
                message.wParam == VK_PRIOR) {
                self->HandleKey(message.wParam);
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    self->ReleaseBackgroundCache();
    self->images.clear();
    if (self->searchBrush) DeleteObject(self->searchBrush);
    if (self->backgroundBrush) DeleteObject(self->backgroundBrush);
    if (self->fontBody) DeleteObject(self->fontBody);
    if (self->fontSmall) DeleteObject(self->fontSmall);
    if (self->fontBold) DeleteObject(self->fontBold);
    if (self->fontTitle) DeleteObject(self->fontTitle);
    UnregisterClassW(self->blockerClassName.c_str(), blockerClass.hInstance);
    UnregisterClassW(self->windowClassName.c_str(), windowClass.hInstance);
    Gdiplus::GdiplusShutdown(self->gdiplusToken);
    return 0;
}

}  // namespace

Overlay::Overlay() : impl_(std::make_unique<OverlayState>()) {}

Overlay::~Overlay() { Stop(); }

bool Overlay::Start(const Database& database, const std::wstring& dataDir, const std::wstring& title,
                    const std::wstring& instanceId, const std::vector<SwitchButton>& switches) {
    if (impl_->started || database.pages.empty()) return false;
    impl_->database = &database;
    impl_->dataDir = dataDir;
    impl_->title = title;
    impl_->windowClassName = L"EsrWikiOverlayWindow-" + instanceId;
    impl_->blockerClassName = L"EsrWikiInputBlocker-" + instanceId;
    impl_->cursorClassName = L"EsrWikiCursor-" + instanceId;
    impl_->switches = switches;
    impl_->cursorThread = CreateThread(nullptr, 0, CursorThread, impl_.get(), 0, &impl_->cursorThreadId);
    impl_->ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (impl_->ready == nullptr) return false;
    impl_->thread = CreateThread(nullptr, 0, UiThread, impl_.get(), 0, nullptr);
    if (impl_->thread == nullptr) {
        CloseHandle(impl_->ready);
        impl_->ready = nullptr;
        return false;
    }
    WaitForSingleObject(impl_->ready, 10000);
    if (!impl_->started) {
        Stop();
        return false;
    }
    return true;
}

bool Overlay::ReplaceDatabase(std::unique_ptr<Database> next) {
    if (!impl_->started || impl_->window == nullptr || !next || next->pages.empty()) return false;
    {
        std::lock_guard<std::mutex> guard(impl_->pendingLock);
        impl_->pendingDb = std::move(next);
    }
    PostMessageW(impl_->window, kMsgReload, 0, 0);
    return true;
}

void Overlay::Stop() {
    if (impl_->cursorThread != nullptr) {
        PostThreadMessageW(impl_->cursorThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(impl_->cursorThread, 2000);
        CloseHandle(impl_->cursorThread);
        impl_->cursorThread = nullptr;
        impl_->cursorThreadId = 0;
    }
    if (impl_->thread == nullptr) return;
    if (impl_->window != nullptr) PostMessageW(impl_->window, kMsgShutdown, 0, 0);
    WaitForSingleObject(impl_->thread, 5000);
    CloseHandle(impl_->thread);
    impl_->thread = nullptr;
    if (impl_->ready != nullptr) CloseHandle(impl_->ready);
    impl_->ready = nullptr;
    impl_->window = nullptr;
    impl_->blocker = nullptr;
    impl_->started = false;
}

void Overlay::Toggle() {
    if (impl_->window != nullptr) PostMessageW(impl_->window, kMsgToggle, 0, 0);
}

void Overlay::SetCloseKey(std::function<bool(unsigned int)> isCloseKey) {
    impl_->isCloseKey = std::move(isCloseKey);
}

void Overlay::Open() {
    if (impl_->window != nullptr) PostMessageW(impl_->window, kMsgOpen, 0, 0);
}

std::string UiTextUtf8(const std::string& english) {
    const std::wstring text = UiText(Wide(english));
    return text == Wide(english) ? std::string() : Narrow(text);  // empty: no translation
}

}  // namespace esrwiki
