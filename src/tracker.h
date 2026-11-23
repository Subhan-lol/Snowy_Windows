// Snowy - tracks top-level windows (z-order, visible bounds, drags).
#pragma once
#include "common.h"

#include <unordered_map>

enum WinKind : uint8_t {
    WK_IGNORE = 0,
    WK_OCCLUDER = 1,  // hides snow behind it, but snow never settles on it (menus, taskbar...)
    WK_SURFACE = 2,   // normal application window: snow piles up on top
};

struct WinSnapshot {
    HWND hwnd = nullptr;
    RECT rc{};          // visible bounds in physical pixels
    int z = 0;          // 0 = top of the z-order
    uint8_t kind = WK_SURFACE;
    bool maximized = false;
};

enum class GoneReason { Closed, Minimized, Hidden, Cloaked, Other };

class Tracker {
public:
    bool Init();
    void Shutdown();
    // Enumerate windows. 'own' windows (overlays) are skipped.
    void Update(double now, const std::vector<HWND>& own);
    const std::vector<WinSnapshot>& Snapshot() const { return snap_; }
    HWND MoveSizeWindow() const;
    GoneReason WhyGone(HWND h);
    // True if the foreground window covers the whole monitor rect (videos, games).
    bool ForegroundCoversMonitor(const RECT& monitorRc);
    // True if Windows reports a full-screen D3D app or presentation mode.
    bool SystemBusyFullscreen();

private:
    struct Info {
        bool init = false;
        double nextCheck = 0;
        double lastSeen = 0;
        uint8_t kind = WK_SURFACE;
        bool cloaked = false;
        RECT lastWr{};
        RECT inset{};
        bool insetValid = false;
        double insetTime = 0;
    };
    void Refresh(HWND h, Info& inf, double now, const RECT& wr);
    static uint8_t Classify(HWND h, const RECT& wr);

    std::vector<HWND> hwnds_;
    std::vector<WinSnapshot> snap_;
    std::unordered_map<HWND, Info> cache_;
    HWINEVENTHOOK hook_ = nullptr;
    double lastPurge_ = 0;
    double lastNotifyCheck_ = 0;
    bool notifyBusy_ = false;
};

bool IsWindowCloaked(HWND h);
