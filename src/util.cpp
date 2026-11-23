// Snowy - utility functions: timing, paths, logging.
#include "common.h"

#include <shlobj.h>
#include <cstdarg>

double NowSeconds() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

static std::wstring KnownDir(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) {
        out = p;
    }
    if (p) CoTaskMemFree(p);
    if (out.empty()) {
        wchar_t tmp[MAX_PATH];
        DWORD n = GetTempPathW(MAX_PATH, tmp);
        out.assign(tmp, n);
    }
    if (!out.empty() && out.back() != L'\\') out += L'\\';
    out += L"Snowy";
    CreateDirectoryW(out.c_str(), nullptr);
    return out;
}

std::wstring AppDataDir() { return KnownDir(FOLDERID_RoamingAppData); }
std::wstring LocalDataDir() { return KnownDir(FOLDERID_LocalAppData); }

std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return std::wstring(buf, n);
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// ---------------------------------------------------------------------------
// Logging: %LOCALAPPDATA%\Snowy\snowy.log (overwritten each launch).
// ---------------------------------------------------------------------------
static CRITICAL_SECTION g_logLock;
static FILE* g_logFile = nullptr;
static bool g_logInit = false;
static bool g_logVerbose = false;
static double g_logStart = 0;

void LogOpen(bool verbose) {
    if (g_logInit) return;
    InitializeCriticalSection(&g_logLock);
    g_logInit = true;
    g_logVerbose = verbose;
    g_logStart = NowSeconds();
    std::wstring path = LocalDataDir() + L"\\snowy.log";
    g_logFile = _wfopen(path.c_str(), L"wb");
    Logf("Snowy %s starting", SNOWY_VERSION_A);
}

void LogClose() {
    if (!g_logInit) return;
    EnterCriticalSection(&g_logLock);
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = nullptr;
    }
    LeaveCriticalSection(&g_logLock);
}

void Logf(const char* fmt, ...) {
    if (!g_logInit) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_logLock);
    double t = NowSeconds() - g_logStart;
    if (g_logFile) {
        fprintf(g_logFile, "[%8.3f] %s\r\n", t, buf);
        fflush(g_logFile);
    }
    if (g_logVerbose) {
        fprintf(stderr, "[%8.3f] %s\n", t, buf);
        fflush(stderr);
    }
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    LeaveCriticalSection(&g_logLock);
}
