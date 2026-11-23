// Snowy - render thread.
#include "engine.h"

#include <mmsystem.h>

#include "capture.h"

extern volatile LONG g_displayChanged;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);

static BOOL CALLBACK MonEnumProc(HMONITOR h, HDC, LPRECT, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<MonitorDesc>*>(lp);
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(h, &mi)) return TRUE;
    MonitorDesc d;
    d.handle = h;
    d.rc = mi.rcMonitor;
    d.work = mi.rcWork;
    d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    static PFN_GetDpiForMonitor getDpi = [] {
        HMODULE m = LoadLibraryW(L"shcore.dll");
        return m ? (PFN_GetDpiForMonitor)GetProcAddress(m, "GetDpiForMonitor") : nullptr;
    }();
    UINT dx = 96, dy = 96;
    if (getDpi && SUCCEEDED(getDpi(h, 0, &dx, &dy)) && dx >= 48) d.scale = (float)dx / 96.f;
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        d.refresh = (float)dm.dmDisplayFrequency;
    v->push_back(d);
    return TRUE;
}

static std::vector<MonitorDesc> EnumerateMonitors() {
    std::vector<MonitorDesc> v;
    EnumDisplayMonitors(nullptr, nullptr, MonEnumProc, (LPARAM)&v);
    if (v.empty()) {
        MonitorDesc d;
        d.rc = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &d.work, 0);
        d.primary = true;
        v.push_back(d);
    }
    return v;
}

static bool SameMonitors(const std::vector<MonitorDesc>& a, const std::vector<MonitorDesc>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (!EqualRect(&a[i].rc, &b[i].rc) || !EqualRect(&a[i].work, &b[i].work) || a[i].scale != b[i].scale ||
            a[i].primary != b[i].primary || a[i].refresh != b[i].refresh)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
bool Engine::Start(HINSTANCE inst, const Settings& s, const EngineOptions& opt) {
    inst_ = inst;
    opt_ = opt;
    s_ = s;
    pending_ = s;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, &threadId_);
    if (!thread_) return false;
    WaitForSingleObject(ready_, 20000);
    return !failed_;
}

void Engine::Stop() {
    if (!thread_) return;
    quit_ = true;
    PostThreadMessageW(threadId_, WM_NULL, 0, 0);
    WaitForSingleObject(thread_, 5000);
    CloseHandle(thread_);
    thread_ = nullptr;
    if (ready_) {
        CloseHandle(ready_);
        ready_ = nullptr;
    }
}

void Engine::Post(EngineCmd c, intptr_t arg, const std::wstring& str) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        cmds_.push_back({c, arg, str});
    }
    if (threadId_) PostThreadMessageW(threadId_, WM_NULL, 0, 0);
}

void Engine::SetSettings(const Settings& s) {
    std::lock_guard<std::mutex> lk(mu_);
    pending_ = s;
    settingsDirty_ = true;
}

void Engine::SetHidden(bool h) { hidden_ = h; }

void Engine::SetOnBattery(bool b) {
    onBattery_ = b;
    std::lock_guard<std::mutex> lk(mu_);
    settingsDirty_ = true;
}

void Engine::SetSessionActive(bool a) { sessionActive_ = a; }

std::string Engine::Status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

DWORD WINAPI Engine::ThreadProc(LPVOID p) {
    reinterpret_cast<Engine*>(p)->Run();
    return 0;
}

void Engine::ApplySettings() {
    float primaryRefresh = 60.f;
    for (auto& m : mons_)
        if (m.primary) primaryRefresh = m.refresh;
    targetFps_ = s_.fpsCap > 0 ? (float)s_.fpsCap : primaryRefresh;
    float density = 1.f;
    if (s_.batterySaver && onBattery_) {
        targetFps_ = std::min(targetFps_, 30.f);
        density = 0.6f;
    }
    targetFps_ = Clamp(targetFps_, 15.f, 240.f);
    syncInterval_ = std::max(1, std::min(4, (int)std::lround(primaryRefresh / targetFps_)));
    world_.SetSettings(s_);
    world_.SetDensityMul(density);
    renderer_.SetCaptureExcluded(s_.hideFromCapture);
}

void Engine::RefreshMonitors(bool force) {
    lastMonCheck_ = NowSeconds();
    std::vector<MonitorDesc> mons = EnumerateMonitors();
    if (!force && SameMonitors(mons, mons_)) return;
    Logf("Monitors changed: %u", (unsigned)mons.size());
    for (auto& m : mons)
        Logf("  [%ld,%ld %ldx%ld] work bottom %ld scale %.2f %.0fHz%s", m.rc.left, m.rc.top, RectW(m.rc), RectH(m.rc),
             m.work.bottom, m.scale, m.refresh, m.primary ? " primary" : "");
    mons_ = mons;
    if (!renderer_.SetMonitors(mons_)) Logf("Renderer failed to create overlays");
    world_.SetMonitors(mons_);
    show_.assign(mons_.size(), 1);
    ApplySettings();
}

void Engine::ProcessCommands() {
    std::vector<Cmd> cmds;
    bool dirty = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cmds.swap(cmds_);
        if (settingsDirty_) {
            s_ = pending_;
            settingsDirty_ = false;
            dirty = true;
        }
    }
    if (dirty) ApplySettings();
    for (auto& c : cmds) {
        lastActivity_ = NowSeconds();
        switch (c.type) {
        case EngineCmd::Melt:
            world_.Melt();
            break;
        case EngineCmd::Avalanche:
            world_.Avalanche();
            break;
        case EngineCmd::BuildSnowman:
            world_.BuildSnowmanNow();
            break;
        case EngineCmd::Shovel:
            world_.Shovel();
            break;
        case EngineCmd::MoveOverride:
            world_.ForceMoveOverride((HWND)c.arg);
            break;
        case EngineCmd::CaptureScreen: {
            std::vector<uint8_t> px;
            int w = 0, h = 0;
            RECT rc;
            if (CaptureScreen(px, w, h, rc))
                Logf("Screen capture %s: %s", Narrow(c.str).c_str(), SavePng(c.str, w, h, px.data()) ? "ok" : "failed");
            break;
        }
        case EngineCmd::CaptureOverlay: {
            for (size_t i = 0; i < mons_.size(); i++)
                if (mons_[i].primary) renderer_.RequestCapture(i);
            pendingOverlayCapture_ = c.str;
            break;
        }
        }
    }
}

void Engine::AppendFps(DrawLists& L) {
    const MonitorDesc* pm = nullptr;
    for (auto& m : mons_)
        if (m.primary) pm = &m;
    if (!pm) return;
    float sc = pm->scale;
    int v = (int)std::lround(fpsValue_);
    std::string txt = std::to_string(v) + " " + std::to_string((int)world_.FlakeCount());
    static const uint8_t masks[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    float w = 7.f * sc, h = 12.f * sc;
    float x = (float)pm->rc.left + 14.f * sc, y = (float)pm->rc.top + 12.f * sc;
    float col[4] = {1.f, 0.85f, 0.3f, 0.95f};
    float shadow[4] = {0.f, 0.f, 0.f, 0.55f};
    for (char ch : txt) {
        if (ch >= '0' && ch <= '9') {
            uint8_t mk = masks[ch - '0'];
            Vec2 seg[7][2] = {{{0, 0}, {w, 0}},         {{w, 0}, {w, h / 2}},     {{w, h / 2}, {w, h}},
                              {{0, h}, {w, h}},         {{0, h / 2}, {0, h}},     {{0, 0}, {0, h / 2}},
                              {{0, h / 2}, {w, h / 2}}};
            for (int pass = 0; pass < 2; pass++) {
                for (int k = 0; k < 7; k++) {
                    if (!(mk & (1 << k))) continue;
                    Vec2 off = pass == 0 ? Vec2(1.f * sc, 1.f * sc) : Vec2(0, 0);
                    ShapeInst si = {};
                    si.ax = x + seg[k][0].x + off.x;
                    si.ay = y + seg[k][0].y + off.y;
                    si.bx = x + seg[k][1].x + off.x;
                    si.by = y + seg[k][1].y + off.y;
                    si.ra = si.rb = 1.1f * sc;
                    const float* c = pass == 0 ? shadow : col;
                    si.r = c[0];
                    si.g = c[1];
                    si.b = c[2];
                    si.a = c[3];
                    si.style = (float)SS_FLAT;
                    L.shapes.push_back(si);
                }
            }
        }
        x += w + 5.f * sc;
    }
}

void Engine::Frame() {
    double now = NowSeconds();
    float dt = lastFrame_ == 0 ? 1.f / 60.f : (float)Clamp((float)(now - lastFrame_), 0.f, 0.05f);
    lastFrame_ = now;
    ProcessCommands();
    if (InterlockedExchange(&g_displayChanged, 0) != 0 || now - lastMonCheck_ > 2.0) RefreshMonitors(false);

    if (now - lastFsCheck_ > 0.5 || show_.size() != mons_.size()) {
        lastFsCheck_ = now;
        show_.assign(mons_.size(), 1);
        if (s_.pauseFullscreen) {
            bool busy = tracker_.SystemBusyFullscreen();
            for (size_t i = 0; i < mons_.size(); i++)
                if (busy || tracker_.ForegroundCoversMonitor(mons_[i].rc)) show_[i] = 0;
        }
    }
    std::vector<char> show = show_;
    if (hidden_ || !sessionActive_) std::fill(show.begin(), show.end(), (char)0);
    bool anyShown = false;
    for (size_t i = 0; i < show.size(); i++) {
        world_.SetMonitorActive(i, show[i] != 0);
        anyShown = anyShown || show[i];
    }
    if (!anyShown) {
        if (wasVisible_) {
            lists_.Clear();
            std::vector<char> none(show.size(), 0);
            renderer_.Render(lists_, show, none, 1);
            wasVisible_ = false;
        }
        return;
    }
    wasVisible_ = true;
    if (now - lastTopmost_ > 1.5) {
        lastTopmost_ = now;
        renderer_.ReassertTopmost();
    }

    tracker_.Update(now, renderer_.OverlayWindows());
    const auto& snap = tracker_.Snapshot();
    bool changed = snap.size() != prevSnap_.size();
    for (size_t i = 0; !changed && i < snap.size(); i++)
        changed = snap[i].hwnd != prevSnap_[i].hwnd || !EqualRect(&snap[i].rc, &prevSnap_[i].rc);
    if (changed) {
        lastActivity_ = now;
        prevSnap_ = snap;
    }
    world_.SyncWindows(snap, tracker_.MoveSizeWindow(), dt, [this](HWND h) { return tracker_.WhyGone(h); });

    InputState in;
    POINT pt;
    if (GetCursorPos(&pt)) {
        in.cursor = Vec2((float)pt.x, (float)pt.y);
        in.cursorValid = true;
    }
    world_.Update(dt, in);
    world_.BuildDrawLists(lists_);
    if (s_.showFps) AppendFps(lists_);

    if (!renderer_.Render(lists_, show, world_.MonitorContent(), syncInterval_)) {
        if (!renderer_.Recreate()) {
            Logf("GPU device could not be recreated; retrying shortly");
            Sleep(500);
        }
    }
    if (!pendingOverlayCapture_.empty()) {
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (renderer_.TakeCapture(px, w, h)) {
            std::vector<FakeWindow> fw;
            const MonitorDesc* pm = nullptr;
            for (auto& m : mons_)
                if (m.primary) pm = &m;
            for (auto& s : snap) {
                if (s.kind != WK_SURFACE || !pm) continue;
                FakeWindow f;
                f.rc = RECT{s.rc.left - pm->rc.left, s.rc.top - pm->rc.top, s.rc.right - pm->rc.left,
                            s.rc.bottom - pm->rc.top};
                wchar_t title[128] = {};
                GetWindowTextW(s.hwnd, title, 127);
                f.title = title;
                f.style = (int)(fw.size() % 3);
                fw.push_back(f);
            }
            std::vector<uint8_t> bg;
            int tb = pm ? std::max(0L, pm->rc.bottom - pm->work.bottom) : 0;
            DrawFakeDesktop(w, h, tb, fw, bg);
            CompositeOver(bg, px);
            Logf("Overlay capture %s: %s", Narrow(pendingOverlayCapture_).c_str(),
                 SavePng(pendingOverlayCapture_, w, h, bg.data()) ? "ok" : "failed");
            pendingOverlayCapture_.clear();
        }
    }

    fpsFrames_++;
    if (now - fpsTimer_ >= 1.0) {
        fpsValue_ = (float)(fpsFrames_ / (now - fpsTimer_));
        fpsFrames_ = 0;
        fpsTimer_ = now;
    }
}

void Engine::Run() {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = renderer_.Init(inst_, opt_.mode, opt_.forceWarp);
    if (ok) {
        tracker_.Init();
        world_.SetSettings(s_);
        world_.SetTimeScale(opt_.timeScale);
        RefreshMonitors(true);
        ok = renderer_.TargetCountOk(mons_.size());
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = std::string(renderer_.Mode() == RenderMode::DirectComposition ? "DirectComposition" : "Layered window") +
                  " on " + renderer_.AdapterName();
    }
    if (!ok) failed_ = true;
    SetEvent(ready_);
    if (!ok) {
        renderer_.Shutdown();
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return;
    }

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    bool hiRes = timer != nullptr;
    if (!timer) {
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        timeBeginPeriod(1);
    }
    double nextDue = NowSeconds();
    fpsTimer_ = nextDue;
    while (!quit_) {
        double now = NowSeconds();
        bool visible = wasVisible_ && !hidden_ && sessionActive_;
        bool lively = world_.AnythingAnimating() || (now - lastActivity_ < 2.0);
        double interval = !visible ? 0.25 : (lively ? 1.0 / targetFps_ : 0.1);
        HANDLE pacing = (visible && lively) ? renderer_.PacingWaitable() : nullptr;
        HANDLE h = pacing;
        DWORD timeout = 250;
        if (!h) {
            if (nextDue < now - interval) nextDue = now;
            LARGE_INTEGER due;
            due.QuadPart = -std::max<LONGLONG>(1, (LONGLONG)((nextDue - now) * 1e7));
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            h = timer;
            timeout = 1000;
        }
        DWORD r = MsgWaitForMultipleObjectsEx(1, &h, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit_ = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit_) break;
        if (r == WAIT_OBJECT_0 || r == WAIT_TIMEOUT) {
            Frame();
            nextDue += interval;
        }
    }
    Logf("Engine loop exited");
    CancelWaitableTimer(timer);
    CloseHandle(timer);
    if (!hiRes) timeEndPeriod(1);
    tracker_.Shutdown();
    renderer_.Shutdown();
    Logf("Renderer shut down");
    if (SUCCEEDED(hrCo)) CoUninitialize();
}

// ---------------------------------------------------------------------------
// Offscreen render test: a scripted scene rendered to PNG frames.
// ---------------------------------------------------------------------------
int Engine::RunRenderTest(HINSTANCE inst, const Settings& s0, const RenderTestOptions& o) {
    Renderer r;
    if (!r.Init(inst, RenderMode::Offscreen, o.forceWarp)) return 2;
    float sc = o.scale;
    int taskbarH = (int)(48 * sc);
    MonitorDesc m;
    m.handle = (HMONITOR)(uintptr_t)1;
    m.rc = RECT{0, 0, o.width, o.height};
    m.work = RECT{0, 0, o.width, o.height - taskbarH};
    m.scale = sc;
    m.primary = true;
    std::vector<MonitorDesc> mons{m};
    if (!r.SetMonitors(mons)) return 3;

    Settings s = s0;
    World w;
    w.SetSeed(20241224);
    w.SetSettings(s);
    w.SetMonitors(mons);
    w.SetTimeScale(14.f);

    struct SceneWin {
        HWND h;
        RECT base;
        std::wstring title;
        int style;
        bool alive;
    };
    auto R = [&](int x0, int y0, int x1, int y1) {
        return RECT{(LONG)(x0 * sc), (LONG)(y0 * sc), (LONG)(x1 * sc), (LONG)(y1 * sc)};
    };
    // front to back
    std::vector<SceneWin> wins = {
        {(HWND)(uintptr_t)0x101, R(420, 130, 1040, 420), L"Projects", 1, true},
        {(HWND)(uintptr_t)0x102, R(640, 450, 1130, 700), L"Trip Ideas.txt - Notepad", 0, true},
        {(HWND)(uintptr_t)0x103, R(70, 480, 700, 800), L"Snow Day.jpg - Photos", 2, true},
    };
    float dt = 1.f / (float)o.fps;
    int total = (int)(o.seconds * (float)o.fps);
    DrawLists L;
    std::vector<char> show{1}, content{1};
    std::vector<uint8_t> px, bg;
    double simMs = 0, renderMs = 0;
    int saved = 0;
    bool snowmanStarted = false;
    for (int f = 0; f < total; f++) {
        float t = (float)f * dt;
        HWND moving = nullptr;
        std::vector<WinSnapshot> snap;
        std::vector<FakeWindow> fake;
        for (size_t i = 0; i < wins.size(); i++) {
            SceneWin& sw = wins[i];
            if (sw.h == (HWND)(uintptr_t)0x103 && t >= 16.f) sw.alive = false;  // close "Snow Day"
            if (!sw.alive) continue;
            RECT rc = sw.base;
            if (sw.h == (HWND)(uintptr_t)0x101 && t >= 11.f && t < 12.3f) {
                // shake "Projects"
                float u = t - 11.f;
                float env = std::sin(kPi * u / 1.3f);
                int dx = (int)(90.f * sc * env * std::sin(kTwoPi * 4.2f * u));
                OffsetRect(&rc, dx, (int)(12.f * sc * env * std::sin(kTwoPi * 2.1f * u)));
                moving = sw.h;
            }
            if (sw.h == (HWND)(uintptr_t)0x102 && t >= 13.5f) {
                float u = Saturate((t - 13.5f) / 0.9f);
                OffsetRect(&rc, (int)(170.f * sc * EaseInOut(u)), (int)(-20.f * sc * EaseInOut(u)));
                if (u < 1.f) moving = sw.h;
            }
            WinSnapshot ws;
            ws.hwnd = sw.h;
            ws.rc = rc;
            ws.z = (int)snap.size();
            ws.kind = WK_SURFACE;
            snap.push_back(ws);
            fake.push_back({rc, sw.title, sw.style});
        }
        // taskbar as an occluder
        WinSnapshot tb;
        tb.hwnd = (HWND)(uintptr_t)0x1FF;
        tb.rc = RECT{0, o.height - taskbarH, o.width, o.height};
        tb.z = (int)snap.size();
        tb.kind = WK_OCCLUDER;
        snap.insert(snap.begin(), tb);
        for (size_t i = 0; i < snap.size(); i++) snap[i].z = (int)i;

        double t0 = NowSeconds();
        w.SyncWindows(snap, moving, dt, [](HWND) { return GoneReason::Closed; });
        if (!snowmanStarted && t >= 17.f) {
            w.BuildSnowmanNow();
            snowmanStarted = true;
        }
        InputState in;
        if (t >= 25.f && t < 25.7f) {
            float u = (t - 25.f) / 0.7f;
            in.cursor = Vec2(Lerp(80.f, 1500.f, u) * sc, (float)(o.height - taskbarH) - 10.f * sc);
            in.cursorValid = true;
        } else {
            in.cursor = Vec2(1450.f * sc, 300.f * sc);
            in.cursorValid = true;
        }
        w.Update(dt, in);
        w.BuildDrawLists(L);
        double t1 = NowSeconds();
        bool save = (f % std::max(1, o.every)) == 0;
        if (save) r.RequestCapture(0);
        r.Render(L, show, content, 1);
        double t2 = NowSeconds();
        simMs += (t1 - t0) * 1000.0;
        renderMs += (t2 - t1) * 1000.0;
        if (save) {
            int cw = 0, ch = 0;
            if (r.TakeCapture(px, cw, ch)) {
                DrawFakeDesktop(cw, ch, taskbarH, fake, bg);
                CompositeOver(bg, px);
                wchar_t name[64];
                swprintf(name, 64, L"\\frame_%05d.png", saved++);
                SavePng(o.outDir + name, cw, ch, bg.data());
            }
        }
        if (f % 60 == 0)
            Logf("render-test t=%.1fs flakes=%u sim=%.2fms render=%.2fms", t, (unsigned)w.FlakeCount(),
                 simMs / (f + 1), renderMs / (f + 1));
    }
    Logf("render-test done: %d frames, avg sim %.3f ms, avg render+readback %.3f ms", total, simMs / total,
         renderMs / total);
    r.Shutdown();
    return 0;
}
