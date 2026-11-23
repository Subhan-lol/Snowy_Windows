// Snowy - window tracking implementation.
#include "tracker.h"

#include <dwmapi.h>
#include <shellapi.h>

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

static HWND g_moveSizeHwnd = nullptr;

static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (idObject != OBJID_WINDOW) return;
    if (event == EVENT_SYSTEM_MOVESIZESTART) {
        g_moveSizeHwnd = hwnd;
    } else if (event == EVENT_SYSTEM_MOVESIZEEND) {
        if (g_moveSizeHwnd == hwnd) g_moveSizeHwnd = nullptr;
    }
}

bool Tracker::Init() {
    hook_ = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, WinEventProc, 0, 0,
                            WINEVENT_OUTOFCONTEXT);
    if (!hook_) Logf("SetWinEventHook failed (%lu); drag detection degraded", GetLastError());
    return true;
}

void Tracker::Shutdown() {
    if (hook_) {
        UnhookWinEvent(hook_);
        hook_ = nullptr;
    }
}

HWND Tracker::MoveSizeWindow() const {
    if (g_moveSizeHwnd && !IsWindow(g_moveSizeHwnd)) g_moveSizeHwnd = nullptr;
    return g_moveSizeHwnd;
}

bool IsWindowCloaked(HWND h) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) return cloaked != 0;
    return false;
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM lp) {
    reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
    return TRUE;
}

static bool ClassIs(const wchar_t* cls, const wchar_t* const* list, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (wcscmp(cls, list[i]) == 0) return true;
    return false;
}

uint8_t Tracker::Classify(HWND h, const RECT& wr) {
    if (GetPropW(h, L"SnowyIgnore")) return WK_IGNORE;
    wchar_t cls[96] = {};
    GetClassNameW(h, cls, 95);
    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);

    static const wchar_t* const kIgnore[] = {
        L"Progman", L"WorkerW", L"SnowyOverlayWindow", L"EdgeUiInputTopWndClass", L"EdgeUiInputWndClass",
        L"ApplicationManager_DesktopShellWindow", L"Internet Explorer_Hidden", L"IME", L"MSCTFIME UI",
        L"ThumbnailDeviceHelperWnd", L"PseudoConsoleWindow",
    };
    if (ClassIs(cls, kIgnore, sizeof(kIgnore) / sizeof(kIgnore[0]))) return WK_IGNORE;

    // Click-through overlays (game overlays, screen tools, other desktop toys).
    if ((ex & WS_EX_LAYERED) && (ex & WS_EX_TRANSPARENT)) return WK_IGNORE;
    if (ex & WS_EX_LAYERED) {
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(h, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha < 40)
            return WK_IGNORE;
    }

    static const wchar_t* const kOccluder[] = {
        L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"#32768", L"tooltips_class32", L"NotifyIconOverflowWindow",
        L"TopLevelWindowForOverflowXamlIsland", L"Xaml_WindowedPopupClass", L"TaskListThumbnailWnd",
        L"MultitaskingViewFrame", L"XamlExplorerHostIslandWindow", L"ForegroundStaging",
        L"Shell_InputSwitchTopLevelWindow", L"Windows.UI.Core.CoreWindow", L"SysShadow", L"ComboLBox",
        L"DropDown", L"Net UI Tool Window", L"TaskSwitcherWnd", L"TaskSwitcherOverlayWnd", L"SysDragImage",
        L"Shell_LightDismissOverlay", L"NativeHWNDHost", L"DesktopWindowXamlSource",
    };
    if (ClassIs(cls, kOccluder, sizeof(kOccluder) / sizeof(kOccluder[0]))) return WK_OCCLUDER;

    if (style & WS_CHILD) return WK_IGNORE;

    // Big top-most tool windows that cover a whole monitor are almost certainly overlays.
    if ((ex & WS_EX_TOOLWINDOW) && (ex & WS_EX_TOPMOST)) {
        HMONITOR mon = MonitorFromRect(&wr, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        if (GetMonitorInfoW(mon, &mi)) {
            RECT r = mi.rcMonitor;
            if (wr.left <= r.left && wr.top <= r.top && wr.right >= r.right && wr.bottom >= r.bottom)
                return WK_IGNORE;
        }
    }

    // Caption-less tool windows: popups, flyouts, notifications.
    if ((ex & WS_EX_TOOLWINDOW) && !(style & WS_CAPTION)) return WK_OCCLUDER;
    // Undecorated popups without a frame (menus of custom-drawn apps, splash screens...).
    if ((style & WS_POPUP) && !(style & (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU)) && !(ex & WS_EX_APPWINDOW)) {
        // Large ones (e.g. Electron/Chromium frameless app windows) still get snow.
        if (RectW(wr) < 300 || RectH(wr) < 200) return WK_OCCLUDER;
    }
    return WK_SURFACE;
}

void Tracker::Refresh(HWND h, Info& inf, double now, const RECT& wr) {
    inf.kind = Classify(h, wr);
    inf.cloaked = IsWindowCloaked(h);
    // Stagger re-checks so they don't all land on the same frame.
    inf.nextCheck = now + 0.45 + (double)((((uintptr_t)h) >> 4) % 17) * 0.01;
    inf.init = true;
}

void Tracker::Update(double now, const std::vector<HWND>& own) {
    hwnds_.clear();
    EnumWindows(EnumProc, (LPARAM)&hwnds_);
    snap_.clear();
    int z = 0;
    for (HWND h : hwnds_) {
        if (!IsWindowVisible(h)) continue;
        if (std::find(own.begin(), own.end(), h) != own.end()) continue;
        if (IsIconic(h)) continue;
        RECT wr;
        if (!GetWindowRect(h, &wr)) continue;
        if (wr.right - wr.left <= 1 || wr.bottom - wr.top <= 1) continue;

        Info& inf = cache_[h];
        if (!inf.init || now >= inf.nextCheck) Refresh(h, inf, now, wr);
        inf.lastSeen = now;
        if (inf.cloaked || inf.kind == WK_IGNORE) continue;

        // Visible frame bounds (excludes the invisible resize borders of Windows 10/11).
        bool sizeChanged = RectW(wr) != RectW(inf.lastWr) || RectH(wr) != RectH(inf.lastWr);
        if (!inf.insetValid || sizeChanged || now - inf.insetTime > 3.0) {
            RECT fr;
            if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof(fr))) &&
                fr.right > fr.left && fr.bottom > fr.top) {
                inf.inset.left = std::max<LONG>(0, fr.left - wr.left);
                inf.inset.top = std::max<LONG>(0, fr.top - wr.top);
                inf.inset.right = std::max<LONG>(0, wr.right - fr.right);
                inf.inset.bottom = std::max<LONG>(0, wr.bottom - fr.bottom);
                // Sanity: insets are only a few pixels of invisible border.
                if (inf.inset.left > 40 || inf.inset.top > 40 || inf.inset.right > 40 || inf.inset.bottom > 40)
                    inf.inset = RECT{0, 0, 0, 0};
            } else {
                inf.inset = RECT{0, 0, 0, 0};
            }
            inf.insetValid = true;
            inf.insetTime = now;
        }
        inf.lastWr = wr;

        WinSnapshot s;
        s.hwnd = h;
        s.rc = RECT{wr.left + inf.inset.left, wr.top + inf.inset.top, wr.right - inf.inset.right,
                    wr.bottom - inf.inset.bottom};
        if (s.rc.right <= s.rc.left || s.rc.bottom <= s.rc.top) continue;
        s.z = z++;
        s.kind = inf.kind;
        s.maximized = IsZoomed(h) != 0;
        snap_.push_back(s);
    }

    if (now - lastPurge_ > 5.0) {
        lastPurge_ = now;
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now - it->second.lastSeen > 10.0)
                it = cache_.erase(it);
            else
                ++it;
        }
    }
}

GoneReason Tracker::WhyGone(HWND h) {
    if (!IsWindow(h)) return GoneReason::Closed;
    if (IsIconic(h)) return GoneReason::Minimized;
    if (!IsWindowVisible(h)) return GoneReason::Hidden;
    if (IsWindowCloaked(h)) return GoneReason::Cloaked;
    return GoneReason::Other;
}

bool Tracker::ForegroundCoversMonitor(const RECT& m) {
    HWND fg = GetForegroundWindow();
    if (!fg || GetPropW(fg, L"SnowyIgnore")) return false;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 63);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
        !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        return false;
    RECT r;
    if (!GetWindowRect(fg, &r)) return false;
    if (!(r.left <= m.left && r.top <= m.top && r.right >= m.right && r.bottom >= m.bottom)) return false;
    // A normal maximized window with a caption is not "full screen" (e.g. auto-hide taskbar setups).
    LONG_PTR style = GetWindowLongPtrW(fg, GWL_STYLE);
    if (IsZoomed(fg) && (style & WS_CAPTION) == WS_CAPTION) return false;
    return true;
}

bool Tracker::SystemBusyFullscreen() {
    double now = NowSeconds();
    if (now - lastNotifyCheck_ < 1.0) return notifyBusy_;
    lastNotifyCheck_ = now;
    QUERY_USER_NOTIFICATION_STATE st;
    notifyBusy_ = false;
    if (SUCCEEDED(SHQueryUserNotificationState(&st)))
        notifyBusy_ = (st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE);
    return notifyBusy_;
}
