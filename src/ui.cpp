// Snowy - tray icon, menu, hotkeys, power handling and the demo/selftest driver.
#include "ui.h"

#include <commctrl.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include "../res/resource.h"
#include "capture.h"

enum MenuId {
    ID_SNOWING = 101,
    ID_INT_LIGHT = 110,
    ID_INT_MODERATE,
    ID_INT_HEAVY,
    ID_INT_BLIZZARD,
    ID_WIND_CALM = 120,
    ID_WIND_RIGHT,
    ID_WIND_LEFT,
    ID_WIND_GUSTY,
    ID_WIND_STORM,
    ID_ON_WINDOWS = 130,
    ID_ICICLES,
    ID_ON_TASKBAR,
    ID_SNOWMAN,
    ID_CURSOR,
    ID_BUILD_SNOWMAN = 140,
    ID_AVALANCHE,
    ID_SHOVEL,
    ID_MELT,
    ID_HIDE = 150,
    ID_SETTINGS,
    ID_AUTOSTART,
    ID_ABOUT,
    ID_QUIT,
};

enum HotkeyId { HK_HIDE = 1, HK_AVALANCHE = 2, HK_SNOWMAN = 3, HK_MELT = 4 };

static const UINT_PTR kSaveTimer = 1;
static const UINT_PTR kPowerTimer = 2;
static const UINT_PTR kSelftestTimer = 3;
static const UINT_PTR kQuitTimer = 4;
static const wchar_t* kAppClass = L"SnowyAppWindow";
static const wchar_t* kDemoClass = L"SnowyDemoWindow";

static App* g_app = nullptr;

// ---------------------------------------------------------------------------
// Demo windows
// ---------------------------------------------------------------------------
static LRESULT CALLBACK DemoProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintDemoWindow(h, dc, (int)(intptr_t)GetPropW(h, L"SnowyDemoStyle"));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(h, nullptr, FALSE);
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

void App::CreateDemoWindows() {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = DemoProc;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = icon_;
    wc.lpszClassName = kDemoClass;
    RegisterClassExW(&wc);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int W = RectW(wa), H = RectH(wa);
    struct D {
        const wchar_t* title;
        float x0, y0, x1, y1;
        int style;
    };
    // Created back to front.
    D defs[3] = {{L"Snow Day.jpg", 0.04f, 0.52f, 0.44f, 0.90f, 2},
                 {L"Trip Ideas.txt", 0.40f, 0.48f, 0.72f, 0.76f, 0},
                 {L"Projects", 0.26f, 0.14f, 0.66f, 0.46f, 1}};
    for (auto& d : defs) {
        int x = wa.left + (int)(d.x0 * W), y = wa.top + (int)(d.y0 * H);
        HWND h = CreateWindowExW(0, kDemoClass, d.title, WS_OVERLAPPEDWINDOW, x, y, (int)((d.x1 - d.x0) * W),
                                 (int)((d.y1 - d.y0) * H), nullptr, nullptr, inst_, nullptr);
        SetPropW(h, L"SnowyDemoStyle", (HANDLE)(intptr_t)d.style);
        ShowWindow(h, SW_SHOWNORMAL);
        UpdateWindow(h);
        demo_.push_back(h);
    }
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
int App::Run(HINSTANCE inst, const AppOptions& opt) {
    inst_ = inst;
    opt_ = opt;
    g_app = this;
    s_.Load();
    if (opt.selftest || opt.demo) s_.firstRun = false;

    icon_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_SNOWY), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                              GetSystemMetrics(SM_CYICON), 0);
    iconSmall_ = nullptr;
    if (FAILED(LoadIconMetric(inst, MAKEINTRESOURCEW(IDI_SNOWY), LIM_SMALL, &iconSmall_)) || !iconSmall_)
        iconSmall_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_SNOWY), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON), 0);
    if (!icon_) icon_ = LoadIcon(nullptr, IDI_APPLICATION);
    if (!iconSmall_) iconSmall_ = icon_;

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hIcon = icon_;
    wc.hIconSm = iconSmall_;
    wc.lpszClassName = kAppClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kAppClass, L"Snowy", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst,
                            nullptr);
    if (!hwnd_) return 1;
    wmTaskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (opt.demo || opt.selftest) CreateDemoWindows();

    if (!engine_.Start(inst, s_, opt.engine)) {
        Logf("Engine failed to start: %s", engine_.Status().c_str());
        if (opt.selftest) {
            engine_.Stop();
            return 2;
        }
        MessageBoxW(nullptr,
                    L"Snowy couldn't start its graphics engine.\n\nMake sure your graphics drivers are up to date. "
                    L"Details are in %LOCALAPPDATA%\\Snowy\\snowy.log",
                    L"Snowy", MB_ICONERROR);
        engine_.Stop();
        return 2;
    }
    Logf("Engine: %s", engine_.Status().c_str());
    UpdatePower();

    AddTray();
    RegisterHotKey(hwnd_, HK_HIDE, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'S');
    RegisterHotKey(hwnd_, HK_AVALANCHE, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'A');
    RegisterHotKey(hwnd_, HK_SNOWMAN, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'M');
    RegisterHotKey(hwnd_, HK_MELT, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'D');
    WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
    static const GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
    displayNotify_ = RegisterPowerSettingNotification(hwnd_, &kConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    SetTimer(hwnd_, kPowerTimer, 15000, nullptr);

    if (s_.firstRun) {
        Balloon(L"Let it snow!",
                L"Snowy is running. Right-click the snowflake in the tray (it may be under the ^ arrow) for options. "
                L"Shake a window to knock the snow off!");
        s_.firstRun = false;
        s_.Save();
    }
    if (opt.selftest) {
        selftestStart_ = NowSeconds();
        SetTimer(hwnd_, kSelftestTimer, 15, nullptr);
    }
    if (opt.quitAfter > 0) SetTimer(hwnd_, kQuitTimer, (UINT)(opt.quitAfter * 1000.f), nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (settingsWnd_.Hwnd() && IsDialogMessageW(settingsWnd_.Hwnd(), &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Logf("Shutting down");
    if (displayNotify_) UnregisterPowerSettingNotification(displayNotify_);
    WTSUnRegisterSessionNotification(hwnd_);
    for (int i = HK_HIDE; i <= HK_MELT; i++) UnregisterHotKey(hwnd_, i);
    RemoveTray();
    settingsWnd_.Close();
    Logf("Stopping engine");
    engine_.Stop();
    Logf("Engine stopped");
    s_.Save();
    for (HWND h : demo_)
        if (IsWindow(h)) DestroyWindow(h);
    DestroyWindow(hwnd_);
    return (int)msg.wParam;
}

void App::Changed() {
    engine_.SetSettings(s_);
    SetTimer(hwnd_, kSaveTimer, 700, nullptr);
}

void App::SetHidden(bool hidden) {
    hidden_ = hidden;
    engine_.SetHidden(hidden);
}

void App::UpdatePower() {
    SYSTEM_POWER_STATUS ps;
    bool battery = false;
    if (GetSystemPowerStatus(&ps)) battery = ps.ACLineStatus == 0 || ps.SystemStatusFlag == 1;
    engine_.SetOnBattery(battery);
}

void App::AddTray() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = iconSmall_;
    wcscpy_s(nid.szTip, L"Snowy \x2744 let it snow");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != 0;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void App::RemoveTray() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayAdded_ = false;
}

void App::Balloon(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    nid.hBalloonIcon = icon_;
    wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid.szInfo, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void AddItem(HMENU m, UINT id, const wchar_t* text, bool checked = false, bool radio = false,
                    bool enabled = true) {
    MENUITEMINFOW mi = {sizeof(mi)};
    mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
    mi.fType = MFT_STRING | (radio ? MFT_RADIOCHECK : 0);
    mi.fState = (checked ? MFS_CHECKED : 0) | (enabled ? 0 : MFS_DISABLED);
    mi.wID = id;
    mi.dwTypeData = (LPWSTR)text;
    InsertMenuItemW(m, GetMenuItemCount(m), TRUE, &mi);
}

void App::ShowMenu() {
    HMENU m = CreatePopupMenu();
    AddItem(m, 0, L"Snowy \x2744", false, false, false);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AddItem(m, ID_SNOWING, L"Let it snow", s_.snowing);

    HMENU amt = CreatePopupMenu();
    int presetsI[4] = {20, 45, 75, 100};
    int best = 0;
    for (int i = 1; i < 4; i++)
        if (std::abs(s_.intensity - presetsI[i]) < std::abs(s_.intensity - presetsI[best])) best = i;
    AddItem(amt, ID_INT_LIGHT, L"Light flurries", best == 0, true);
    AddItem(amt, ID_INT_MODERATE, L"Steady snowfall", best == 1, true);
    AddItem(amt, ID_INT_HEAVY, L"Heavy snow", best == 2, true);
    AddItem(amt, ID_INT_BLIZZARD, L"Blizzard!", best == 3, true);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)amt, L"Snowfall");

    HMENU wind = CreatePopupMenu();
    int wsel = -1;
    if (s_.wind == 0 && s_.gusts <= 40) wsel = 0;
    else if (s_.wind > 0 && s_.wind < 70) wsel = 1;
    else if (s_.wind < 0 && s_.wind > -70) wsel = 2;
    else if (s_.wind == 0) wsel = 3;
    else wsel = 4;
    AddItem(wind, ID_WIND_CALM, L"Calm", wsel == 0, true);
    AddItem(wind, ID_WIND_RIGHT, L"Breeze \x2192", wsel == 1, true);
    AddItem(wind, ID_WIND_LEFT, L"Breeze \x2190", wsel == 2, true);
    AddItem(wind, ID_WIND_GUSTY, L"Gusty", wsel == 3, true);
    AddItem(wind, ID_WIND_STORM, L"Snowstorm", wsel == 4, true);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)wind, L"Wind");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    AddItem(m, ID_ON_WINDOWS, L"Snow piles up on windows", s_.snowOnWindows);
    AddItem(m, ID_ICICLES, L"Icicles", s_.icicles);
    AddItem(m, ID_ON_TASKBAR, L"Snow piles up on the taskbar", s_.snowOnTaskbar);
    AddItem(m, ID_SNOWMAN, L"Snowmen now and then", s_.snowman);
    AddItem(m, ID_CURSOR, L"Cursor plays with the snow", s_.cursorPlay);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AddItem(m, ID_BUILD_SNOWMAN, L"Build a snowman now\tCtrl+Alt+Shift+M");
    AddItem(m, ID_AVALANCHE, L"Avalanche! (shake it all off)\tCtrl+Alt+Shift+A");
    AddItem(m, ID_SHOVEL, L"Shovel the taskbar");
    AddItem(m, ID_MELT, L"Melt everything\tCtrl+Alt+Shift+D");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AddItem(m, ID_HIDE, L"Hide snow\tCtrl+Alt+Shift+S", hidden_);
    AddItem(m, ID_SETTINGS, L"Settings...");
    AddItem(m, ID_AUTOSTART, L"Start with Windows", Settings::GetAutostart());
    AddItem(m, ID_ABOUT, L"About Snowy");
    AddItem(m, ID_QUIT, L"Quit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    UINT flags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
    flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    int cmd = TrackPopupMenuEx(m, flags, pt.x, pt.y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) Action(cmd);
}

void App::Action(int id) {
    switch (id) {
    case ID_SNOWING:
        s_.snowing = !s_.snowing;
        Changed();
        break;
    case ID_INT_LIGHT:
        s_.intensity = 20;
        Changed();
        break;
    case ID_INT_MODERATE:
        s_.intensity = 45;
        Changed();
        break;
    case ID_INT_HEAVY:
        s_.intensity = 75;
        Changed();
        break;
    case ID_INT_BLIZZARD:
        s_.intensity = 100;
        s_.wind = s_.wind == 0 ? 55 : s_.wind;
        s_.gusts = 85;
        s_.snowing = true;
        Changed();
        break;
    case ID_WIND_CALM:
        s_.wind = 0;
        s_.gusts = 20;
        Changed();
        break;
    case ID_WIND_RIGHT:
        s_.wind = 35;
        s_.gusts = 35;
        Changed();
        break;
    case ID_WIND_LEFT:
        s_.wind = -35;
        s_.gusts = 35;
        Changed();
        break;
    case ID_WIND_GUSTY:
        s_.wind = 0;
        s_.gusts = 90;
        Changed();
        break;
    case ID_WIND_STORM:
        s_.wind = 85;
        s_.gusts = 100;
        Changed();
        break;
    case ID_ON_WINDOWS:
        s_.snowOnWindows = !s_.snowOnWindows;
        Changed();
        break;
    case ID_ICICLES:
        s_.icicles = !s_.icicles;
        Changed();
        break;
    case ID_ON_TASKBAR:
        s_.snowOnTaskbar = !s_.snowOnTaskbar;
        Changed();
        break;
    case ID_SNOWMAN:
        s_.snowman = !s_.snowman;
        Changed();
        break;
    case ID_CURSOR:
        s_.cursorPlay = !s_.cursorPlay;
        Changed();
        break;
    case ID_BUILD_SNOWMAN:
        if (hidden_) SetHidden(false);
        engine_.Post(EngineCmd::BuildSnowman);
        break;
    case ID_AVALANCHE:
        engine_.Post(EngineCmd::Avalanche);
        break;
    case ID_SHOVEL:
        engine_.Post(EngineCmd::Shovel);
        break;
    case ID_MELT:
        engine_.Post(EngineCmd::Melt);
        break;
    case ID_HIDE:
        SetHidden(!hidden_);
        break;
    case ID_SETTINGS:
        settingsWnd_.Show(this);
        break;
    case ID_AUTOSTART:
        Settings::SetAutostart(!Settings::GetAutostart());
        settingsWnd_.Sync();
        break;
    case ID_ABOUT: {
        std::wstring msg = L"Snowy " SNOWY_VERSION_W L"\nSnowfall for your Windows desktop.\n\n"
                           L"\x2022 Snow piles up on your windows and taskbar\n"
                           L"\x2022 Shake a window and the snow slides off\n"
                           L"\x2022 Move a window and its icicles snap off\n"
                           L"\x2022 Close or minimize one and it all comes down\n"
                           L"\x2022 Now and then, a snowman appears on the taskbar\n"
                           L"\x2022 Flick the cursor through the snow to plow it\n\n"
                           L"Hotkeys (Ctrl+Alt+Shift +):\n"
                           L"   S  hide / show snow\n   A  avalanche\n   M  build a snowman\n   D  melt everything\n\n"
                           L"Renderer: " +
                           Widen(engine_.Status());
        MessageBoxW(nullptr, msg.c_str(), L"About Snowy", MB_ICONINFORMATION | MB_SETFOREGROUND);
        break;
    }
    case ID_QUIT:
        PostQuitMessage(0);
        break;
    }
    settingsWnd_.Sync();
}

void App::SelftestTick() {
    double t = NowSeconds() - selftestStart_;
    auto out = [&](const wchar_t* name) { return opt_.outDir + L"\\" + name; };
    HWND A = demo_.size() > 2 ? demo_[2] : nullptr;  // Projects
    HWND B = demo_.size() > 0 ? demo_[0] : nullptr;  // Snow Day
    HWND C = demo_.size() > 1 ? demo_[1] : nullptr;  // Trip Ideas
    switch (selftestStep_) {
    case 0:
        if (t > 7.0) {
            engine_.Post(EngineCmd::CaptureOverlay, 0, out(L"overlay_1_accumulated.png"));
            engine_.Post(EngineCmd::CaptureScreen, 0, out(L"screen_1_accumulated.png"));
            if (A) GetWindowRect(A, &shakeBase_);
            engine_.Post(EngineCmd::MoveOverride, (intptr_t)A);
            selftestStep_++;
        }
        break;
    case 1:  // shake "Projects"
        if (t < 9.4) {
            if (t > 8.0 && A) {
                float u = (float)(t - 8.0);
                float env = std::sin(kPi * u / 1.4f);
                int dx = (int)(85.f * env * std::sin(kTwoPi * 4.0f * u));
                SetWindowPos(A, nullptr, shakeBase_.left + dx, shakeBase_.top, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        } else {
            if (A)
                SetWindowPos(A, nullptr, shakeBase_.left, shakeBase_.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            selftestStep_++;
        }
        break;
    case 2:
        if (t > 9.55) {
            engine_.Post(EngineCmd::CaptureOverlay, 0, out(L"overlay_2_shaken.png"));
            engine_.Post(EngineCmd::CaptureScreen, 0, out(L"screen_2_shaken.png"));
            if (C) GetWindowRect(C, &moveBase_);
            engine_.Post(EngineCmd::MoveOverride, (intptr_t)C);
            selftestStep_++;
        }
        break;
    case 3:  // move "Trip Ideas"
        if (t < 11.3) {
            if (t > 10.5 && C) {
                float u = Saturate((float)(t - 10.5) / 0.8f);
                SetWindowPos(C, nullptr, moveBase_.left + (int)(150.f * EaseInOut(u)), moveBase_.top, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        } else {
            selftestStep_++;
        }
        break;
    case 4:
        if (t > 11.45) {
            engine_.Post(EngineCmd::CaptureOverlay, 0, out(L"overlay_3_moved.png"));
            selftestStep_++;
        }
        break;
    case 5:
        if (t > 12.5) {
            if (B) DestroyWindow(B);
            selftestStep_++;
        }
        break;
    case 6:
        if (t > 12.85) {
            engine_.Post(EngineCmd::CaptureOverlay, 0, out(L"overlay_4_closed.png"));
            engine_.Post(EngineCmd::CaptureScreen, 0, out(L"screen_4_closed.png"));
            selftestStep_++;
        }
        break;
    case 7:
        if (t > 13.5) {
            engine_.Post(EngineCmd::BuildSnowman);
            selftestStep_++;
        }
        break;
    case 8:
        if (t > 25.0) {
            engine_.Post(EngineCmd::CaptureOverlay, 0, out(L"overlay_5_snowman.png"));
            engine_.Post(EngineCmd::CaptureScreen, 0, out(L"screen_5_snowman.png"));
            selftestStep_++;
        }
        break;
    case 9:
        if (t > 26.5) {
            Logf("Selftest finished: %s", engine_.Status().c_str());
            KillTimer(hwnd_, kSelftestTimer);
            PostQuitMessage(0);
            selftestStep_++;
        }
        break;
    }
}

LRESULT CALLBACK App::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_app) {
        if (!g_app->hwnd_) g_app->hwnd_ = h;
        if (g_app->hwnd_ == h) return g_app->Handle(m, w, l);
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT App::Handle(UINT m, WPARAM w, LPARAM l) {
    if (m == wmTaskbarCreated_ && wmTaskbarCreated_) {
        trayAdded_ = false;
        AddTray();
        return 0;
    }
    switch (m) {
    case WM_APP_TRAY:
        switch (LOWORD(l)) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            ShowMenu();
            break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP:
            settingsWnd_.Show(this);
            break;
        }
        return 0;
    case WM_APP_SHOW_SETTINGS:
        settingsWnd_.Show(this);
        return 0;
    case WM_HOTKEY:
        switch ((int)w) {
        case HK_HIDE:
            SetHidden(!hidden_);
            break;
        case HK_AVALANCHE:
            Action(ID_AVALANCHE);
            break;
        case HK_SNOWMAN:
            Action(ID_BUILD_SNOWMAN);
            break;
        case HK_MELT:
            Action(ID_MELT);
            break;
        }
        return 0;
    case WM_TIMER:
        if (w == kSaveTimer) {
            KillTimer(hwnd_, kSaveTimer);
            s_.Save();
        } else if (w == kPowerTimer) {
            UpdatePower();
        } else if (w == kSelftestTimer) {
            SelftestTick();
        } else if (w == kQuitTimer) {
            PostQuitMessage(0);
        }
        return 0;
    case WM_POWERBROADCAST:
        if (w == PBT_APMPOWERSTATUSCHANGE) UpdatePower();
        if (w == PBT_POWERSETTINGCHANGE && l) {
            auto* ps = (POWERBROADCAST_SETTING*)l;
            if (ps->DataLength >= sizeof(DWORD)) {
                DWORD state = *(DWORD*)ps->Data;  // 0 off, 1 on, 2 dimmed
                engine_.SetSessionActive(state != 0);
            }
        }
        return TRUE;
    case WM_WTSSESSION_CHANGE:
        if (w == WTS_SESSION_LOCK || w == WTS_CONSOLE_DISCONNECT) engine_.SetSessionActive(false);
        if (w == WTS_SESSION_UNLOCK || w == WTS_CONSOLE_CONNECT) engine_.SetSessionActive(true);
        return 0;
    case WM_ENDSESSION:
        if (w) s_.Save();
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}
