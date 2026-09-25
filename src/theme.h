#pragma once
#include <windows.h>

namespace io {
struct Theme {
    bool highContrast = false;
    COLORREF canvas() const {
        return highContrast ? GetSysColor(COLOR_WINDOW) : RGB(243, 243, 243);
    }
    COLORREF panel() const {
        return GetSysColor(COLOR_WINDOW);
    }
    COLORREF ink() const {
        return highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(26, 26, 26);
    }
    COLORREF muted() const {
        return highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(96, 96, 96);
    }
    COLORREF accent() const {
        return highContrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(0, 103, 192);
    }
    COLORREF accentPressed() const {
        return highContrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(0, 84, 158);
    }
    COLORREF onAccent() const {
        return highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(255, 255, 255);
    }
    COLORREF tint() const {
        return highContrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(229, 239, 249);
    }
    COLORREF onTint() const {
        return highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ink();
    }
    COLORREF navigation() const {
        return highContrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(228, 228, 228);
    }
    COLORREF border() const {
        return highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(210, 210, 210);
    }
    COLORREF cardBorder() const {
        return highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(229, 229, 229);
    }
    COLORREF disabled() const {
        return highContrast ? GetSysColor(COLOR_GRAYTEXT) : RGB(122, 122, 122);
    }
    COLORREF disabledFill() const {
        return highContrast ? GetSysColor(COLOR_WINDOW) : RGB(245, 245, 245);
    }
    COLORREF danger() const {
        return highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(154, 31, 50);
    }
    void refresh() {
        HIGHCONTRASTW setting{sizeof(setting)};
        highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(setting), &setting, 0) &&
                       (setting.dwFlags & HCF_HIGHCONTRASTON);
    }
};
inline void fillColor(HDC dc, const RECT &rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
inline void roundedBox(HDC dc, const RECT &rect, COLORREF fill, COLORREF border, int radius) {
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, border);
    auto brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    auto pen = SelectObject(dc, GetStockObject(DC_PEN));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, pen);
    SelectObject(dc, brush);
}
} // namespace io
