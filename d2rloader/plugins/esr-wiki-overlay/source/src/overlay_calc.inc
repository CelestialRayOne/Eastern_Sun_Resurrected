// ---------------------------------------------------------------------------------------------------------------
// The IAS calculator page (page id "ias-calculator"): the website's Frame Table, drawn in the detail pane in place
// of the entry's cards. The numbers come from ias_calc.cpp; this file only chooses and draws. It is included by
// overlay.cpp, after OverlayState is defined, and uses that file's drawing helpers.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr int kCalcClass = 0, kCalcForm = 1, kCalcSkill = 2, kCalcMainType = 3, kCalcMain = 4, kCalcOffType = 5, kCalcOff = 6;

std::string CalcWeaponLabel(const ias::Weapon& weapon) {
    return weapon.name + "  [" + (weapon.wsm > 0 ? "+" : "") + std::to_string(weapon.wsm) + "]";
}

std::vector<CalcOption> CalcOptionsFor(const ias::Data& data, const ias::Setup& setup, int field, const std::string& mainType,
                                       const std::string& offType) {
    std::vector<CalcOption> out;
    switch (field) {
    case kCalcClass:
        for (std::size_t i = 0; i < data.classes.size(); ++i) out.push_back({data.classes[i].name, static_cast<int>(i), ""});
        break;
    case kCalcForm:
        out.push_back({"None", -1, ""});
        for (std::size_t i = 0; i < data.forms.size(); ++i) out.push_back({data.forms[i].name, static_cast<int>(i), ""});
        break;
    case kCalcSkill:
        for (int i = 0; i < 10; ++i)
            if (ias::SkillAllowed(data, setup, ias::kSkills[i].id)) out.push_back({ias::kSkills[i].name, i, ""});
        break;
    case kCalcMainType:
    case kCalcOffType: {
        const bool off = field == kCalcOffType;
        out.push_back({off ? "None" : "Bare-handed", -1, ""});
        std::set<std::string> names;
        for (const ias::Weapon& weapon : data.weapons) {
            if (off && !ias::CanOffhand(data, setup, weapon)) continue;
            names.insert(weapon.typeName);
        }
        for (const std::string& name : names) out.push_back({name, 0, name});
        break;
    }
    case kCalcMain:
    case kCalcOff: {
        const bool off = field == kCalcOff;
        out.push_back({off ? "None" : "Bare-handed", -1, ""});
        const std::string& type = off ? offType : mainType;
        for (std::size_t i = 0; i < data.weapons.size(); ++i) {
            const ias::Weapon& weapon = data.weapons[i];
            if (weapon.typeName != type) continue;
            if (off && !ias::CanOffhand(data, setup, weapon)) continue;
            out.push_back({CalcWeaponLabel(weapon), static_cast<int>(i), ""});
        }
        break;
    }
    default:
        break;
    }
    return out;
}

}  // namespace

int* OverlayState::CalcNumber(int id) {
    switch (id) {
    case 0: return &calcSetup.ias;
    case 1: return &calcSetup.wias;
    case 2: return &calcSetup.oias;
    case 3: return &calcSetup.mar;
    case 4: return &calcSetup.oar;
    case 5: return &calcSetup.ar;
    default: return &calcSetup.fcr;
    }
}

void OverlayState::CalcChoose(int field, const CalcOption& option) {
    switch (field) {
    case kCalcClass: calcSetup.cls = option.value; break;
    case kCalcForm: calcSetup.form = option.value; break;
    case kCalcSkill: calcSetup.skill = ias::kSkills[option.value].id; break;
    case kCalcMainType:
        calcMainType = option.key;
        if (calcSetup.wep >= 0 && calcData && calcData->weapons[static_cast<std::size_t>(calcSetup.wep)].typeName != option.key) calcSetup.wep = -1;
        break;
    case kCalcMain: calcSetup.wep = option.value; break;
    case kCalcOffType:
        calcOffType = option.key;
        if (calcSetup.wep2 >= 0 && calcData && calcData->weapons[static_cast<std::size_t>(calcSetup.wep2)].typeName != option.key) calcSetup.wep2 = -1;
        break;
    case kCalcOff: calcSetup.wep2 = option.value; break;
    default: break;
    }
}

bool OverlayState::CalcActive() const { return !database->pages.empty() && CurrentPage().id == "ias-calculator"; }

bool OverlayState::CalcClick(int x, int y) {
    if (!CalcActive() || !Inside(layout.detail, x, y)) return false;
    for (const CalcHit& hit : calcHits) {
        if (!Inside(hit.rect, x, y)) continue;
        if (hit.kind == 0) {
            calcOpen = calcOpen == hit.index ? -1 : hit.index;
            calcListScroll = 0;
            calcNumFocus = -1;
        } else if (hit.kind == 1) {
            calcOpen = -1;
            calcNumFocus = hit.index;
            calcNumText = std::to_string(*CalcNumber(hit.index));
            calcNumFresh = true;
            if (search != nullptr) SetFocus(search);
        } else if (hit.kind == 2 && calcOpen >= 0 && calcData) {
            const std::vector<CalcOption> options = CalcOptionsFor(*calcData, calcSetup, calcOpen, calcMainType, calcOffType);
            if (hit.index >= 0 && static_cast<std::size_t>(hit.index) < options.size()) CalcChoose(calcOpen, options[static_cast<std::size_t>(hit.index)]);
            calcOpen = -1;
        }
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }
    if (calcOpen >= 0 || calcNumFocus >= 0) {
        calcOpen = -1;
        calcNumFocus = -1;
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }
    return false;
}

bool OverlayState::CalcWheel(int x, int y, int steps) {
    if (!CalcActive() || calcOpen < 0 || !Inside(calcListRect, x, y)) return false;
    calcListScroll = std::max(0, calcListScroll - steps);
    InvalidateRect(window, nullptr, FALSE);
    return true;
}

// Typing into the number field that has the focus. The search box keeps the keyboard, so its window procedure
// passes characters here first. Returns true when the character was used.
bool OverlayState::CalcChar(wchar_t ch) {
    if (!CalcActive() || calcNumFocus < 0) return false;
    if (ch == L'\r' || ch == L'\n' || ch == L'\t' || ch == 27) {
        calcNumFocus = -1;
    } else if (ch == L'\b') {
        if (calcNumFresh) calcNumText.clear();
        else if (!calcNumText.empty()) calcNumText.pop_back();
        calcNumFresh = false;
    } else if ((ch >= L'0' && ch <= L'9') || ch == L'-') {
        if (calcNumFresh) calcNumText.clear();
        calcNumFresh = false;
        if (ch == L'-') {
            if (!calcNumText.empty()) return true;
            calcNumText = "-";
        } else if (calcNumText.size() < 5) {
            calcNumText += static_cast<char>(ch);
        }
    } else {
        return true;  // other characters never reach the search box while a number is being typed
    }
    if (calcNumFocus >= 0) {
        long value = 0;
        if (calcNumText != "-" && !calcNumText.empty()) value = std::strtol(calcNumText.c_str(), nullptr, 10);
        *CalcNumber(calcNumFocus) = static_cast<int>(std::clamp(value, -1000L, 9999L));
    }
    InvalidateRect(window, nullptr, FALSE);
    return true;
}

// Calculator text that comes from the website data or fixed English labels; weapon names with a bracketed speed are left alone.
static std::string Tr(const std::string& text) {
    if (text.empty() || text.find('[') != std::string::npos) return text;
    return Narrow(UiText(Wide(text)));
}

void Overlay::SetCalculatorData(std::shared_ptr<const ias::Data> data) {
    if (!impl_ || !data) return;
    {
        std::lock_guard<std::mutex> guard(impl_->calcLock);
        impl_->calcPending = std::move(data);
    }
    if (impl_->window != nullptr) InvalidateRect(impl_->window, nullptr, FALSE);
}

int OverlayState::RenderCalculator(HDC dc, int top, int scroll, bool draw) {
    const RECT area = layout.detail;
    const int left = area.left + S(16);
    const int width_ = area.right - S(22) - left;
    int y = top - scroll;
    const int origin = y;
    if (draw) calcHits.clear();

    {
        std::lock_guard<std::mutex> guard(calcLock);
        if (calcPending) { calcData = std::move(calcPending); calcPending.reset(); }
    }
    const auto line = [&](const std::wstring& text, HFONT font, COLORREF color, int indent = 0) {
        const int h = MeasureHeight(dc, text, width_ - indent, font);
        if (draw) Text(dc, text, Rect(left + indent, y, left + width_, y + h), color, font, DT_LEFT | DT_WORDBREAK);
        y += h;
    };
    line(UiText(L"Calculator"), fontTitle, kHeaderGold);
    y += S(2);
    if (!calcData) {
        line(UiText(L"The calculator data is not available."), fontBody, kDim);
        return y - origin;
    }
    line(UiText(L"Attacks and casts resolve in whole animation frames at 25 frames per second, so speed only changes when you cross a "
         L"breakpoint. Pick your setup to see where you land and what the next tier costs."),
         fontSmall, kDim);
    y += S(10);

    const ias::Data& data = *calcData;
    const ias::Result r = ias::Compute(data, calcSetup);
    calcSetup = r.setup;
    if (calcSetup.wep >= 0) calcMainType = data.weapons[static_cast<std::size_t>(calcSetup.wep)].typeName;
    if (calcSetup.wep2 >= 0) calcOffType = data.weapons[static_cast<std::size_t>(calcSetup.wep2)].typeName;
    if (!r.offhandLive) calcOffType.clear();

    const int rowH = std::max(S(26), smallLine + S(10));
    // The caption column is as wide as the longest caption, so none is cut off.
    int labelW = S(190);
    for (const wchar_t* caption : {L"Increased attack speed (all gear)", L"Increased attack speed (main hand)",
                                   L"Increased attack speed (off hand)", L"Base attack speed (skills, buffs)",
                                   L"Base attack speed (off hand)", L"Main hand: weapon", L"Off hand: weapon"})
        labelW = std::max(labelW, MeasureWidth(dc, UiText(caption), fontSmall) + S(20));
    labelW = std::min(labelW, width_ / 2);
    const int boxW = std::min(S(330), width_ - labelW);
    const int numW = S(110);

    const auto hint = [&](const std::string& text, int boxWidth) {
        if (text.empty()) return;
        const std::wstring wide = Wide(Tr(text));
        const int h = MeasureHeight(dc, wide, boxWidth, fontSmall);
        if (draw) Text(dc, wide, Rect(left + labelW, y, left + labelW + boxWidth, y + h), kDim, fontSmall, DT_LEFT | DT_WORDBREAK);
        y += h;
    };
    const auto caption = [&](const std::string& text, bool enabled) {
        if (draw) Text(dc, UiText(Wide(text)), Rect(left, y, left + labelW - S(8), y + rowH), enabled ? kText : kDim, fontSmall,
                       DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    };
    int popupField = -1;
    RECT popupBox{};
    const auto choiceRow = [&](int field, const std::string& title, const std::string& value, bool enabled, const std::string& hintText) {
        caption(title, enabled);
        const RECT box = Rect(left + labelW, y, left + labelW + boxW, y + rowH);
        if (draw) {
            FillPanel(dc, box, kSearchFill, enabled ? 235 : 150);
            FrameColor(dc, box, calcOpen == field ? kGold : kBorder);
            Text(dc, Wide(Tr(value)), Rect(box.left + S(8), box.top, box.right - S(26), box.bottom), enabled ? kText : kDim, fontBody,
                 DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            Text(dc, L"v", Rect(box.right - S(24), box.top, box.right - S(6), box.bottom), kDim, fontSmall,
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            if (enabled) calcHits.push_back({box, 0, field});
        }
        y += rowH;
        hint(hintText, boxW);
        y += S(4);
        if (calcOpen == field && enabled) { popupField = field; popupBox = box; }  // drawn last, on top of the rest
    };
    const auto numberRow = [&](int id, const std::string& title, const std::string& hintText) {
        caption(title, true);
        const RECT box = Rect(left + labelW, y, left + labelW + numW, y + rowH);
        if (draw) {
            FillPanel(dc, box, kSearchFill, 235);
            FrameColor(dc, box, calcNumFocus == id ? kGold : kBorder);
            const std::string shown = calcNumFocus == id ? calcNumText + "|" : std::to_string(*CalcNumber(id));
            Text(dc, Wide(shown), Rect(box.left + S(8), box.top, box.right - S(8), box.bottom), kText, fontBody,
                 DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
            calcHits.push_back({box, 1, id});
        }
        y += rowH;
        hint(hintText, boxW);
        y += S(4);
    };

    choiceRow(kCalcClass, "Class", data.classes[static_cast<std::size_t>(calcSetup.cls)].name, true, "");
    choiceRow(kCalcForm, "Form", calcSetup.form < 0 ? "None" : data.forms[static_cast<std::size_t>(calcSetup.form)].name, true, r.formHint);
    std::string skillName = "Standard attack";
    for (const ias::SkillChoice& choice : ias::kSkills)
        if (calcSetup.skill == choice.id) skillName = choice.name;
    choiceRow(kCalcSkill, "Skill", skillName, true, "");
    choiceRow(kCalcMainType, "Main hand: type", calcMainType.empty() ? "Bare-handed" : calcMainType, true, "");
    choiceRow(kCalcMain, "Main hand: weapon",
              calcSetup.wep < 0 ? "Bare-handed" : CalcWeaponLabel(data.weapons[static_cast<std::size_t>(calcSetup.wep)]),
              !calcMainType.empty(), "Number in brackets is the weapon speed modifier. Lower is faster.");
    choiceRow(kCalcOffType, "Off hand: type", calcOffType.empty() ? "None" : calcOffType, r.offhandLive, r.offhandHint);
    choiceRow(kCalcOff, "Off hand: weapon",
              calcSetup.wep2 < 0 ? "None" : CalcWeaponLabel(data.weapons[static_cast<std::size_t>(calcSetup.wep2)]),
              r.offhandLive && !calcOffType.empty(), "");
    y += S(4);
    numberRow(0, "Increased attack speed (all gear)", r.iasHint);
    if (r.showMainHandIas) numberRow(1, "Increased attack speed (main hand)", r.mainHandIasHint);
    if (r.showOffHandFields) numberRow(2, "Increased attack speed (off hand)", "Averaged with the main hand - the engine toggles each weapon's stats on and off.");
    numberRow(3, "Base attack speed (weapon)", "Base attack speed granted by the weapon itself. Its bracketed speed is already counted.");
    if (r.showOffHandFields) numberRow(4, "Base attack speed (off hand)", "Averaged with the main hand, like the speed modifiers.");
    numberRow(5, "Base attack speed (skills, buffs)", "Skills, buffs and auras. Slows count as negatives.");
    numberRow(6, "Faster cast rate", Narrow(UiFormat(L"Caps at {0}.", {std::to_wstring(data.k.fcrCap)})));
    y += S(8);

    // the readout
    const auto readout = [&](const char* key, const std::string& value, COLORREF color) {
        if (draw) {
            Text(dc, UiText(Wide(key)), Rect(left, y, left + labelW + S(40), y + rowH), kDim, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            Text(dc, Wide(value), Rect(left + labelW + S(40), y, left + width_, y + rowH), color, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
        y += rowH - S(4);
    };
    readout("Animation", r.anim, kText);
    if (r.hasAnim) {
        readout("Base frames", r.baseFrames, kText);
        readout("Weapon speed modifier", r.speedModifier, kText);
        readout("Total increased attack speed", r.totalIas, kText);
        readout("Flat speed", r.flatSpeed, kText);
        readout("Effective IAS", r.effectiveIas, r.effectiveWarn ? RGB(214, 120, 76) : kText);
    }
    y += S(14);

    const int cw = width_ / 5;
    const auto bigNumbers = [&](const std::string& frames, const std::string& frameLabel, const std::string& perSecond,
                                const std::string& perSecondLabel, const std::string& solo) {
        const int h = titleLine + S(4);
        if (draw) {
            Text(dc, Wide(frames), Rect(left, y, left + S(60), y + h), kGold, fontTitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            Text(dc, Wide(frameLabel + (solo.empty() ? "" : "   " + solo)), Rect(left + S(60), y, left + width_ - cw, y + h), kDim, fontSmall,
                 DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            Text(dc, Wide(perSecond + " " + perSecondLabel), Rect(left + width_ - cw - S(20), y, left + width_, y + h), kText, fontBold,
                 DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        y += h + S(6);
    };
    const auto table = [&](const std::string& statTitle, const std::string& perSecondTitle, const ias::Table& t) {
        const int rows = rowH - S(4);
        if (draw) {
            Text(dc, Wide(statTitle), Rect(left + S(8), y, left + cw, y + rows), kHeaderGold, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            Text(dc, UiText(L"Frames"), Rect(left + cw, y, left + 2 * cw, y + rows), kHeaderGold, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            Text(dc, Wide(perSecondTitle), Rect(left + 2 * cw, y, left + 3 * cw, y + rows), kHeaderGold, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            Text(dc, UiText(L"From here"), Rect(left + 3 * cw, y, left + width_, y + rows), kHeaderGold, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            FillColor(dc, Rect(left, y + rows, left + width_, y + rows + 1), kBorder);
        }
        y += rows + S(2);
        if (t.rows.empty()) {
            line(UiText(L"Not available for this setup."), fontSmall, kDim);
        }
        for (const ias::TableRow& row : t.rows) {
            if (draw) {
                const RECT band = Rect(left, y, left + width_, y + rows);
                if (row.state == 1) FillPanel(dc, band, kSelected, 120);
                if (row.state == 2) { FillPanel(dc, band, kSelected, 200); FrameColor(dc, band, kGold); }
                const COLORREF color = row.state == 2 ? kGold : row.state == 3 ? kText : row.state == 1 ? kBody : kDim;
                Text(dc, Wide(std::to_string(row.stat) + "%"), Rect(left + S(8), y, left + cw, y + rows), color, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                Text(dc, Wide(std::to_string(row.frames)), Rect(left + cw, y, left + 2 * cw, y + rows), color, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                Text(dc, Wide(row.perSecond), Rect(left + 2 * cw, y, left + 3 * cw, y + rows), color, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                Text(dc, Wide(row.gap), Rect(left + 3 * cw, y, left + width_, y + rows), color, fontSmall, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
            y += rows;
        }
        y += S(6);
        if (!t.note.empty()) line(Wide(t.note), fontSmall, kDim);
        y += S(10);
    };

    line(UiText(L"Attack"), fontBold, kHeaderGold);
    y += S(4);
    bigNumbers(r.attackFrames, r.attackLabel, r.attackPerSecond, r.attackPerSecondLabel, r.attackSolo);
    table(r.tableTitleStat, r.tableTitlePerSecond, r.attack);
    line(UiText(L"Cast"), fontBold, kHeaderGold);
    y += S(4);
    bigNumbers(r.castFrames, Tr("frames per cast"), r.castPerSecond, Tr("casts / sec"), "");
    table(Tr("FCR needed"), Tr("Casts / sec"), r.cast);
    line(UiText(L"Values come from the Frame Table on the Eastern Sun Resurrected website and refresh with it."), fontSmall, kDim);
    y += S(10);

    // the open drop-down list, drawn over the page instead of pushing it down
    if (draw && popupField >= 0) {
        const int field = popupField;
        const RECT box = popupBox;
        std::vector<CalcHit> popupHits;
        const std::vector<CalcOption> options = CalcOptionsFor(data, calcSetup, field, calcMainType, calcOffType);
        const int count = static_cast<int>(options.size());
        const int visible = std::min(count, 9);
        calcListScroll = std::clamp(calcListScroll, 0, std::max(0, count - visible));
        const int listH = visible * rowH + 2;
        int listTop = box.bottom;  // drawn over what lies below, like an ordinary drop-down
        if (listTop + listH > area.bottom && box.top - listH >= area.top) listTop = box.top - listH;  // no room below: open upwards
        const RECT list = Rect(box.left, listTop, box.right, listTop + listH);
        {
            calcListRect = list;
            FillPanel(dc, list, kPanel, 250);
            FrameColor(dc, list, kGold);
            for (int k = 0; k < visible; ++k) {
                const int index = calcListScroll + k;
                const RECT item = Rect(list.left + 1, list.top + 1 + k * rowH, list.right - 1, list.top + 1 + (k + 1) * rowH);
                const CalcOption& option = options[static_cast<std::size_t>(index)];
                bool current = false;
                switch (field) {
                case kCalcClass: current = option.value == calcSetup.cls; break;
                case kCalcForm: current = option.value == calcSetup.form; break;
                case kCalcSkill: current = calcSetup.skill == ias::kSkills[option.value].id; break;
                case kCalcMainType: current = option.key == calcMainType; break;
                case kCalcMain: current = option.value == calcSetup.wep; break;
                case kCalcOffType: current = option.key == calcOffType; break;
                default: current = option.value == calcSetup.wep2; break;
                }
                if (current) FillColor(dc, item, kSelected);
                Text(dc, Wide(Tr(option.label)), Rect(item.left + S(8), item.top, item.right - S(14), item.bottom), current ? kGold : kText, fontBody,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                popupHits.push_back({item, 2, index});
            }
            if (count > visible) {  // a thin thumb on the right edge shows the position
                const int track = visible * rowH;
                const int thumb = std::max(S(12), track * visible / count);
                const int offset = (track - thumb) * calcListScroll / std::max(1, count - visible);
                FillColor(dc, Rect(list.right - S(6), list.top + 1 + offset, list.right - S(3), list.top + 1 + offset + thumb), kBorder);
            }
        }
        calcHits.insert(calcHits.begin(), popupHits.begin(), popupHits.end());  // the list is on top, so it takes clicks first
    }
    return y - origin;
}
