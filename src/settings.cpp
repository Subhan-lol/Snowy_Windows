// Snowy - settings persistence and autostart.
#include "settings.h"

static std::wstring IniPath() { return AppDataDir() + L"\\Snowy.ini"; }

static int ReadInt(const wchar_t* key, int def, int lo, int hi) {
    int v = (int)GetPrivateProfileIntW(L"Snowy", key, def, IniPath().c_str());
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}
static bool ReadBool(const wchar_t* key, bool def) { return ReadInt(key, def ? 1 : 0, 0, 1) != 0; }

static void WriteInt(const wchar_t* key, int v) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", v);
    WritePrivateProfileStringW(L"Snowy", key, buf, IniPath().c_str());
}

void Settings::Load() {
    Settings d;
    snowing = ReadBool(L"Snowing", d.snowing);
    intensity = ReadInt(L"Intensity", d.intensity, 0, 100);
    flakeSize = ReadInt(L"FlakeSize", d.flakeSize, 0, 100);
    fallSpeed = ReadInt(L"FallSpeed", d.fallSpeed, 0, 100);
    wind = ReadInt(L"Wind", d.wind + 1000, 900, 1100) - 1000;  // stored with offset (no negatives in INI ints)
    gusts = ReadInt(L"Gusts", d.gusts, 0, 100);
    flakeStyle = ReadInt(L"FlakeStyle", d.flakeStyle, 0, 2);
    snowOnWindows = ReadBool(L"SnowOnWindows", d.snowOnWindows);
    snowOnTaskbar = ReadBool(L"SnowOnTaskbar", d.snowOnTaskbar);
    icicles = ReadBool(L"Icicles", d.icicles);
    accumulation = ReadInt(L"Accumulation", d.accumulation, 0, 100);
    windowDepth = ReadInt(L"WindowDepth", d.windowDepth, 0, 100);
    groundDepth = ReadInt(L"GroundDepth", d.groundDepth, 0, 100);
    snowman = ReadBool(L"Snowman", d.snowman);
    snowmanFreq = ReadInt(L"SnowmanFrequency", d.snowmanFreq, 0, 2);
    cursorPlay = ReadBool(L"CursorPlay", d.cursorPlay);
    flakeOutline = ReadBool(L"FlakeOutline", d.flakeOutline);
    pauseFullscreen = ReadBool(L"PauseFullscreen", d.pauseFullscreen);
    batterySaver = ReadBool(L"BatterySaver", d.batterySaver);
    fpsCap = ReadInt(L"FpsCap", d.fpsCap, 0, 240);
    if (fpsCap != 0 && fpsCap != 30 && fpsCap != 60) fpsCap = 60;
    hideFromCapture = ReadBool(L"HideFromCapture", d.hideFromCapture);
    showFps = ReadBool(L"ShowFps", d.showFps);
    firstRun = ReadBool(L"FirstRun", d.firstRun);
}

void Settings::Save() const {
    WriteInt(L"Snowing", snowing);
    WriteInt(L"Intensity", intensity);
    WriteInt(L"FlakeSize", flakeSize);
    WriteInt(L"FallSpeed", fallSpeed);
    WriteInt(L"Wind", wind + 1000);
    WriteInt(L"Gusts", gusts);
    WriteInt(L"FlakeStyle", flakeStyle);
    WriteInt(L"SnowOnWindows", snowOnWindows);
    WriteInt(L"SnowOnTaskbar", snowOnTaskbar);
    WriteInt(L"Icicles", icicles);
    WriteInt(L"Accumulation", accumulation);
    WriteInt(L"WindowDepth", windowDepth);
    WriteInt(L"GroundDepth", groundDepth);
    WriteInt(L"Snowman", snowman);
    WriteInt(L"SnowmanFrequency", snowmanFreq);
    WriteInt(L"CursorPlay", cursorPlay);
    WriteInt(L"FlakeOutline", flakeOutline);
    WriteInt(L"PauseFullscreen", pauseFullscreen);
    WriteInt(L"BatterySaver", batterySaver);
    WriteInt(L"FpsCap", fpsCap);
    WriteInt(L"HideFromCapture", hideFromCapture);
    WriteInt(L"ShowFps", showFps);
    WriteInt(L"FirstRun", firstRun);
}

void Settings::ResetToDefaults() {
    bool fr = firstRun;
    *this = Settings();
    firstRun = fr;
}

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool Settings::GetAutostart() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[MAX_PATH * 2];
    DWORD type = 0, size = sizeof(buf);
    LONG r = RegQueryValueExW(k, L"Snowy", nullptr, &type, (BYTE*)buf, &size);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool Settings::SetAutostart(bool enable) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LONG r;
    if (enable) {
        std::wstring cmd = L"\"" + ExePath() + L"\" --startup";
        r = RegSetValueExW(k, L"Snowy", 0, REG_SZ, (const BYTE*)cmd.c_str(),
                           (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(k, L"Snowy");
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
