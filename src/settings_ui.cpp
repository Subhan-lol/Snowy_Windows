// Snowy - the settings window (plain Win32 controls, per-monitor DPI aware).
#include "ui.h"

#include <commctrl.h>

#include "../res/resource.h"

enum CtlId {
    CID_SNOWING = 1001,
    CID_INTENSITY,
    CID_SIZE,
    CID_SPEED,
    CID_WIND,
    CID_GUSTS,
    CID_STYLE,
    CID_SNOWMAN,
    CID_SNOWMAN_FREQ,
    CID_CURSOR,
    CID_BTN_SNOWMAN,
    CID_BTN_AVALANCHE,
    CID_ON_WINDOWS,
    CID_ICICLES,
    CID_ON_TASKBAR,
    CID_ACCUM,
    CID_WIN_DEPTH,
    CID_GROUND_DEPTH,
    CID_BTN_SHOVEL,
    CID_BTN_MELT,
    CID_FULLSCREEN,
    CID_BATTERY,
    CID_FPS,
    CID_OUTLINE,
    CID_HIDECAP,
    CID_AUTOSTART,
    CID_SHOWFPS,
    CID_DEFAULTS,
    CID_CLOSE,
};

// Menu command ids shared with ui.cpp
enum { ACT_BUILD_SNOWMAN = 140, ACT_AVALANCHE = 141, ACT_SHOVEL = 142, ACT_MELT = 143 };

static const wchar_t* kSettingsClass = L"SnowySettingsWindow";

typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
typedef BOOL(WINAPI* PFN_AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef BOOL(WINAPI* PFN_SystemParametersInfoForDpi)(UINT, UINT, PVOID, UINT, UINT);
typedef HRESULT(WINAPI* PFN_GetDpiForMonitor2)(HMONITOR, int, UINT*, UINT*);

static UINT MonitorDpi(HMONITOR mon) {
    static PFN_GetDpiForMonitor2 fn = [] {
        HMODULE m = LoadLibraryW(L"shcore.dll");
        return m ? (PFN_GetDpiForMonitor2)GetProcAddress(m, "GetDpiForMonitor") : nullptr;
    }();
    UINT dx = 96, dy = 96;
    if (fn && SUCCEEDED(fn(mon, 0, &dx, &dy))) return dx;
    return 96;
}

static HFONT MakeFont(UINT dpi, bool bold) {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    auto spi = (PFN_SystemParametersInfoForDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                              "SystemParametersInfoForDpi");
    LOGFONTW lf = {};
    if (spi && spi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) {
        lf = ncm.lfMessageFont;
    } else {
        lf.lfHeight = -MulDiv(9, (int)dpi, 72);
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        lf.lfQuality = CLEARTYPE_QUALITY;
    }
    if (bold) lf.lfWeight = FW_SEMIBOLD;
    return CreateFontIndirectW(&lf);
}

void SettingsWindow::Show(App* app) {
    app_ = app;
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        Sync();
        return;
    }
    Create();
}

void SettingsWindow::Close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

HWND SettingsWindow::Add(const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd_,
                             (HMENU)(intptr_t)id, app_->Instance(), nullptr);
    Ctl ctl;
    ctl.h = c;
    ctl.id = id;
    ctl.x = x;
    ctl.y = y;
    ctl.w = w;
    ctl.hgt = h;
    ctls_.push_back(ctl);
    return c;
}

void SettingsWindow::Slider(int id, const wchar_t* label, int x, int y, int lo, int hi) {
    Add(L"STATIC", label, SS_LEFT, 0, x, y + 5, 104, 20);
    HWND tb = Add(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, id, x + 104, y, 186, 30);
    SendMessageW(tb, TBM_SETRANGE, FALSE, MAKELPARAM(lo, hi));
    SendMessageW(tb, TBM_SETPAGESIZE, 0, 10);
}

void SettingsWindow::Check(int id, const wchar_t* label, int x, int y, int w) {
    Add(L"BUTTON", label, WS_TABSTOP | BS_AUTOCHECKBOX, id, x, y, w, 24);
}

void SettingsWindow::Create() {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = Proc;
    wc.hInstance = app_->Instance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(app_->Instance(), MAKEINTRESOURCEW(IDI_SNOWY));
    wc.lpszClassName = kSettingsClass;
    RegisterClassExW(&wc);

    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    dpi_ = MonitorDpi(mon);
    float s = dpi_ / 96.f;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, (LONG)(680 * s), (LONG)(588 * s)};
    auto adj = (PFN_AdjustWindowRectExForDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
    if (adj)
        adj(&rc, style, FALSE, 0, dpi_);
    else
        AdjustWindowRectEx(&rc, style, FALSE, 0);
    int w = RectW(rc), h = RectH(rc);
    int x = mi.rcWork.left + (RectW(mi.rcWork) - w) / 2;
    int y = mi.rcWork.top + (RectH(mi.rcWork) - h) / 2;
    hwnd_ = CreateWindowExW(0, kSettingsClass, L"Snowy Settings", style, x, y, w, h, nullptr, nullptr,
                            app_->Instance(), this);
    if (!hwnd_) return;

    ctls_.clear();
    // Left column
    Add(L"BUTTON", L"Snowfall", BS_GROUPBOX, 0, 16, 10, 318, 272);
    Check(CID_SNOWING, L"Let it snow", 32, 34);
    Slider(CID_INTENSITY, L"Amount", 32, 64, 0, 100);
    Slider(CID_SIZE, L"Flake size", 32, 98, 0, 100);
    Slider(CID_SPEED, L"Fall speed", 32, 132, 0, 100);
    Slider(CID_WIND, L"Wind  \x2190 \x2192", 32, 166, -100, 100);
    Slider(CID_GUSTS, L"Gusts", 32, 200, 0, 100);
    Add(L"STATIC", L"Flake style", SS_LEFT, 0, 32, 243, 104, 20);
    HWND styleBox = Add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, CID_STYLE, 136, 238, 180, 200);
    SendMessageW(styleBox, CB_ADDSTRING, 0, (LPARAM)L"Mixed");
    SendMessageW(styleBox, CB_ADDSTRING, 0, (LPARAM)L"Soft dots");
    SendMessageW(styleBox, CB_ADDSTRING, 0, (LPARAM)L"Crystals");

    Add(L"BUTTON", L"Fun", BS_GROUPBOX, 0, 16, 292, 318, 150);
    Check(CID_SNOWMAN, L"Snowmen", 32, 318, 100);
    HWND freq = Add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, CID_SNOWMAN_FREQ, 136, 316, 180, 200);
    SendMessageW(freq, CB_ADDSTRING, 0, (LPARAM)L"Rarely");
    SendMessageW(freq, CB_ADDSTRING, 0, (LPARAM)L"Now and then");
    SendMessageW(freq, CB_ADDSTRING, 0, (LPARAM)L"Often");
    Check(CID_CURSOR, L"Cursor plays with the snow", 32, 350);
    Add(L"BUTTON", L"Build a snowman", WS_TABSTOP | BS_PUSHBUTTON, CID_BTN_SNOWMAN, 32, 390, 136, 32);
    Add(L"BUTTON", L"Avalanche!", WS_TABSTOP | BS_PUSHBUTTON, CID_BTN_AVALANCHE, 180, 390, 136, 32);

    // Right column
    Add(L"BUTTON", L"Snow piles", BS_GROUPBOX, 0, 346, 10, 318, 272);
    Check(CID_ON_WINDOWS, L"Piles up on windows", 362, 34);
    Check(CID_ICICLES, L"Icicles under windows", 362, 60);
    Check(CID_ON_TASKBAR, L"Piles up on the taskbar", 362, 86);
    Slider(CID_ACCUM, L"Pile-up speed", 362, 116, 0, 100);
    Slider(CID_WIN_DEPTH, L"Depth on windows", 362, 150, 0, 100);
    Slider(CID_GROUND_DEPTH, L"Depth on taskbar", 362, 184, 0, 100);
    Add(L"BUTTON", L"Shovel the taskbar", WS_TABSTOP | BS_PUSHBUTTON, CID_BTN_SHOVEL, 362, 232, 140, 32);
    Add(L"BUTTON", L"Melt everything", WS_TABSTOP | BS_PUSHBUTTON, CID_BTN_MELT, 510, 232, 140, 32);

    Add(L"BUTTON", L"Display && performance", BS_GROUPBOX, 0, 346, 292, 318, 236);
    Check(CID_FULLSCREEN, L"Pause during full-screen apps && games", 362, 316);
    Check(CID_BATTERY, L"Battery saver (lighter when unplugged)", 362, 342);
    Add(L"STATIC", L"Frame rate", SS_LEFT, 0, 362, 375, 100, 20);
    HWND fps = Add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, CID_FPS, 466, 370, 184, 200);
    SendMessageW(fps, CB_ADDSTRING, 0, (LPARAM)L"30 fps (lightest)");
    SendMessageW(fps, CB_ADDSTRING, 0, (LPARAM)L"60 fps");
    SendMessageW(fps, CB_ADDSTRING, 0, (LPARAM)L"Match display");
    Check(CID_OUTLINE, L"Shadowed flakes (visible on white)", 362, 404);
    Check(CID_HIDECAP, L"Hide snow from screen sharing", 362, 430);
    Check(CID_AUTOSTART, L"Start with Windows", 362, 456);
    Check(CID_SHOWFPS, L"Show FPS counter", 362, 482);

    Add(L"STATIC",
        L"Tip: shake a window to knock the snow off, flick the cursor through "
        L"a snow pile to plow it.\nHotkeys Ctrl+Alt+Shift + S hide, A avalanche, M snowman, D melt.",
        SS_LEFT, 0, 18, 454, 318, 76);
    Add(L"BUTTON", L"Reset to defaults", WS_TABSTOP | BS_PUSHBUTTON, CID_DEFAULTS, 16, 542, 150, 32);
    Add(L"BUTTON", L"Close", WS_TABSTOP | BS_DEFPUSHBUTTON, CID_CLOSE, 544, 542, 120, 32);

    auto gdw = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (gdw) dpi_ = gdw(hwnd_);
    Layout();
    Sync();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::Layout() {
    if (font_) DeleteObject(font_);
    if (bold_) DeleteObject(bold_);
    font_ = MakeFont(dpi_, false);
    bold_ = MakeFont(dpi_, true);
    float s = dpi_ / 96.f;
    for (auto& c : ctls_) {
        SetWindowPos(c.h, nullptr, (int)(c.x * s), (int)(c.y * s), (int)(c.w * s), (int)(c.hgt * s),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        wchar_t cls[32] = {};
        GetClassNameW(c.h, cls, 31);
        bool group = (GetWindowLongPtrW(c.h, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX && !wcscmp(cls, L"Button");
        SendMessageW(c.h, WM_SETFONT, (WPARAM)(group ? bold_ : font_), TRUE);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::Sync() {
    if (!hwnd_ || !app_) return;
    syncing_ = true;
    const Settings& s = app_->Get();
    auto check = [&](int id, bool v) { CheckDlgButton(hwnd_, id, v ? BST_CHECKED : BST_UNCHECKED); };
    auto slide = [&](int id, int v) { SendDlgItemMessageW(hwnd_, id, TBM_SETPOS, TRUE, v); };
    check(CID_SNOWING, s.snowing);
    slide(CID_INTENSITY, s.intensity);
    slide(CID_SIZE, s.flakeSize);
    slide(CID_SPEED, s.fallSpeed);
    slide(CID_WIND, s.wind);
    slide(CID_GUSTS, s.gusts);
    SendDlgItemMessageW(hwnd_, CID_STYLE, CB_SETCURSEL, s.flakeStyle, 0);
    check(CID_SNOWMAN, s.snowman);
    SendDlgItemMessageW(hwnd_, CID_SNOWMAN_FREQ, CB_SETCURSEL, s.snowmanFreq, 0);
    check(CID_CURSOR, s.cursorPlay);
    check(CID_ON_WINDOWS, s.snowOnWindows);
    check(CID_ICICLES, s.icicles);
    check(CID_ON_TASKBAR, s.snowOnTaskbar);
    slide(CID_ACCUM, s.accumulation);
    slide(CID_WIN_DEPTH, s.windowDepth);
    slide(CID_GROUND_DEPTH, s.groundDepth);
    check(CID_FULLSCREEN, s.pauseFullscreen);
    check(CID_BATTERY, s.batterySaver);
    SendDlgItemMessageW(hwnd_, CID_FPS, CB_SETCURSEL, s.fpsCap == 30 ? 0 : (s.fpsCap == 60 ? 1 : 2), 0);
    check(CID_OUTLINE, s.flakeOutline);
    check(CID_HIDECAP, s.hideFromCapture);
    check(CID_AUTOSTART, Settings::GetAutostart());
    check(CID_SHOWFPS, s.showFps);
    syncing_ = false;
}

void SettingsWindow::OnSlider(HWND h) {
    if (syncing_ || !app_) return;
    int id = GetDlgCtrlID(h);
    int v = (int)SendMessageW(h, TBM_GETPOS, 0, 0);
    Settings& s = app_->Get();
    switch (id) {
    case CID_INTENSITY: s.intensity = v; break;
    case CID_SIZE: s.flakeSize = v; break;
    case CID_SPEED: s.fallSpeed = v; break;
    case CID_WIND: s.wind = v; break;
    case CID_GUSTS: s.gusts = v; break;
    case CID_ACCUM: s.accumulation = v; break;
    case CID_WIN_DEPTH: s.windowDepth = v; break;
    case CID_GROUND_DEPTH: s.groundDepth = v; break;
    default: return;
    }
    app_->Changed();
}

void SettingsWindow::OnCommand(int id, int code) {
    if (syncing_ || !app_) return;
    Settings& s = app_->Get();
    bool checked = IsDlgButtonChecked(hwnd_, id) == BST_CHECKED;
    switch (id) {
    case CID_SNOWING: s.snowing = checked; break;
    case CID_SNOWMAN: s.snowman = checked; break;
    case CID_CURSOR: s.cursorPlay = checked; break;
    case CID_ON_WINDOWS: s.snowOnWindows = checked; break;
    case CID_ICICLES: s.icicles = checked; break;
    case CID_ON_TASKBAR: s.snowOnTaskbar = checked; break;
    case CID_FULLSCREEN: s.pauseFullscreen = checked; break;
    case CID_BATTERY: s.batterySaver = checked; break;
    case CID_OUTLINE: s.flakeOutline = checked; break;
    case CID_HIDECAP: s.hideFromCapture = checked; break;
    case CID_SHOWFPS: s.showFps = checked; break;
    case CID_AUTOSTART:
        Settings::SetAutostart(checked);
        return;
    case CID_STYLE:
        if (code != CBN_SELCHANGE) return;
        s.flakeStyle = (int)SendDlgItemMessageW(hwnd_, CID_STYLE, CB_GETCURSEL, 0, 0);
        break;
    case CID_SNOWMAN_FREQ:
        if (code != CBN_SELCHANGE) return;
        s.snowmanFreq = (int)SendDlgItemMessageW(hwnd_, CID_SNOWMAN_FREQ, CB_GETCURSEL, 0, 0);
        break;
    case CID_FPS: {
        if (code != CBN_SELCHANGE) return;
        int sel = (int)SendDlgItemMessageW(hwnd_, CID_FPS, CB_GETCURSEL, 0, 0);
        s.fpsCap = sel == 0 ? 30 : (sel == 1 ? 60 : 0);
        break;
    }
    case CID_BTN_SNOWMAN: app_->Action(ACT_BUILD_SNOWMAN); return;
    case CID_BTN_AVALANCHE: app_->Action(ACT_AVALANCHE); return;
    case CID_BTN_SHOVEL: app_->Action(ACT_SHOVEL); return;
    case CID_BTN_MELT: app_->Action(ACT_MELT); return;
    case CID_DEFAULTS:
        s.ResetToDefaults();
        Sync();
        break;
    case CID_CLOSE:
    case IDCANCEL:
        DestroyWindow(hwnd_);
        return;
    default:
        return;
    }
    app_->Changed();
}

LRESULT CALLBACK SettingsWindow::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    SettingsWindow* self = nullptr;
    if (m == WM_NCCREATE) {
        self = (SettingsWindow*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = h;
    } else {
        self = (SettingsWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (m == WM_NCDESTROY) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return DefWindowProcW(h, m, w, l);
    }
    if (self && self->hwnd_ == h) return self->Handle(m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT SettingsWindow::Handle(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_HSCROLL:
        if (l) OnSlider((HWND)l);
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(w), HIWORD(w));
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)w;
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(w);
        RECT* r = (RECT*)l;
        SetWindowPos(hwnd_, nullptr, r->left, r->top, RectW(*r), RectH(*r), SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        ctls_.clear();
        if (font_) DeleteObject(font_);
        if (bold_) DeleteObject(bold_);
        font_ = bold_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}
