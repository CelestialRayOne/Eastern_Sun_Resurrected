// Header buttons that open another panel (the switch buttons): drawing and hand-off. Included by overlay.cpp after
// OverlayState is defined.

void OverlayState::DrawSwitchButton(HDC dc, const RECT& rect, const std::wstring& label, bool current) {
    {
        const int need = MeasureWidth(dc, label, fontBold);
        const int have = rect.right - rect.left;
        if (need > have - S(8)) {
            DebugLogOnce("button:" + Narrow(label), "button label does not fit: '" + Narrow(label) + "' needs " +
                                                       std::to_string(need) + " px, the button is " + std::to_string(have) + " px");
        }
    }
    if (current) {
        // The panel the button is on: the dark selected fill with gold lettering, so it reads as the active one.
        FillPanel(dc, rect, kSelected, 235);
        FrameColor(dc, rect, kGold);
        Text(dc, label, rect, kHeaderGold, fontBold, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }
    // Like the launcher's gold PLAY button: a gold fill with dark lettering.
    FillPanel(dc, rect, RGB(196, 152, 66), 235);
    FrameColor(dc, rect, RGB(250, 226, 160));
    FrameColor(dc, Rect(rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1), RGB(122, 88, 28));
    Text(dc, label, rect, RGB(58, 40, 14), fontBold, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

// The other panel opens while this one stays up, and this one hides once the other has painted, so the dimmed
// backdrop and the cursor never drop out in between.
void OverlayState::BeginSwitch(const std::function<void()>& onSwitch) {
    open = false;
    EndDrag();
    ClearHover();
    if (onSwitch) {
        handingOff = true;
        gHandoffActive = true;
        gHandoffReady = false;
        handoffStarted = GetTickCount64();
        handoffSettled = false;
        gHandoffForeground = previousForeground;
        onSwitch();
        SetTimer(window, kTimerHandoff, kHandoffPollMs, nullptr);
    } else {
        Hide();
    }
}
