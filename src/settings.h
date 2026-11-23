// Snowy - user settings (persisted to %APPDATA%\Snowy\Snowy.ini).
#pragma once
#include "common.h"

struct Settings {
    // Snowfall
    bool snowing = true;      // spawn new flakes
    int intensity = 45;       // 0..100
    int flakeSize = 50;       // 0..100
    int fallSpeed = 50;       // 0..100
    int wind = 0;             // -100..100 (negative = blowing left)
    int gusts = 35;           // 0..100
    int flakeStyle = 0;       // 0 = mixed, 1 = soft dots, 2 = crystals

    // Accumulation
    bool snowOnWindows = true;
    bool snowOnTaskbar = true;
    bool icicles = true;
    int accumulation = 50;    // 0..100 (speed)
    int windowDepth = 50;     // 0..100 (max cap height on windows)
    int groundDepth = 40;     // 0..100 (max height on the taskbar)

    // Fun
    bool snowman = true;
    int snowmanFreq = 1;      // 0 = rarely, 1 = sometimes, 2 = often
    bool cursorPlay = true;   // cursor swirls flakes and plows snow

    // Display / performance
    bool flakeOutline = true; // faint shadow so snow is visible on white windows
    bool pauseFullscreen = true;
    bool batterySaver = true;
    int fpsCap = 60;          // 30, 60 or 0 (= display refresh rate)
    bool hideFromCapture = false;
    bool showFps = false;

    bool firstRun = true;

    void Load();
    void Save() const;
    void ResetToDefaults();

    static bool GetAutostart();
    static bool SetAutostart(bool enable);

    // Derived quantities (DIP units).
    float MaxCapDip() const { return 8.f + windowDepth * 0.30f; }      // 8..38
    float MaxGroundDip() const { return groundDepth * 1.6f; }          // 0..160
    float AccumulationMul() const {
        float a = accumulation / 50.f;                                 // 0..2
        return 0.15f + a * a * 0.85f + a * 0.25f;                      // ~0.15..4.05
    }
    float DensityPerMDip() const {                                     // flakes per million DIP^2
        float i = (float)intensity;
        return 8.f + 2.0f * i + 0.035f * i * i;
    }
    float SizeMul() const { return 0.6f + flakeSize / 100.f * 1.0f; }  // 0.6..1.6
    float SpeedMul() const { return 0.45f + fallSpeed / 100.f * 1.15f; } // 0.45..1.6
    float WindDip() const { return wind * 1.3f; }                       // -130..130 DIP/s
    float GustDip() const { return gusts * 1.0f; }                      // 0..100 DIP/s
};
