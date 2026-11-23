// Snowy - PNG export, fake desktop backdrop, screen capture.
#include "capture.h"

#include <wincodec.h>

bool SavePng(const std::wstring& path, int w, int h, const uint8_t* bgra) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                                  factory.PutVoid());
    if (FAILED(hr)) return false;
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.Put()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.Put()))) return false;
    if (FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(enc->CreateNewFrame(frame.Put(), props.Put()))) return false;
    if (FAILED(frame->Initialize(props.Get()))) return false;
    if (FAILED(frame->SetSize((UINT)w, (UINT)h))) return false;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&fmt))) return false;
    std::vector<uint8_t> conv;
    const uint8_t* src = bgra;
    if (fmt != GUID_WICPixelFormat32bppBGRA) {
        // Encoder wants 24bpp BGR.
        conv.resize((size_t)w * h * 3);
        for (size_t i = 0; i < (size_t)w * h; i++) {
            conv[i * 3 + 0] = bgra[i * 4 + 0];
            conv[i * 3 + 1] = bgra[i * 4 + 1];
            conv[i * 3 + 2] = bgra[i * 4 + 2];
        }
        src = conv.data();
        if (FAILED(frame->WritePixels((UINT)h, (UINT)w * 3, (UINT)conv.size(), (BYTE*)src))) return false;
    } else {
        if (FAILED(frame->WritePixels((UINT)h, (UINT)w * 4, (UINT)((size_t)w * h * 4), (BYTE*)src))) return false;
    }
    if (FAILED(frame->Commit())) return false;
    if (FAILED(enc->Commit())) return false;
    return true;
}

void CompositeOver(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    size_t n = std::min(dst.size(), src.size()) / 4;
    for (size_t i = 0; i < n; i++) {
        const uint8_t* s = &src[i * 4];
        uint8_t* d = &dst[i * 4];
        unsigned inv = 255u - s[3];
        for (int c = 0; c < 3; c++) {
            unsigned v = s[c] + (d[c] * inv + 127u) / 255u;
            d[c] = (uint8_t)std::min(255u, v);
        }
        d[3] = 255;
    }
}

static void FillRectColor(HDC dc, int x0, int y0, int x1, int y1, COLORREF c) {
    RECT r = {x0, y0, x1, y1};
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void RoundRectColor(HDC dc, int x0, int y0, int x1, int y1, int rad, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, x0, y0, x1, y1, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void DrawWindowChrome(HDC dc, const RECT& rc, const std::wstring& title, int style) {
    int x0 = rc.left, y0 = rc.top, x1 = rc.right, y1 = rc.bottom;
    RoundRectColor(dc, x0, y0, x1, y1, 16, RGB(32, 32, 34), RGB(70, 70, 74));
    FillRectColor(dc, x0 + 1, y0 + 8, x1 - 1, y0 + 34, RGB(43, 43, 46));
    RoundRectColor(dc, x0 + 1, y0 + 1, x1 - 1, y0 + 20, 14, RGB(43, 43, 46), RGB(43, 43, 46));
    HFONT f = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 220, 225));
    RECT tr = {x0 + 14, y0 + 6, x1 - 150, y0 + 30};
    DrawTextW(dc, title.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    // caption buttons
    SetTextColor(dc, RGB(200, 200, 205));
    RECT b1 = {x1 - 138, y0 + 4, x1 - 92, y0 + 30}, b2 = {x1 - 92, y0 + 4, x1 - 46, y0 + 30},
         b3 = {x1 - 46, y0 + 4, x1 - 2, y0 + 30};
    DrawTextW(dc, L"\x2014", -1, &b1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(dc, L"\x25A1", -1, &b2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(dc, L"\x2715", -1, &b3, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (style == 0) {
        SetTextColor(dc, RGB(210, 210, 214));
        const wchar_t* lines[] = {L"Lapland in December", L"Northern lights", L"Hot chocolate, lots",
                                  L"Build a snowman on the taskbar"};
        for (int i = 0; i < 4; i++) {
            RECT lr = {x0 + 16, y0 + 46 + i * 22, x1 - 16, y0 + 66 + i * 22};
            if (lr.bottom > y1 - 8) break;
            DrawTextW(dc, lines[i], -1, &lr, DT_LEFT | DT_SINGLELINE);
        }
    } else if (style == 1) {
        FillRectColor(dc, x0 + 1, y0 + 34, x0 + 170, y1 - 8, RGB(38, 38, 41));
        SetTextColor(dc, RGB(205, 205, 210));
        const wchar_t* side[] = {L"Home", L"Desktop", L"Documents", L"Downloads", L"Pictures"};
        for (int i = 0; i < 5; i++) {
            RECT lr = {x0 + 18, y0 + 50 + i * 26, x0 + 165, y0 + 72 + i * 26};
            if (lr.bottom > y1 - 8) break;
            DrawTextW(dc, side[i], -1, &lr, DT_LEFT | DT_SINGLELINE);
        }
        const wchar_t* files[] = {L"App Mockups", L"Launch Notes.txt", L"Q4 Plan"};
        for (int i = 0; i < 3; i++) {
            int fx = x0 + 200 + i * 120, fy = y0 + 60;
            if (fx + 90 > x1) break;
            COLORREF c = i == 1 ? RGB(235, 235, 240) : RGB(90, 170, 245);
            RoundRectColor(dc, fx + 14, fy, fx + 74, fy + 50, 8, c, c);
            RECT lr = {fx - 10, fy + 56, fx + 98, fy + 76};
            DrawTextW(dc, files[i], -1, &lr, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    } else {
        int px0 = x0 + (x1 - x0) / 4, px1 = x1 - (x1 - x0) / 4;
        TRIVERTEX v[2] = {{px0, y0 + 34, 0x2000, 0x2a00, 0x5000, 0xff00}, {px1, y1 - 2, 0x3800, 0x4800, 0x8800, 0xff00}};
        GRADIENT_RECT gr = {0, 1};
        GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
        RoundRectColor(dc, (px0 + px1) / 2 - 40, y1 - 120, (px0 + px1) / 2 + 40, y1 - 40, 80, RGB(240, 242, 248),
                       RGB(240, 242, 248));
        RoundRectColor(dc, (px0 + px1) / 2 - 28, y1 - 170, (px0 + px1) / 2 + 28, y1 - 114, 56, RGB(240, 242, 248),
                       RGB(240, 242, 248));
    }
    SelectObject(dc, of);
    DeleteObject(f);
}

void DrawFakeDesktop(int w, int h, int taskbarH, const std::vector<FakeWindow>& wins, std::vector<uint8_t>& out) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);

    // Wallpaper: deep blue/violet gradient with soft light streaks.
    uint8_t* px = (uint8_t*)bits;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float fy = (float)y / (float)h, fx = (float)x / (float)w;
            float r = 70 - 40 * fy + 30 * (1 - fx) * (1 - fy);
            float g = 50 + 25 * fy;
            float b = 190 + 30 * fy - 30 * fx * (1 - fy);
            float streak = std::sin((fx * 1.3f - fy * 0.9f) * 9.f) * 0.5f + 0.5f;
            streak = std::pow(streak, 8.f) * 40.f * (0.4f + fy);
            float arc = std::exp(-std::pow((fy - 0.55f - 0.12f * std::cos(fx * 3.f)) * 18.f, 2.f)) * 50.f;
            uint8_t* p = px + ((size_t)y * w + x) * 4;
            p[0] = (uint8_t)Clamp(b + streak + arc, 0.f, 255.f);
            p[1] = (uint8_t)Clamp(g + streak * 0.8f + arc * 0.8f, 0.f, 255.f);
            p[2] = (uint8_t)Clamp(r + streak * 0.6f + arc * 0.5f, 0.f, 255.f);
            p[3] = 255;
        }
    }
    GdiFlush();
    // Windows, back to front (list is front to back).
    for (int i = (int)wins.size() - 1; i >= 0; i--) DrawWindowChrome(dc, wins[(size_t)i].rc, wins[(size_t)i].title, wins[(size_t)i].style);
    // Taskbar
    if (taskbarH > 0) {
        FillRectColor(dc, 0, h - taskbarH, w, h, RGB(28, 30, 38));
        FillRectColor(dc, 0, h - taskbarH, w, h - taskbarH + 1, RGB(60, 62, 72));
        int n = 9, size = taskbarH - 18, gap = 10;
        int total = n * size + (n - 1) * gap;
        int x = (w - total) / 2;
        COLORREF cols[9] = {RGB(0, 120, 215), RGB(240, 190, 40), RGB(60, 180, 90), RGB(220, 70, 60),
                            RGB(140, 90, 220), RGB(30, 160, 220), RGB(240, 120, 40), RGB(200, 200, 210),
                            RGB(40, 200, 170)};
        for (int i = 0; i < n; i++)
            RoundRectColor(dc, x + i * (size + gap), h - taskbarH + 9, x + i * (size + gap) + size, h - 9, 8, cols[i],
                           cols[i]);
    }
    GdiFlush();
    out.resize((size_t)w * h * 4);
    memcpy(out.data(), bits, out.size());
    for (size_t i = 0; i < (size_t)w * h; i++) out[i * 4 + 3] = 255;
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

bool CaptureScreen(std::vector<uint8_t>& out, int& w, int& h, RECT& rc) {
    rc.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rc.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    rc.right = rc.left + w;
    rc.bottom = rc.top + h;
    if (w <= 0 || h <= 0) return false;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    BOOL ok = BitBlt(dc, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    if (ok) {
        out.resize((size_t)w * h * 4);
        memcpy(out.data(), bits, out.size());
        for (size_t i = 0; i < (size_t)w * h; i++) out[i * 4 + 3] = 255;
    }
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return ok != 0;
}

void PaintDemoWindow(HWND hwnd, HDC dc, int style) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 33));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HFONT f = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(215, 215, 220));
    const wchar_t* text = style == 0 ? L"Lapland in December\nNorthern lights\nHot chocolate, lots"
                                     : (style == 1 ? L"App Mockups\nLaunch Notes.txt\nQ4 Plan"
                                                   : L"Snow Day.jpg");
    RECT tr = {rc.left + 16, rc.top + 12, rc.right - 16, rc.bottom - 12};
    DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_TOP);
    SelectObject(dc, of);
    DeleteObject(f);
}
