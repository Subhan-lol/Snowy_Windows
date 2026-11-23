// Snowy - application shell: tray icon, menu, settings window, hotkeys.
#pragma once
#include "common.h"
#include "engine.h"
#include "settings.h"

struct AppOptions {
    bool demo = false;            // open a few demo windows to play with
    bool selftest = false;        // scripted run with screenshots, then exit
    std::wstring outDir;          // selftest output folder
    bool startup = false;         // launched at sign-in
    float quitAfter = 0;          // testing: quit after N seconds
    EngineOptions engine;
};

class App;

class SettingsWindow {
public:
    void Show(App* app);
    void Close();
    void Sync();  // refresh controls from settings
    HWND Hwnd() const { return hwnd_; }

private:
    struct Ctl {
        HWND h = nullptr;
        int id = 0;
        int x = 0, y = 0, w = 0, hgt = 0;  // DIP
    };
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Create();
    void Layout();
    HWND Add(const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h);
    void Slider(int id, const wchar_t* label, int x, int y, int lo, int hi);
    void Check(int id, const wchar_t* label, int x, int y, int w = 280);
    void OnSlider(HWND h);
    void OnCommand(int id, int code);

    App* app_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HFONT bold_ = nullptr;
    UINT dpi_ = 96;
    std::vector<Ctl> ctls_;
    bool syncing_ = false;
};

class App {
public:
    int Run(HINSTANCE inst, const AppOptions& opt);
    Settings& Get() { return s_; }
    void Changed();  // settings edited (applies live, saves shortly after)
    void Action(int id);
    HINSTANCE Instance() const { return inst_; }
    std::string EngineStatus() const { return engine_.Status(); }

    static const UINT WM_APP_TRAY = WM_APP + 1;
    static const UINT WM_APP_SHOW_SETTINGS = WM_APP + 2;

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void AddTray();
    void RemoveTray();
    void ShowMenu();
    void Balloon(const wchar_t* title, const wchar_t* text);
    void UpdatePower();
    void SetHidden(bool hidden);
    void CreateDemoWindows();
    void SelftestTick();

    HINSTANCE inst_ = nullptr;
    AppOptions opt_;
    HWND hwnd_ = nullptr;
    Settings s_;
    Engine engine_;
    SettingsWindow settingsWnd_;
    HICON icon_ = nullptr;
    HICON iconSmall_ = nullptr;
    UINT wmTaskbarCreated_ = 0;
    bool trayAdded_ = false;
    bool hidden_ = false;
    HPOWERNOTIFY displayNotify_ = nullptr;

    // demo / selftest
    std::vector<HWND> demo_;
    double selftestStart_ = 0;
    int selftestStep_ = 0;
    RECT shakeBase_{};
    RECT moveBase_{};
};
