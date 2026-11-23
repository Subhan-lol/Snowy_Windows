// Snowy - the render thread: owns the overlays, the window tracker and the
// simulation, paces frames and reacts to power / full-screen / display changes.
#pragma once
#include "common.h"
#include "renderer.h"
#include "settings.h"
#include "tracker.h"
#include "world.h"

#include <atomic>
#include <mutex>

enum class EngineCmd {
    Melt,
    Avalanche,
    BuildSnowman,
    Shovel,
    CaptureScreen,    // str = output png path (real screen)
    CaptureOverlay,   // str = output png path (overlay over a fake desktop)
    MoveOverride,     // arg = HWND treated as being dragged (tests)
};

struct EngineOptions {
    RenderMode mode = RenderMode::DirectComposition;
    bool forceWarp = false;
    float timeScale = 1.f;
};

struct RenderTestOptions {
    std::wstring outDir;
    int width = 1600, height = 900;
    float scale = 1.f;
    int fps = 30;
    float seconds = 30.f;
    int every = 1;          // save every Nth frame
    bool forceWarp = false;
};

class Engine {
public:
    bool Start(HINSTANCE inst, const Settings& s, const EngineOptions& opt);
    void Stop();
    void Post(EngineCmd c, intptr_t arg = 0, const std::wstring& str = std::wstring());
    void SetSettings(const Settings& s);
    void SetHidden(bool hidden);
    void SetOnBattery(bool onBattery);
    void SetSessionActive(bool active);
    bool Failed() const { return failed_; }
    std::string Status() const;

    // Offscreen deterministic rendering of a scripted scene to PNG frames.
    static int RunRenderTest(HINSTANCE inst, const Settings& s, const RenderTestOptions& opt);

private:
    static DWORD WINAPI ThreadProc(LPVOID p);
    void Run();
    void Frame();
    void ProcessCommands();
    void RefreshMonitors(bool force);
    void ApplySettings();
    void AppendFps(DrawLists& L);

    struct Cmd {
        EngineCmd type;
        intptr_t arg;
        std::wstring str;
    };

    HINSTANCE inst_ = nullptr;
    EngineOptions opt_;
    HANDLE thread_ = nullptr;
    DWORD threadId_ = 0;
    HANDLE ready_ = nullptr;
    std::atomic<bool> quit_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> hidden_{false};
    std::atomic<bool> onBattery_{false};
    std::atomic<bool> sessionActive_{true};

    mutable std::mutex mu_;
    std::vector<Cmd> cmds_;
    Settings pending_;
    bool settingsDirty_ = false;
    std::string status_;

    // render-thread state
    Settings s_;
    Renderer renderer_;
    Tracker tracker_;
    World world_;
    DrawLists lists_;
    std::vector<MonitorDesc> mons_;
    std::vector<char> show_;
    double lastFrame_ = 0;
    double lastMonCheck_ = 0;
    double lastTopmost_ = 0;
    double lastFsCheck_ = 0;
    double fpsTimer_ = 0;
    int fpsFrames_ = 0;
    float fpsValue_ = 0;
    int syncInterval_ = 1;
    float targetFps_ = 60;
    bool wasVisible_ = true;
    std::vector<WinSnapshot> prevSnap_;
    std::wstring pendingOverlayCapture_;
    double lastActivity_ = 0;
};
