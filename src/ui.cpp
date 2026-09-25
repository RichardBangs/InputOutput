#include "app.h"
#include <windowsx.h>

namespace io {
namespace {
constexpr int IdSettings = 400, IdExit = 401, IdMinimise = 404, IdDisplays = 410, IdOutputs = 411,
              IdInputs = 412, IdHeaderFirst = 460;
constexpr int IdList = 500, IdName = 501, IdSave = 503, IdCapture = 504, IdUpdate = 505, IdDelete = 506,
              IdUp = 507, IdDown = 508, IdFallback = 509, IdWindows = 510, IdStartup = 511, IdCalls = 512,
              IdFolder = 513, IdGithub = 514, IdNavigation = 515, IdVersion = 516;
constexpr wchar_t GithubUrl[] = L"https://github.com/RichardBangs/InputOutput";
constexpr const wchar_t *PageNames[] = {L"Displays", L"Audio output", L"Microphone", L"General"};
constexpr const wchar_t *PageIcons[] = {L"\uE7F4", L"\uE767", L"\uE720", L"\uE713"};
std::wstring textOf(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
std::wstring currentName(const std::vector<AudioChoice> &choices, const std::wstring &id) {
    for (auto &c : choices)
        if (c.id == id)
            return c.name;
    return {};
}
} // namespace
void App::buildPopup(bool syncDpi) {
    const auto &ui = popupUi_;
    if (!popup_)
        return;
    if (syncDpi)
        syncPopupDpi();
    int focusId = GetDlgCtrlID(GetFocus());
    if (popupTips_)
        DestroyWindow(popupTips_);
    popupTips_ = nullptr;
    for (auto &[c, y] : popupChildren_) {
        (void)y;
        DestroyWindow(c);
    }
    popupChildren_.clear();
    rows_.clear();
    popupTips_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                 WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, popup_, nullptr, instance_, nullptr);
    int y = 8;
    auto child = [&](const wchar_t *cls, const std::wstring &text, DWORD style, int x, int at, int width,
                     int height, int id) {
        auto h = control(popup_, cls, text, style, x, at, width, height, id);
        popupChildren_.push_back({h, at});
        return h;
    };
    auto label = [&](const std::wstring &text, int id = 0) {
        auto h = child(L"STATIC", text, SS_LEFT, 16, y, 278, 24, id);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui.smallFont), TRUE);
        y += 26;
    };
    auto heading = [&](int page) {
        // The native control retains its readable name for accessibility; only its visual is an icon.
        auto h =
            child(L"STATIC", PageNames[page], SS_OWNERDRAW | SS_NOTIFY, 16, y, 24, 24, IdHeaderFirst + page);
        if (popupTips_) {
            TOOLINFOW tool{sizeof(tool)};
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = popup_;
            tool.uId = reinterpret_cast<UINT_PTR>(h);
            tool.lpszText = const_cast<wchar_t *>(PageNames[page]);
            SendMessageW(popupTips_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        y += 26;
    };
    auto button = [&](const std::wstring &text, int id, bool checked = false, bool enabled = true) {
        auto h = child(L"BUTTON", (checked ? L"Current: " : L"") + text,
                       BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP, 8, y, 296, 34, id);
        rows_[h] = {text, checked};
        EnableWindow(h, enabled && !busy_);
        y += 35;
        return h;
    };
    heading(0);
    bool any = false;
    for (size_t i = 0; i < config_.displays.size(); ++i) {
        auto &p = config_.displays[i];
        if (!p.shown)
            continue;
        any = true;
        bool available = smoke_ || displaysAvailable(p.snapshot, available_);
        button(p.name + (available ? L"" : L"  ·  Disconnected"), 1000 + static_cast<int>(i),
               !busy_ && sameDisplays(p.snapshot, current_), available);
    }
    if (!any)
        button(L"Add display presets…", IdDisplays);
    y += 9;
    auto audioGroup = [&](int page, const std::vector<AudioChoice> &choices,
                          const std::vector<AudioDevice> &devices, const std::wstring &current, int base,
                          int emptyId) {
        heading(page);
        bool has = false;
        for (size_t i = 0; i < choices.size(); ++i) {
            auto &c = choices[i];
            if (!c.shown)
                continue;
            has = true;
            bool available = audioAvailable(devices, c.id);
            button(c.name + (available ? L"" : L"  ·  Unavailable"), base + static_cast<int>(i),
                   c.id == current, available);
        }
        if (!has)
            button(L"Choose devices…", emptyId);
        else if (!current.empty() && std::none_of(choices.begin(), choices.end(),
                                                  [&](auto &c) { return c.shown && c.id == current; }))
            label(L"Current device is outside this menu");
        y += 9;
    };
    audioGroup(1, config_.outputs, outputs_, currentOutput_, 2000, IdOutputs);
    audioGroup(2, config_.inputs, inputs_, currentInput_, 3000, IdInputs);
    if (busy_)
        label(L"Switching…");
    else if (!notice_.empty()) {
        auto h = child(L"STATIC", notice_, SS_LEFT, 16, y, 278, 58, 451);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(ui.smallFont), TRUE);
        y += 62;
    }
    auto s = child(L"BUTTON", L"Settings…", BS_PUSHBUTTON | WS_TABSTOP, 12, y, 100, 30, IdSettings);
    child(L"BUTTON", L"Minimise", BS_PUSHBUTTON | WS_TABSTOP, 124, y, 102, 30, IdMinimise);
    auto exit = child(L"BUTTON", L"Exit", BS_PUSHBUTTON | WS_TABSTOP, 238, y, 62, 30, IdExit);
    EnableWindow(s, !busy_);
    EnableWindow(exit, !busy_);
    y += 42;
    popupContent_ = y;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(popup_, MONITOR_DEFAULTTONEAREST), &mi);
    int maxHeight = mi.rcWork.bottom - mi.rcWork.top - ui.scale(24);
    popupHeight_ = std::min(ui.scale(popupContent_), maxHeight);
    SetWindowPos(popup_, nullptr, 0, 0, ui.scale(330), popupHeight_,
                 SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    popupScroll_ = std::clamp(popupScroll_, 0, std::max(0, ui.scale(popupContent_) - popupHeight_));
    SCROLLINFO si{sizeof(si),
                  SIF_RANGE | SIF_PAGE | SIF_POS,
                  0,
                  ui.scale(popupContent_) - 1,
                  static_cast<UINT>(popupHeight_),
                  popupScroll_,
                  0};
    SetScrollInfo(popup_, SB_VERT, &si, TRUE);
    scrollPopup(0);
    if (focusId) {
        HWND target = GetDlgItem(popup_, focusId);
        if (target && IsWindowEnabled(target))
            SetFocus(target);
    }
    InvalidateRect(popup_, nullptr, TRUE);
    if (syncDpi)
        positionPopup();
}
void App::scrollPopup(int delta) {
    const auto &ui = popupUi_;
    popupScroll_ = std::clamp(popupScroll_ + delta, 0, std::max(0, ui.scale(popupContent_) - popupHeight_));
    for (auto &[c, y] : popupChildren_) {
        RECT r{};
        GetWindowRect(c, &r);
        POINT p{r.left, r.top};
        ScreenToClient(popup_, &p);
        SetWindowPos(c, nullptr, p.x, ui.scale(y) - popupScroll_, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SetScrollPos(popup_, SB_VERT, popupScroll_, TRUE);
}
POINT App::popupAnchor() const {
    POINT p{};
    GetCursorPos(&p);
    NOTIFYICONIDENTIFIER id{};
    id.cbSize = sizeof(id);
    id.hWnd = owner_;
    id.uID = 1;
    id.guidItem = tray_.guidItem;
    RECT iconRect{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &iconRect))) {
        p.x = iconRect.right - 1;
        p.y = iconRect.top;
    }
    return p;
}
void App::syncPopupDpi() {
    auto target = MonitorFromPoint(popupAnchor(), MONITOR_DEFAULTTONEAREST);
    if (MonitorFromWindow(popup_, MONITOR_DEFAULTTONEAREST) != target) {
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(target, &mi)) {
            // Put the window wholly on the target monitor before asking Windows for its DPI.
            // The final bounds are calculated below; this intermediate move is never painted.
            SetWindowPos(popup_, nullptr, mi.rcWork.left + 1, mi.rcWork.top + 1, 1, 1,
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
        }
    }
    updateWindowDpi(popup_, GetDpiForWindow(popup_));
}
void App::positionPopup() {
    const auto &ui = popupUi_;
    POINT p = popupAnchor();
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
    int width = ui.scale(330);
    int x = std::clamp(p.x - width, mi.rcWork.left + ui.scale(6),
                       std::max(mi.rcWork.left + ui.scale(6), mi.rcWork.right - width - ui.scale(6)));
    int y = std::clamp(p.y - popupHeight_ - ui.scale(6), mi.rcWork.top + ui.scale(6),
                       std::max(mi.rcWork.top + ui.scale(6), mi.rcWork.bottom - popupHeight_ - ui.scale(6)));
    SetWindowPos(popup_, HWND_TOPMOST, x, y, width, popupHeight_, SWP_NOACTIVATE);
}
void App::showPopup() {
    if (!busy_)
        try {
            refresh();
        } catch (const std::exception &e) {
            notice_ = wide(e.what());
        }
    buildPopup();
    positionPopup();
    ShowWindow(popup_, SW_SHOWNORMAL);
    SetForegroundWindow(popup_);
    auto c = GetNextDlgTabItem(popup_, nullptr, FALSE);
    if (c)
        SetFocus(c);
}
void App::drawButton(const DRAWITEMSTRUCT &item) {
    const auto &ui = GetParent(item.hwndItem) == settings_ ? settingsUi_ : popupUi_;
    auto it = rows_.find(item.hwndItem);
    HDC dc = item.hDC;
    RECT r = item.rcItem;
    bool disabled = item.itemState & ODS_DISABLED;
    bool pressed = item.itemState & ODS_SELECTED;
    bool focused = item.itemState & ODS_FOCUS;
    bool menuRow = it != rows_.end();
    bool checked = menuRow && it->second.selected;
    bool primary = item.CtlID == IdSave || item.CtlID == IdCapture;
    bool danger = item.CtlID == IdDelete;
    COLORREF background = theme_.panel(), foreground = theme_.ink(), border = theme_.border();
    if (disabled) {
        background = menuRow ? theme_.panel() : theme_.disabledFill();
        foreground = theme_.disabled();
    } else if (primary) {
        background = pressed ? theme_.accentPressed() : theme_.accent();
        foreground = theme_.onAccent();
        border = background;
    } else if (checked) {
        background = theme_.tint();
        foreground = theme_.onTint();
        border = background;
    } else {
        if (pressed || focused)
            background = theme_.tint();
        if (danger)
            foreground = theme_.danger();
    }
    fillColor(dc, r,
              GetParent(item.hwndItem) == settings_ ? settingsBackground(item.hwndItem) : theme_.panel());
    roundedBox(dc, r, background, menuRow ? background : border, ui.scale(4));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, foreground);
    bool saveIcon = item.CtlID == IdSave;
    auto old = SelectObject(dc, saveIcon ? ui.iconFont : ui.font);
    std::wstring label = saveIcon ? L"\uE74E" : menuRow ? it->second.label : textOf(item.hwndItem);
    if (menuRow) {
        RECT mark = r;
        mark.left += ui.scale(8);
        mark.right = mark.left + ui.scale(22);
        if (checked)
            DrawTextW(dc, L"✓", 1, &mark, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        r.left += ui.scale(36);
    } else {
        r.left += ui.scale(6);
    }
    r.right -= ui.scale(6);
    DrawTextW(dc, label.c_str(), -1, &r,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | (menuRow ? DT_LEFT : DT_CENTER));
    if (focused) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -ui.scale(3), -ui.scale(3));
        DrawFocusRect(dc, &focus);
    }
    SelectObject(dc, old);
}
void App::drawPopupHeader(const DRAWITEMSTRUCT &item) {
    const auto &ui = popupUi_;
    auto page = item.CtlID - IdHeaderFirst;
    if (page >= 3)
        return;
    RECT rect = item.rcItem;
    fillColor(item.hDC, rect, theme_.panel());
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, theme_.muted());
    auto font = SelectObject(item.hDC, ui.iconFont);
    DrawTextW(item.hDC, PageIcons[page], -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    SelectObject(item.hDC, font);
}
void App::drawNavigation(const DRAWITEMSTRUCT &item) {
    const auto &ui = settingsUi_;
    if (item.itemID >= std::size(PageNames))
        return;
    bool active = static_cast<int>(item.itemID) == page_;
    RECT rect = item.rcItem;
    fillColor(item.hDC, rect, theme_.canvas());
    InflateRect(&rect, 0, -ui.scale(2));
    if (active) {
        roundedBox(item.hDC, rect, theme_.navigation(), theme_.navigation(), ui.scale(4));
        RECT indicator{rect.left, rect.top + ui.scale(10), rect.left + ui.scale(3),
                       rect.bottom - ui.scale(10)};
        roundedBox(item.hDC, indicator, theme_.accent(), theme_.accent(), ui.scale(2));
    }
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, active && theme_.highContrast ? theme_.onAccent() : theme_.ink());
    auto font = SelectObject(item.hDC, ui.iconFont);
    RECT icon = rect;
    icon.left += ui.scale(14);
    icon.right = icon.left + ui.scale(20);
    DrawTextW(item.hDC, PageIcons[item.itemID], -1, &icon, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    SelectObject(item.hDC, ui.font);
    RECT label = rect;
    label.left += ui.scale(46);
    DrawTextW(item.hDC, PageNames[item.itemID], -1, &label, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    if (item.itemState & ODS_FOCUS) {
        InflateRect(&rect, -ui.scale(3), -ui.scale(3));
        DrawFocusRect(item.hDC, &rect);
    }
    SelectObject(item.hDC, font);
}
LRESULT App::drawList(NMHDR *notification) {
    const auto &ui = settingsUi_;
    if (notification->hwndFrom == list_) {
        auto draw = reinterpret_cast<NMLVCUSTOMDRAW *>(notification);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
            return CDRF_NOTIFYITEMDRAW;
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            bool selected =
                ListView_GetItemState(list_, static_cast<int>(draw->nmcd.dwItemSpec), LVIS_SELECTED);
            draw->clrText = selected ? theme_.onTint() : theme_.ink();
            draw->clrTextBk = selected ? theme_.tint() : theme_.panel();
            draw->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
            return CDRF_NEWFONT;
        }
    } else if (list_ && notification->hwndFrom == ListView_GetHeader(list_)) {
        auto draw = reinterpret_cast<NMCUSTOMDRAW *>(notification);
        if (draw->dwDrawStage == CDDS_PREPAINT)
            return CDRF_NOTIFYITEMDRAW;
        if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
            fillColor(draw->hdc, draw->rc, theme_.panel());
            wchar_t title[80]{};
            HDITEMW item{};
            item.mask = HDI_TEXT;
            item.pszText = title;
            item.cchTextMax = 80;
            Header_GetItem(notification->hwndFrom, static_cast<int>(draw->dwItemSpec), &item);
            RECT rect = draw->rc;
            rect.left += ui.scale(7);
            SetBkMode(draw->hdc, TRANSPARENT);
            SetTextColor(draw->hdc, theme_.ink());
            auto font = SelectObject(draw->hdc, ui.font);
            DrawTextW(draw->hdc, title, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(draw->hdc, font);
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}
LRESULT CALLBACK App::popupProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        app->popup_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(hwnd, msg, w, l);
    try {
        return app->popupMessage(msg, w, l);
    } catch (const std::exception &e) {
        app->notifyError(wide(e.what()));
        return 0;
    }
}
LRESULT App::popupMessage(UINT msg, WPARAM w, LPARAM l) {
    const auto &ui = popupUi_;
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT r{};
        GetClientRect(popup_, &r);
        fillColor(reinterpret_cast<HDC>(w), r, theme_.panel());
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w);
        SetBkColor(dc, theme_.panel());
        SetTextColor(dc, theme_.muted());
        SetDCBrushColor(dc, theme_.panel());
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_DRAWITEM: {
        auto item = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        if (item->CtlType == ODT_STATIC)
            drawPopupHeader(*item);
        else
            drawButton(*item);
        return TRUE;
    }
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE)
            ShowWindow(popup_, SW_HIDE);
        return 0;
    case WM_ACTIVATEAPP:
        if (!w)
            ShowWindow(popup_, SW_HIDE);
        return 0;
    case WM_COMMAND: {
        if (HIWORD(w) == BN_SETFOCUS) {
            auto child = reinterpret_cast<HWND>(l);
            RECT r{};
            GetWindowRect(child, &r);
            POINT p{r.left, r.top};
            ScreenToClient(popup_, &p);
            if (p.y < 0)
                scrollPopup(p.y);
            else if (p.y + r.bottom - r.top > popupHeight_)
                scrollPopup(p.y + r.bottom - r.top - popupHeight_);
            return 0;
        }
        if (HIWORD(w) != BN_CLICKED)
            return 0;
        int id = LOWORD(w);
        if (id == IDCANCEL || id == IdMinimise) {
            ShowWindow(popup_, SW_HIDE);
            return 0;
        }
        if (busy_)
            return 0;
        if (id == IdSettings) {
            showSettings();
            return 0;
        }
        if (id == IdExit) {
            PostMessageW(owner_, WM_CLOSE, 0, 0);
            return 0;
        }
        if (id >= IdDisplays && id <= IdInputs) {
            showSettings(id - IdDisplays);
            return 0;
        }
        if (id >= 1000 && id < 1064)
            switchDisplay(id - 1000);
        if (id >= 2000 && id < 2256 && size_t(id - 2000) < config_.outputs.size())
            switchAudio(config_.outputs[id - 2000].id, eRender);
        if (id >= 3000 && id < 3256 && size_t(id - 3000) < config_.inputs.size())
            switchAudio(config_.inputs[id - 3000].id, eCapture);
        return 0;
    }
    case WM_MOUSEWHEEL:
        scrollPopup(-GET_WHEEL_DELTA_WPARAM(w) * ui.scale(70) / WHEEL_DELTA);
        return 0;
    case WM_VSCROLL: {
        int amount = 0;
        switch (LOWORD(w)) {
        case SB_LINEUP:
            amount = -ui.scale(34);
            break;
        case SB_LINEDOWN:
            amount = ui.scale(34);
            break;
        case SB_PAGEUP:
            amount = -popupHeight_ / 2;
            break;
        case SB_PAGEDOWN:
            amount = popupHeight_ / 2;
            break;
        case SB_THUMBTRACK: {
            SCROLLINFO si{sizeof(si), SIF_TRACKPOS};
            GetScrollInfo(popup_, SB_VERT, &si);
            amount = si.nTrackPos - popupScroll_;
            break;
        }
        }
        scrollPopup(amount);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(popup_, SW_HIDE);
        return 0;
    case WM_DPICHANGED: {
        updateWindowDpi(popup_, HIWORD(w));
        auto r = reinterpret_cast<RECT *>(l);
        SetWindowPos(popup_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // Windows can send this synchronously from SetWindowPos while controls are being built.
        queueLayout(popup_);
        return 0;
    }
    case WM_RELAYOUT:
        popupUi_.layoutQueued = false;
        buildPopup();
        positionPopup();
        return 0;
    }
    return DefWindowProcW(popup_, msg, w, l);
}

void App::showSettings(int page) {
    const auto &ui = settingsUi_;
    if (busy_)
        return;
    if (page >= 0)
        page_ = page;
    ShowWindow(popup_, SW_HIDE);
    if (!settings_) {
        RECT r{0, 0, ui.scale(924), ui.scale(442)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE,
                                 WS_EX_CONTROLPARENT, ui.dpi);
        settings_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"InputOutput.Settings", L"InputOutput · Settings",
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                    CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                    instance_, this);
    }
    try {
        refresh();
    } catch (const std::exception &e) {
        notifyError(wide(e.what()));
    }
    relayoutSettings(false);
    ShowWindow(settings_, SW_SHOWNORMAL);
    SetForegroundWindow(settings_);
}
void App::relayoutSettings(bool preserveDraft) {
    if (!settings_)
        return;
    bool hadName = preserveDraft && name_;
    auto draft = hadName ? textOf(name_) : std::wstring{};
    auto focus = GetFocus();
    int focusId = IsChild(settings_, focus) ? GetDlgCtrlID(focus) : 0;
    updateWindowDpi(settings_, GetDpiForWindow(settings_));
    const auto &ui = settingsUi_;
    RECT bounds{0, 0, ui.scale(924), ui.scale(442)};
    AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(settings_, GWL_STYLE)), FALSE,
                             static_cast<DWORD>(GetWindowLongPtrW(settings_, GWL_EXSTYLE)), ui.dpi);
    SetWindowPos(settings_, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    buildSettings();
    if (hadName && name_)
        SetWindowTextW(name_, draft.c_str());
    if (focusId && IsWindowVisible(settings_)) {
        auto target = GetDlgItem(settings_, focusId);
        if (target && IsWindowEnabled(target))
            SetFocus(target);
    }
}
void App::buildSettings() {
    const auto &ui = settingsUi_;
    updating_ = true;
    for (auto c : settingChildren_)
        if (c != navigation_)
            DestroyWindow(c);
    settingChildren_.clear();
    settingsCards_.clear();
    list_ = name_ = details_ = fallback_ = startup_ = calls_ = nullptr;
    auto add = [&](const wchar_t *cls, const std::wstring &text, DWORD style, int x, int y, int width,
                   int height, int id = 0) {
        auto c = control(settings_, cls, text, style, x + 164, y, width, height, id);
        settingChildren_.push_back(c);
        return c;
    };
    if (!navigation_) {
        // A native list box retains keyboard navigation and accessible page names.
        navigation_ =
            control(settings_, L"LISTBOX", L"Settings pages",
                    WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, 12,
                    54, 166, 160, IdNavigation);
        for (auto label : PageNames)
            ListBox_AddString(navigation_, label);
    } else {
        SetWindowPos(navigation_, HWND_TOP, ui.scale(12), ui.scale(54), ui.scale(166), ui.scale(160),
                     SWP_NOACTIVATE);
        SendMessageW(navigation_, WM_SETFONT, reinterpret_cast<WPARAM>(ui.font), TRUE);
    }
    settingChildren_.push_back(navigation_);
    ListBox_SetItemHeight(navigation_, 0, ui.scale(40));
    ListBox_SetCurSel(navigation_, page_);
    InvalidateRect(navigation_, nullptr, TRUE);
    auto caption = control(settings_, L"STATIC", L"Settings", SS_LEFT, 26, 14, 148, 24);
    settingChildren_.push_back(caption);
    auto title = add(L"STATIC", PageNames[page_], SS_LEFT, 34, 4, 438, 36);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(ui.titleFont), TRUE);
    auto card = [&](int x, int y, int width, int height) {
        settingsCards_.push_back(
            {ui.scale(x + 164), ui.scale(y), ui.scale(x + 164 + width), ui.scale(y + height)});
    };
    if (page_ == 3) {
        card(34, 54, 698, 52);
        card(34, 114, 698, 52);
        startup_ = add(L"BUTTON", L"Start when I sign in to Windows", BS_AUTOCHECKBOX | WS_TABSTOP, 50, 66,
                       660, 28, IdStartup);
        Button_SetCheck(startup_, config_.startup ? BST_CHECKED : BST_UNCHECKED);
        if (noStartup_)
            EnableWindow(startup_, FALSE);
        calls_ = add(L"BUTTON", L"Also use selected audio devices for calls", BS_AUTOCHECKBOX | WS_TABSTOP,
                     50, 126, 660, 28, IdCalls);
        Button_SetCheck(calls_, config_.calls ? BST_CHECKED : BST_UNCHECKED);
        add(L"BUTTON", L"Open data folder", BS_PUSHBUTTON | WS_TABSTOP, 34, 186, 165, 32, IdFolder);
        add(WC_LINK, L"<a href=\"https://github.com/RichardBangs/InputOutput\">View on GitHub</a>",
            WS_TABSTOP, 220, 191, 200, 26, IdGithub);
        auto version = add(L"STATIC", L"InputOutput 0.1.0 · Windows 11 · Native C++", SS_LEFT, 34, 234, 650,
                           24, IdVersion);
        SendMessageW(version, WM_SETFONT, reinterpret_cast<WPARAM>(ui.smallFont), TRUE);
        updating_ = false;
        InvalidateRect(settings_, nullptr, TRUE);
        return;
    }
    card(26, 72, 454, 284);
    card(488, 72, 244, 284);
    add(L"BUTTON", page_ == 0 ? L"Windows display settings" : L"Windows sound settings",
        BS_PUSHBUTTON | WS_TABSTOP, 494, 8, 224, 30, IdWindows);
    add(L"STATIC",
        page_ == 0 ? L"Choose which presets appear in the tray" : L"Choose which devices appear in the tray",
        SS_LEFT, 34, 46, 688, 22);
    list_ = add(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 34, 80, 438,
                268, IdList);
    ListView_SetBkColor(list_, theme_.panel());
    ListView_SetTextBkColor(list_, theme_.panel());
    ListView_SetTextColor(list_, theme_.ink());
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t *>(L"Name");
    col.cx = ui.scale(page_ ? 272 : 410);
    ListView_InsertColumn(list_, 0, &col);
    if (page_) {
        col.pszText = const_cast<wchar_t *>(L"Status");
        col.cx = ui.scale(138);
        ListView_InsertColumn(list_, 1, &col);
    }
    add(L"STATIC", L"Menu name", SS_LEFT, 502, 80, 216, 22);
    name_ = add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 502, 104, 178, 32, IdName);
    SendMessageW(name_, EM_SETLIMITTEXT, 60, 0);
    auto saveButton = add(L"BUTTON", L"Save name", BS_PUSHBUTTON | WS_TABSTOP, 686, 104, 32, 32, IdSave);
    auto tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                   WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                                   CW_USEDEFAULT, CW_USEDEFAULT, settings_, nullptr, instance_, nullptr);
    if (tooltip) {
        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = settings_;
        tool.uId = reinterpret_cast<UINT_PTR>(saveButton);
        tool.lpszText = const_cast<wchar_t *>(L"Save name");
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        settingChildren_.push_back(tooltip);
    }
    if (page_ == 0) {
        details_ =
            add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 502, 152, 216, 150);
        add(L"BUTTON", L"Update from current setup", BS_PUSHBUTTON | WS_TABSTOP, 502, 314, 216, 32, IdUpdate);
        add(L"BUTTON", L"Capture current as new", BS_PUSHBUTTON | WS_TABSTOP, 34, 372, 192, 32, IdCapture);
        add(L"BUTTON", L"Delete", BS_PUSHBUTTON | WS_TABSTOP, 238, 372, 76, 32, IdDelete);
        add(L"BUTTON", L"Move up", BS_PUSHBUTTON | WS_TABSTOP, 502, 372, 100, 32, IdUp);
        add(L"BUTTON", L"Move down", BS_PUSHBUTTON | WS_TABSTOP, 614, 372, 104, 32, IdDown);
    } else {
        details_ =
            add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 502, 152, 216, 150);
        add(L"BUTTON", L"Move up", BS_PUSHBUTTON | WS_TABSTOP, 502, 372, 100, 32, IdUp);
        add(L"BUTTON", L"Move down", BS_PUSHBUTTON | WS_TABSTOP, 614, 372, 104, 32, IdDown);
        add(L"STATIC", L"When the selected device disconnects", SS_LEFT, 34, 368, 425, 22);
        fallback_ =
            add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 34, 392, 438, 180, IdFallback);
    }
    populateList();
    updating_ = false;
    selectSettings(selected_);
    InvalidateRect(settings_, nullptr, TRUE);
}
void App::populateList() {
    if (!list_)
        return;
    bool wasUpdating = updating_;
    updating_ = true;
    ListView_DeleteAllItems(list_);
    settingsDevices_.clear();
    auto row = [&](int index, const std::wstring &text, const std::wstring &status, bool shown) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.pszText = const_cast<wchar_t *>(text.c_str());
        ListView_InsertItem(list_, &item);
        if (page_)
            ListView_SetItemText(list_, index, 1, const_cast<wchar_t *>(status.c_str()));
        ListView_SetCheckState(list_, index, shown);
    };
    if (page_ == 0) {
        for (size_t i = 0; i < config_.displays.size(); ++i) {
            auto &p = config_.displays[i];
            row(static_cast<int>(i), p.name, L"", p.shown);
        }
    } else {
        auto devices = page_ == 1 ? outputs_ : inputs_;
        auto &configured = choices();
        for (auto &c : configured) {
            auto found = std::find_if(devices.begin(), devices.end(), [&](auto &d) { return d.id == c.id; });
            AudioDevice d;
            if (found != devices.end()) {
                d = *found;
                devices.erase(found);
            } else {
                d.id = c.id;
                d.name = c.name;
                d.state = DEVICE_STATE_NOTPRESENT;
                d.flow = page_ == 1 ? eRender : eCapture;
            }
            settingsDevices_.push_back(d);
        }
        std::erase_if(devices, [](auto &d) { return d.state == DEVICE_STATE_NOTPRESENT; });
        settingsDevices_.insert(settingsDevices_.end(), devices.begin(), devices.end());
        for (size_t i = 0; i < settingsDevices_.size(); ++i) {
            auto &d = settingsDevices_[i];
            auto c =
                std::find_if(configured.begin(), configured.end(), [&](auto &x) { return x.id == d.id; });
            row(static_cast<int>(i), c == configured.end() ? d.name : c->name, deviceState(d.state),
                c != configured.end() && c->shown);
        }
        updateFallback();
    }
    auto count = ListView_GetItemCount(list_);
    selected_ = count ? std::clamp(selected_, 0, count - 1) : -1;
    if (selected_ >= 0) {
        ListView_SetItemState(list_, selected_, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list_, selected_, FALSE);
    }
    updating_ = wasUpdating;
}
void App::refreshSettingsDevices() {
    if (!settings_ || !IsWindowVisible(settings_) || page_ < 1 || page_ > 2 || !list_ || updating_)
        return;
    std::wstring selectedId, draft = textOf(name_);
    if (selected_ >= 0 && size_t(selected_) < settingsDevices_.size())
        selectedId = settingsDevices_[selected_].id;
    populateList();
    for (size_t i = 0; i < settingsDevices_.size(); ++i)
        if (settingsDevices_[i].id == selectedId) {
            selected_ = static_cast<int>(i);
            break;
        }
    updating_ = true;
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (selected_ >= 0)
        ListView_SetItemState(list_, selected_, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    updating_ = false;
    selectSettings(selected_);
    if (!selectedId.empty() && selected_ >= 0 && size_t(selected_) < settingsDevices_.size() &&
        settingsDevices_[selected_].id == selectedId)
        SetWindowTextW(name_, draft.c_str());
}
void App::selectSettings(int index) {
    selected_ = index;
    bool valid = index >= 0 && index < ListView_GetItemCount(list_);
    for (int id : {IdSave, IdUpdate, IdDelete, IdUp, IdDown, IdName})
        if (auto c = GetDlgItem(settings_, id))
            EnableWindow(c, valid && !readOnly_);
    if (!valid) {
        SetWindowTextW(name_, L"");
        if (details_)
            SetWindowTextW(details_, page_ == 0
                                         ? L"Arrange your screens in Windows, then capture the current setup."
                                         : L"Connect a device to add it to this menu.");
        return;
    }
    if (page_ == 0) {
        auto &p = config_.displays[index];
        SetWindowTextW(name_, p.name.c_str());
        SetWindowTextW(details_, describeDisplays(p.snapshot).c_str());
        EnableWindow(GetDlgItem(settings_, IdUp), index > 0);
        EnableWindow(GetDlgItem(settings_, IdDown), index + 1 < static_cast<int>(config_.displays.size()));
    } else {
        auto &d = settingsDevices_[index];
        auto name = currentName(choices(), d.id);
        SetWindowTextW(name_, (name.empty() ? d.name : name).c_str());
        SetWindowTextW(details_, (d.name + L"\r\n" + deviceState(d.state)).c_str());
        auto &selectedChoices = choices();
        auto found = std::find_if(selectedChoices.begin(), selectedChoices.end(),
                                  [&](auto &c) { return c.id == d.id; });
        bool configured = found != selectedChoices.end();
        EnableWindow(GetDlgItem(settings_, IdUp), configured && found != selectedChoices.begin());
        EnableWindow(GetDlgItem(settings_, IdDown), configured && std::next(found) != selectedChoices.end());
    }
}
void App::updateFallback() {
    if (!fallback_)
        return;
    auto &entries = choices();
    auto &value = page_ == 1 ? config_.outputFallback : config_.inputFallback;
    ComboBox_ResetContent(fallback_);
    ComboBox_AddString(fallback_, L"Let Windows choose");
    ComboBox_SetItemData(fallback_, 0, -1);
    int selection = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].shown)
            continue;
        int at = ComboBox_AddString(fallback_, entries[i].name.c_str());
        ComboBox_SetItemData(fallback_, at, i);
        if (entries[i].id == value)
            selection = at;
    }
    ComboBox_SetCurSel(fallback_, selection);
}
void App::settingsCheck(NMLISTVIEW *change) {
    if (updating_ || page_ > 2 || change->iItem < 0 || !(change->uChanged & LVIF_STATE) ||
        ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK) == 0)
        return;
    bool shown = ListView_GetCheckState(list_, change->iItem) != FALSE;
    Config before = config_;
    if (page_ == 0) {
        if (size_t(change->iItem) >= config_.displays.size())
            return;
        config_.displays[change->iItem].shown = shown;
        if (!save(before)) {
            updating_ = true;
            ListView_SetCheckState(list_, change->iItem, config_.displays[change->iItem].shown);
            updating_ = false;
        }
        return;
    }
    if (size_t(change->iItem) >= settingsDevices_.size())
        return;
    auto &d = settingsDevices_[change->iItem];
    auto &entries = choices();
    auto found = std::find_if(entries.begin(), entries.end(), [&](auto &c) { return c.id == d.id; });
    if (found == entries.end()) {
        if (shown)
            entries.push_back({d.id, d.name, true});
    } else
        found->shown = shown;
    auto &fallback = page_ == 1 ? config_.outputFallback : config_.inputFallback;
    if (!shown && fallback == d.id)
        fallback.clear();
    if (!save(before)) {
        bool previousUpdating = updating_;
        updating_ = true;
        auto previous =
            std::find_if(choices().begin(), choices().end(), [&](auto &c) { return c.id == d.id; });
        ListView_SetCheckState(list_, change->iItem, previous != choices().end() && previous->shown);
        updating_ = previousUpdating;
    }
    updateFallback();
}
void App::openWindowsSettings(const wchar_t *uri) {
    ShellExecuteW(settings_, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
}
void App::settingsCommand(int id) {
    if (busy_ || updating_)
        return;
    if (id == IDCANCEL) {
        DestroyWindow(settings_);
        return;
    }
    if (id == IdWindows) {
        openWindowsSettings(page_ == 0 ? L"ms-settings:display" : L"ms-settings:sound");
        return;
    }
    if (id == IdFolder) {
        ShellExecuteW(settings_, L"open", folder_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    Config before = config_;
    if (id == IdStartup) {
        config_.startup = Button_GetCheck(startup_) == BST_CHECKED;
        try {
            if (!noStartup_)
                setStartup(config_.startup);
        } catch (...) {
            config_ = before;
            Button_SetCheck(startup_, before.startup ? BST_CHECKED : BST_UNCHECKED);
            throw;
        }
        if (!save(before) && !noStartup_)
            setStartup(before.startup);
        return;
    }
    if (id == IdCalls) {
        config_.calls = Button_GetCheck(calls_) == BST_CHECKED;
        save(before);
        return;
    }
    if (id == IdFallback) {
        int sel = ComboBox_GetCurSel(fallback_);
        auto index = ComboBox_GetItemData(fallback_, sel);
        auto &value = page_ == 1 ? config_.outputFallback : config_.inputFallback;
        value = index >= 0 && size_t(index) < choices().size() ? choices()[index].id : L"";
        save(before);
        return;
    }
    if (id == IdCapture) {
        if (config_.displays.size() >= 64)
            throw std::runtime_error("You can save up to 64 display presets.");
        auto s = smoke_ ? current_ : queryDisplays();
        DisplayPreset preset{
            newId(), s.paths.size() == 1 ? L"Single screen" : std::to_wstring(s.paths.size()) + L" screens",
            true, false, s};
        config_.displays.push_back(preset);
        selected_ = static_cast<int>(config_.displays.size() - 1);
        if (save(before)) {
            populateList();
            selectSettings(selected_);
            SetFocus(name_);
            SendMessageW(name_, EM_SETSEL, 0, -1);
        }
        return;
    }
    if (selected_ < 0)
        return;
    if (page_ == 0) {
        if (size_t(selected_) >= config_.displays.size())
            return;
        auto &p = config_.displays[selected_];
        if (id == IdSave) {
            auto name = trim(textOf(name_));
            if (name.empty())
                throw std::runtime_error("Enter a name for this preset.");
            p.name = name;
        } else if (id == IdUpdate) {
            if (MessageBoxW(settings_, L"Replace this preset with your current Windows display setup?",
                            L"Update display preset", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
            p.snapshot = smoke_ ? current_ : queryDisplays();
        } else if (id == IdDelete) {
            if (MessageBoxW(settings_,
                            (L"Delete “" + p.name + L"”? This only removes the saved preset.").c_str(),
                            L"Delete display preset", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
            config_.displays.erase(config_.displays.begin() + selected_);
        } else if (id == IdUp && selected_ > 0) {
            std::swap(config_.displays[selected_], config_.displays[selected_ - 1]);
            --selected_;
        } else if (id == IdDown && size_t(selected_ + 1) < config_.displays.size()) {
            std::swap(config_.displays[selected_], config_.displays[selected_ + 1]);
            ++selected_;
        } else
            return;
    } else {
        if (size_t(selected_) >= settingsDevices_.size())
            return;
        auto device = settingsDevices_[selected_];
        auto &entries = choices();
        auto found = std::find_if(entries.begin(), entries.end(), [&](auto &c) { return c.id == device.id; });
        if (id == IdSave) {
            auto name = trim(textOf(name_));
            if (name.empty())
                throw std::runtime_error("Enter a menu name for this device.");
            if (found == entries.end())
                entries.push_back({device.id, name, false});
            else
                found->name = name;
        } else if (found != entries.end() && (id == IdUp || id == IdDown)) {
            auto index = std::distance(entries.begin(), found);
            auto next = index + (id == IdUp ? -1 : 1);
            if (next < 0 || next >= static_cast<ptrdiff_t>(entries.size()))
                return;
            std::swap(entries[index], entries[next]);
            selected_ = static_cast<int>(next);
        } else
            return;
    }
    if (save(before)) {
        populateList();
        selectSettings(selected_);
    }
}
COLORREF App::settingsBackground(HWND child) const {
    RECT rect{};
    GetWindowRect(child, &rect);
    POINT origin{rect.left, rect.top};
    ScreenToClient(settings_, &origin);
    for (const auto &card : settingsCards_)
        if (PtInRect(&card, origin))
            return theme_.panel();
    return theme_.canvas();
}
LRESULT CALLBACK App::settingsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        app->settings_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(hwnd, msg, w, l);
    try {
        return app->settingsMessage(msg, w, l);
    } catch (const std::exception &e) {
        app->notifyError(wide(e.what()));
        return 0;
    }
}
LRESULT App::settingsMessage(UINT msg, WPARAM w, LPARAM l) {
    const auto &ui = settingsUi_;
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(settings_, &rect);
        fillColor(reinterpret_cast<HDC>(w), rect, theme_.canvas());
        for (const auto &card : settingsCards_)
            roundedBox(reinterpret_cast<HDC>(w), card, theme_.panel(), theme_.cardBorder(), ui.scale(4));
        return TRUE;
    }
    case WM_MEASUREITEM: {
        auto item = reinterpret_cast<MEASUREITEMSTRUCT *>(l);
        if (item->CtlID == IdNavigation) {
            item->itemHeight = ui.scale(40);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto item = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        if (item->CtlID == IdNavigation)
            drawNavigation(*item);
        else if (item->CtlType == ODT_BUTTON)
            drawButton(*item);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IdNavigation && HIWORD(w) == LBN_SELCHANGE) {
            int next = ListBox_GetCurSel(navigation_);
            if (next >= 0 && next < static_cast<int>(std::size(PageNames)) && next != page_) {
                page_ = next;
                selected_ = 0;
                buildSettings();
            }
            return 0;
        }
        if (HIWORD(w) == BN_CLICKED || HIWORD(w) == CBN_SELCHANGE)
            settingsCommand(LOWORD(w));
        return 0;
    case WM_NOTIFY: {
        auto header = reinterpret_cast<NMHDR *>(l);
        if (header->idFrom == IdGithub && (header->code == NM_CLICK || header->code == NM_RETURN)) {
            auto result = ShellExecuteW(settings_, L"open", GithubUrl, nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32)
                notifyError(
                    L"Could not open your browser. Visit https://github.com/RichardBangs/InputOutput");
            return 0;
        }
        if (header->code == NM_CUSTOMDRAW)
            return drawList(header);
        if (header->hwndFrom == list_ && header->code == LVN_ITEMCHANGED) {
            auto change = reinterpret_cast<NMLISTVIEW *>(l);
            if (!updating_) {
                settingsCheck(change);
                if ((change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED))
                    selectSettings(change->iItem);
            }
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(settings_);
        return 0;
    case WM_DESTROY:
        settings_ = nullptr;
        settingsUi_.layoutQueued = false;
        settingChildren_.clear();
        settingsCards_.clear();
        navigation_ = list_ = name_ = details_ = fallback_ = startup_ = calls_ = nullptr;
        return 0;
    case WM_DPICHANGED: {
        updateWindowDpi(settings_, HIWORD(w));
        auto r = reinterpret_cast<RECT *>(l);
        SetWindowPos(settings_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        queueLayout(settings_);
        return 0;
    }
    case WM_RELAYOUT:
        settingsUi_.layoutQueued = false;
        relayoutSettings();
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(w);
        auto child = reinterpret_cast<HWND>(l);
        bool field = msg == WM_CTLCOLOREDIT || (msg == WM_CTLCOLORLISTBOX && child != navigation_);
        COLORREF background = field ? theme_.panel() : settingsBackground(child);
        SetBkColor(dc, background);
        SetTextColor(dc, GetDlgCtrlID(child) == IdVersion ? theme_.muted() : theme_.ink());
        SetDCBrushColor(dc, background);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    }
    return DefWindowProcW(settings_, msg, w, l);
}
void App::uiSmokeStep() {
    static int step = 0;
    // This mode exercises window creation/navigation and persistence in an isolated data folder.
    // It never invokes device switching, startup registration, capture, or microphone recording.
    if (step < 4) {
        showSettings(step);
        if (page_ != 3 && list_ && ListView_GetItemCount(list_))
            selectSettings(0);
        log(folder_, L"UI smoke page " + std::to_wstring(step) + L" OK");
    } else if (step == 4) {
        ShowWindow(settings_, SW_HIDE);
        showPopup();
        log(folder_, L"UI smoke tray panel OK");
    } else if (step >= 5 && step <= 8) {
        // Simulate a busy switch without changing a device. All dismissal paths must remain
        // usable, and the completion redraw must not bring the switcher back.
        busy_ = true;
        showPopup();
        bool ok = IsWindowVisible(popup_) && IsWindowEnabled(GetDlgItem(popup_, IdMinimise));
        if (step == 5)
            SendMessageW(popup_, WM_COMMAND, MAKEWPARAM(IdMinimise, BN_CLICKED), 0);
        else if (step == 6)
            SendMessageW(popup_, WM_ACTIVATE, WA_INACTIVE, 0);
        else if (step == 7)
            SendMessageW(popup_, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        else
            SendMessageW(popup_, WM_CLOSE, 0, 0);
        ok = ok && !IsWindowVisible(popup_);
        busy_ = false;
        buildPopup();
        positionPopup();
        ok = ok && !IsWindowVisible(popup_);
        const wchar_t *actions[] = {L"Minimise", L"click-away", L"Escape", L"close"};
        log(folder_, L"UI smoke busy " + std::wstring(actions[step - 5]) + (ok ? L" OK" : L" FAILED"));
        if (!ok) {
            KillTimer(owner_, 4);
            PostQuitMessage(1);
            return;
        }
    } else if (step == 9) {
        // Exercise mixed window DPIs without changing any Windows display setting.
        ShowWindow(popup_, SW_HIDE);
        showSettings(0);
        ShowWindow(settings_, SW_HIDE);
        auto widthOf = [](HWND window) {
            RECT r{};
            GetWindowRect(window, &r);
            return r.right - r.left;
        };
        auto fontHeight = [](HWND window) {
            LOGFONTW font{};
            GetObjectW(reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)), sizeof(font), &font);
            return font.lfHeight;
        };
        bool ok = true;
        for (auto dpi : {96u, 144u, 192u}) {
            UINT otherDpi = dpi == 192 ? 96 : 192;
            createFonts(settingsUi_, otherDpi);
            buildSettings();
            createFonts(popupUi_, dpi);
            buildPopup(false);
            ok = ok && settingsUi_.dpi == otherDpi && popupUi_.dpi == dpi;
            ok = ok && widthOf(GetDlgItem(settings_, IdName)) == MulDiv(178, otherDpi, 96);
            ok = ok && widthOf(GetDlgItem(popup_, IdMinimise)) == MulDiv(102, dpi, 96);
            ok = ok && fontHeight(GetDlgItem(settings_, IdName)) == -MulDiv(10, otherDpi, 72);
            ok = ok && fontHeight(GetDlgItem(popup_, IdMinimise)) == -MulDiv(10, dpi, 72);
        }
        log(folder_, ok ? L"UI smoke independent 100/150/200 percent window scaling OK"
                        : L"UI smoke independent window scaling FAILED");
        // A hidden popup must discard stale metrics when it next lays out on the real monitor.
        buildPopup();
        positionPopup();
        bool popupOk = popupUi_.dpi == GetDpiForWindow(popup_) && !IsWindowVisible(popup_) &&
                       widthOf(GetDlgItem(popup_, IdMinimise)) == MulDiv(102, popupUi_.dpi, 96);
        log(folder_,
            popupOk ? L"UI smoke hidden popup DPI resync OK" : L"UI smoke hidden popup DPI resync FAILED");
        SetWindowTextW(name_, L"Unsaved DPI test name");
        relayoutSettings();
        RECT client{};
        GetClientRect(settings_, &client);
        bool settingsOk = settingsUi_.dpi == GetDpiForWindow(settings_) &&
                          client.right == MulDiv(924, settingsUi_.dpi, 96) &&
                          client.bottom == MulDiv(442, settingsUi_.dpi, 96) &&
                          textOf(name_) == L"Unsaved DPI test name";
        log(folder_, settingsOk ? L"UI smoke settings DPI resync and draft preservation OK"
                                : L"UI smoke settings DPI resync FAILED");
        if (!ok || !popupOk || !settingsOk) {
            KillTimer(owner_, 4);
            PostQuitMessage(1);
            return;
        }
    } else {
        KillTimer(owner_, 4);
        PostMessageW(owner_, WM_CLOSE, 0, 0);
    }
    ++step;
}
} // namespace io
