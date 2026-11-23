// Snowy - snowfall for your Windows desktop.
//
//   Snowy.exe                     run (tray app)
//   Snowy.exe --demo              also open a few demo windows to play with
//   Snowy.exe --selftest <dir>    scripted run with screenshots, then exit
//   Snowy.exe --render-test <dir> offscreen scripted scene -> PNG frames
//   options: --fallback (layered window path), --warp (software GPU), --speed <x>,
//            --verbose, --seconds <s>, --every <n>, --size <w>x<h>, --scale <s>
#include "common.h"

#include <commctrl.h>
#include <shellapi.h>

#include "engine.h"
#include "settings.h"
#include "ui.h"

typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);

static void EnsureDpiAwareness() {
    // The manifest already requests Per-Monitor V2; this is a fallback.
    auto fn = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                "SetProcessDpiAwarenessContext");
    if (fn) fn((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    EnsureDpiAwareness();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    AppOptions opt;
    RenderTestOptions rt;
    bool renderTest = false;
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
        if (a == L"--demo") opt.demo = true;
        else if (a == L"--selftest") {
            opt.selftest = true;
            opt.outDir = next();
        } else if (a == L"--render-test") {
            renderTest = true;
            rt.outDir = next();
        } else if (a == L"--fallback") opt.engine.mode = RenderMode::LayeredWindow;
        else if (a == L"--warp") {
            opt.engine.forceWarp = true;
            rt.forceWarp = true;
        } else if (a == L"--speed") opt.engine.timeScale = (float)_wtof(next().c_str());
        else if (a == L"--verbose") verbose = true;
        else if (a == L"--startup") opt.startup = true;
        else if (a == L"--quit-after") opt.quitAfter = (float)_wtof(next().c_str());
        else if (a == L"--seconds") rt.seconds = (float)_wtof(next().c_str());
        else if (a == L"--every") rt.every = _wtoi(next().c_str());
        else if (a == L"--fps") rt.fps = _wtoi(next().c_str());
        else if (a == L"--scale") rt.scale = (float)_wtof(next().c_str());
        else if (a == L"--size") {
            std::wstring v = next();
            swscanf(v.c_str(), L"%dx%d", &rt.width, &rt.height);
        }
    }
    if (argv) LocalFree(argv);
    if (opt.engine.timeScale <= 0) opt.engine.timeScale = 1.f;
    if (opt.selftest && opt.engine.timeScale == 1.f) opt.engine.timeScale = 12.f;

    LogOpen(verbose);
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    int code = 0;
    if (renderTest) {
        if (rt.outDir.empty()) rt.outDir = L".";
        CreateDirectoryW(rt.outDir.c_str(), nullptr);
        Settings s;  // defaults, independent of the user's saved settings
        code = Engine::RunRenderTest(inst, s, rt);
    } else {
        HANDLE mutex = nullptr;
        if (!opt.selftest && !opt.demo) {
            mutex = CreateMutexW(nullptr, TRUE, L"Local\\SnowyDesktopSnow.SingleInstance");
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                HWND other = FindWindowW(L"SnowyAppWindow", nullptr);
                if (other) PostMessageW(other, App::WM_APP_SHOW_SETTINGS, 0, 0);
                if (mutex) CloseHandle(mutex);
                LogClose();
                return 0;
            }
        }
        if (opt.selftest) {
            if (opt.outDir.empty()) opt.outDir = L"selftest";
            CreateDirectoryW(opt.outDir.c_str(), nullptr);
        }
        App app;
        code = app.Run(inst, opt);
        if (mutex) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
    }
    if (SUCCEEDED(hrCo)) CoUninitialize();
    Logf("Exit code %d", code);
    LogClose();
    return code;
}
