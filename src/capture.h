// Snowy - image helpers for automated tests and screenshots.
#pragma once
#include "common.h"

struct FakeWindow {
    RECT rc;
    std::wstring title;
    int style = 0;  // 0 = text editor, 1 = file browser, 2 = photo
};

// Saves a top-down BGRA buffer as PNG (via WIC). Caller must have initialized COM.
bool SavePng(const std::wstring& path, int w, int h, const uint8_t* bgra);

// Renders a desktop-like backdrop (wallpaper, windows, taskbar) into an opaque BGRA buffer.
void DrawFakeDesktop(int w, int h, int taskbarH, const std::vector<FakeWindow>& wins, std::vector<uint8_t>& out);

// dst (opaque BGRA) = src (premultiplied BGRA) over dst.
void CompositeOver(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src);

// Captures the whole virtual screen (including layered/composited windows).
bool CaptureScreen(std::vector<uint8_t>& out, int& w, int& h, RECT& rc);

// Paints a demo "application" window (used by --selftest/--demo).
void PaintDemoWindow(HWND hwnd, HDC dc, int style);
