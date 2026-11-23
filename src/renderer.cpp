// Snowy - GPU renderer implementation.
#include "renderer.h"

#include <d3dcompiler.h>

#include "shaders.h"

typedef HRESULT(WINAPI* PFN_D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                          LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
typedef HRESULT(WINAPI* PFN_DCompCreateDevice)(IDXGIDevice*, REFIID, void**);
typedef BOOL(WINAPI* PFN_SetWindowDisplayAffinity)(HWND, DWORD);

static const wchar_t* kOverlayClass = L"SnowyOverlayWindow";
static const int kHeightsWidth = 2048;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

struct FrameCB {
    float view[4];
    float misc[4];
};

// Flag set by overlay windows when the display configuration changes.
volatile LONG g_displayChanged = 0;

static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        return 0;  // only the engine destroys overlays
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        InterlockedExchange(&g_displayChanged, 1);
        return 0;
    case WM_SETTINGCHANGE:
        if (w == SPI_SETWORKAREA) InterlockedExchange(&g_displayChanged, 1);
        break;
    case WM_WINDOWPOSCHANGING: {
        // Never let anything (e.g. DPI changes) move or resize the overlay.
        WINDOWPOS* wp = (WINDOWPOS*)l;
        if (GetPropW(h, L"SnowyLock")) wp->flags |= SWP_NOMOVE | SWP_NOSIZE;
        break;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------
bool Renderer::Init(HINSTANCE inst, RenderMode preferred, bool forceWarp) {
    inst_ = inst;
    preferred_ = preferred;
    forceWarp_ = forceWarp;

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = inst;
    wc.lpszClassName = kOverlayClass;
    wc.hCursor = nullptr;
    RegisterClassExW(&wc);

    d3dcompiler_ = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!d3dcompiler_) {
        Logf("d3dcompiler_47.dll not found");
        return false;
    }
    if (!CreateDevice(forceWarp)) return false;
    if (!CompileShaders()) return false;
    if (!CreateStates()) return false;

    mode_ = preferred;
    if (mode_ == RenderMode::DirectComposition) {
        dcompDll_ = LoadLibraryW(L"dcomp.dll");
        auto create = dcompDll_ ? (PFN_DCompCreateDevice)GetProcAddress(dcompDll_, "DCompositionCreateDevice")
                                : nullptr;
        ComPtr<IDXGIDevice> dxgiDev;
        HRESULT hr = E_FAIL;
        if (create && SUCCEEDED(device_.As(dxgiDev))) {
            hr = create(dxgiDev.Get(), __uuidof(IDCompositionDevice), dcomp_.PutVoid());
        }
        if (FAILED(hr) || !dcomp_) {
            Logf("DirectComposition unavailable (hr=0x%08lx); using layered-window fallback", (unsigned long)hr);
            mode_ = RenderMode::LayeredWindow;
        }
    }
    Logf("Renderer ready: mode=%s adapter=\"%s\" featureLevel=0x%x",
         mode_ == RenderMode::DirectComposition ? "DirectComposition"
                                                : (mode_ == RenderMode::LayeredWindow ? "LayeredWindow" : "Offscreen"),
         adapterName_.c_str(), (unsigned)featureLevel_);
    return true;
}

static ComPtr<IDXGIAdapter1> PickAdapter(std::string& nameOut) {
    ComPtr<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), f.PutVoid()))) return {};
    POINT zero = {0, 0};
    HMONITOR prim = MonitorFromPoint(zero, MONITOR_DEFAULTTOPRIMARY);
    for (UINT i = 0;; i++) {
        ComPtr<IDXGIAdapter1> a;
        if (f->EnumAdapters1(i, a.Put()) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d;
        if (FAILED(a->GetDesc1(&d)) || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        for (UINT j = 0;; j++) {
            ComPtr<IDXGIOutput> o;
            if (a->EnumOutputs(j, o.Put()) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC od;
            if (SUCCEEDED(o->GetDesc(&od)) && od.Monitor == prim) {
                nameOut = Narrow(d.Description);
                return a;
            }
        }
    }
    return {};
}

bool Renderer::CreateDevice(bool warp) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = E_FAIL;
    adapterName_.clear();
    if (!warp) {
        ComPtr<IDXGIAdapter1> adapter = PickAdapter(adapterName_);
        if (adapter) {
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, fls, 3,
                                   D3D11_SDK_VERSION, device_.Put(), &featureLevel_, ctx_.Put());
        }
        if (FAILED(hr)) {
            adapterName_.clear();
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fls, 3, D3D11_SDK_VERSION,
                                   device_.Put(), &featureLevel_, ctx_.Put());
        }
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, fls, 3, D3D11_SDK_VERSION,
                               device_.Put(), &featureLevel_, ctx_.Put());
        adapterName_ = "WARP (software)";
    }
    if (FAILED(hr)) {
        Logf("D3D11CreateDevice failed: 0x%08lx", (unsigned long)hr);
        return false;
    }
    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(device_.As(dxgiDev)) || FAILED(dxgiDev->GetAdapter(adapter.Put()))) return false;
    if (adapterName_.empty()) {
        DXGI_ADAPTER_DESC d;
        if (SUCCEEDED(adapter->GetDesc(&d))) adapterName_ = Narrow(d.Description);
    }
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), factory_.PutVoid()))) {
        Logf("IDXGIFactory2 unavailable");
        factory_ = nullptr;
    }
    // Keep GPU work low priority relative to games and the compositor.
    dxgiDev->SetGPUThreadPriority(-2);
    deviceLost_ = false;
    return true;
}

static bool CompileOne(PFN_D3DCompileFn compile, const char* entry, const char* profile, ComPtr<ID3DBlob>& out) {
    ComPtr<ID3DBlob> err;
    HRESULT hr = compile(kShaderSource, sizeof(kShaderSource) - 1, "snowy.hlsl", nullptr, nullptr, entry, profile,
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out.Put(), err.Put());
    if (FAILED(hr)) {
        Logf("Shader %s (%s) failed: 0x%08lx %s", entry, profile, (unsigned long)hr,
             err ? (const char*)err->GetBufferPointer() : "");
        return false;
    }
    return true;
}

bool Renderer::CompileShaders() {
    auto compile = (PFN_D3DCompileFn)GetProcAddress(d3dcompiler_, "D3DCompile");
    if (!compile) return false;
    ComPtr<ID3DBlob> b;
#define SNOWY_VS(entry, vs, il, desc)                                                                      \
    if (!CompileOne(compile, entry, "vs_4_0", b)) return false;                                            \
    if (FAILED(device_->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, vs.Put()))) \
        return false;                                                                                      \
    if (FAILED(device_->CreateInputLayout(desc, (UINT)(sizeof(desc) / sizeof(desc[0])), b->GetBufferPointer(), \
                                          b->GetBufferSize(), il.Put())))                                  \
        return false;
#define SNOWY_PS(entry, ps)                                                                               \
    if (!CompileOne(compile, entry, "ps_4_0", b)) return false;                                           \
    if (FAILED(device_->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, ps.Put()))) \
        return false;

    const D3D11_INPUT_CLASSIFICATION I = D3D11_INPUT_PER_INSTANCE_DATA;
    D3D11_INPUT_ELEMENT_DESC occDesc[] = {
        {"ORECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, I, 1},
        {"ODEPTH", 0, DXGI_FORMAT_R32_FLOAT, 0, 16, I, 1},
    };
    D3D11_INPUT_ELEMENT_DESC pileDesc[] = {
        {"PA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, I, 1},
        {"PB", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, I, 1},
        {"PC", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, I, 1},
    };
    D3D11_INPUT_ELEMENT_DESC shapeDesc[] = {
        {"SA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, I, 1},
        {"SB", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, I, 1},
        {"SC", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, I, 1},
        {"SD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, I, 1},
        {"SE", 0, DXGI_FORMAT_R32_FLOAT, 0, 64, I, 1},
    };
    D3D11_INPUT_ELEMENT_DESC flakeDesc[] = {
        {"FA", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, I, 1},
        {"FB", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, I, 1},
    };
    SNOWY_VS("OccVS", occVS_, occIL_, occDesc)
    SNOWY_PS("OccPS", occPS_)
    SNOWY_VS("PileVS", pileVS_, pileIL_, pileDesc)
    SNOWY_PS("PilePS", pilePS_)
    SNOWY_VS("ShapeVS", shapeVS_, shapeIL_, shapeDesc)
    SNOWY_PS("ShapePS", shapePS_)
    SNOWY_VS("FlakeVS", flakeVS_, flakeIL_, flakeDesc)
    SNOWY_PS("FlakePS", flakePS_)
#undef SNOWY_VS
#undef SNOWY_PS
    return true;
}

bool Renderer::CreateStates() {
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&bd, blendPremul_.Put()))) return false;
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = 0;
    if (FAILED(device_->CreateBlendState(&bd, blendNoColor_.Put()))) return false;

    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(device_->CreateDepthStencilState(&dd, dsWrite_.Put()))) return false;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device_->CreateDepthStencilState(&dd, dsTest_.Put()))) return false;

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&rd, raster_.Put()))) return false;

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(FrameCB);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&cbd, nullptr, cb_.Put()))) return false;
    return true;
}

void Renderer::Shutdown() {
    for (auto& t : targets_) DestroyTarget(t);
    targets_.clear();
    if (ctx_) {
        ctx_->ClearState();
        ctx_->Flush();
    }
    occVB_ = pileVB_ = shapeVB_ = flakeVB_ = nullptr;
    occCap_ = pileCap_ = shapeCap_ = flakeCap_ = 0;
    heightsTex_ = nullptr;
    heightsSRV_ = nullptr;
    heightsRows_ = 0;
    occVS_ = pileVS_ = shapeVS_ = flakeVS_ = nullptr;
    occPS_ = pilePS_ = shapePS_ = flakePS_ = nullptr;
    occIL_ = pileIL_ = shapeIL_ = flakeIL_ = nullptr;
    blendPremul_ = blendNoColor_ = nullptr;
    dsWrite_ = dsTest_ = nullptr;
    raster_ = nullptr;
    cb_ = nullptr;
    dcomp_ = nullptr;
    factory_ = nullptr;
    ctx_ = nullptr;
    device_ = nullptr;
}

bool Renderer::Recreate() {
    Logf("Recreating GPU device");
    for (auto& t : targets_) ReleaseGpuTargetResources(t);
    if (dcomp_) dcomp_ = nullptr;
    if (ctx_) {
        ctx_->ClearState();
        ctx_->Flush();
    }
    occVB_ = pileVB_ = shapeVB_ = flakeVB_ = nullptr;
    occCap_ = pileCap_ = shapeCap_ = flakeCap_ = 0;
    heightsTex_ = nullptr;
    heightsSRV_ = nullptr;
    heightsRows_ = 0;
    ctx_ = nullptr;
    device_ = nullptr;
    factory_ = nullptr;
    if (!CreateDevice(forceWarp_) || !CompileShaders() || !CreateStates()) return false;
    if (mode_ == RenderMode::DirectComposition) {
        auto create = dcompDll_ ? (PFN_DCompCreateDevice)GetProcAddress(dcompDll_, "DCompositionCreateDevice")
                                : nullptr;
        ComPtr<IDXGIDevice> dxgiDev;
        if (!create || FAILED(device_.As(dxgiDev)) ||
            FAILED(create(dxgiDev.Get(), __uuidof(IDCompositionDevice), dcomp_.PutVoid())))
            return false;
    }
    for (auto& t : targets_) {
        if (!CreateTargetGpuResources(t)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------
HWND Renderer::CreateOverlayWindow(const RECT& rc, bool noRedirection) {
    DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (noRedirection) ex |= WS_EX_NOREDIRECTIONBITMAP;
    HWND h = CreateWindowExW(ex, kOverlayClass, L"Snowy", WS_POPUP, rc.left, rc.top, RectW(rc), RectH(rc),
                             nullptr, nullptr, inst_, nullptr);
    if (!h) {
        Logf("CreateWindowEx(overlay) failed: %lu", GetLastError());
        return nullptr;
    }
    SetPropW(h, L"SnowyIgnore", (HANDLE)1);
    if (noRedirection) SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
    if (captureExcluded_) {
        auto fn = (PFN_SetWindowDisplayAffinity)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                               "SetWindowDisplayAffinity");
        if (fn) fn(h, WDA_EXCLUDEFROMCAPTURE);
    }
    return h;
}

bool Renderer::CreateTargetGpuResources(Target& t) {
    if (mode_ == RenderMode::DirectComposition) {
        if (!factory_) return false;
        DXGI_SWAP_CHAIN_DESC1 d = {};
        d.Width = (UINT)t.w;
        d.Height = (UINT)t.h;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        d.Flags = t.primary ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;
        HRESULT hr = factory_->CreateSwapChainForComposition(device_.Get(), &d, nullptr, t.swap.Put());
        if (FAILED(hr) && d.Flags) {
            d.Flags = 0;
            hr = factory_->CreateSwapChainForComposition(device_.Get(), &d, nullptr, t.swap.Put());
        }
        if (FAILED(hr)) {
            Logf("CreateSwapChainForComposition failed: 0x%08lx", (unsigned long)hr);
            return false;
        }
        if (d.Flags) {
            ComPtr<IDXGISwapChain2> sc2;
            if (SUCCEEDED(t.swap.As(sc2))) {
                sc2->SetMaximumFrameLatency(1);
                t.waitable = sc2->GetFrameLatencyWaitableObject();
            }
        }
        if (FAILED(dcomp_->CreateTargetForHwnd(t.hwnd, TRUE, t.dcTarget.Put())) ||
            FAILED(dcomp_->CreateVisual(t.dcVisual.Put())) || FAILED(t.dcVisual->SetContent(t.swap.Get())) ||
            FAILED(t.dcTarget->SetRoot(t.dcVisual.Get())) || FAILED(dcomp_->Commit())) {
            Logf("DirectComposition target setup failed");
            return false;
        }
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(t.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), back.PutVoid()))) return false;
        if (FAILED(device_->CreateRenderTargetView(back.Get(), nullptr, t.rtv.Put()))) return false;
    } else {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)t.w;
        td.Height = (UINT)t.h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, t.color.Put()))) return false;
        if (FAILED(device_->CreateRenderTargetView(t.color.Get(), nullptr, t.rtv.Put()))) return false;
    }
    // staging textures (layered presentation and captures)
    {
        D3D11_TEXTURE2D_DESC sd = {};
        sd.Width = (UINT)t.w;
        sd.Height = (UINT)t.h;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        int n = (mode_ == RenderMode::LayeredWindow) ? 2 : 1;
        for (int i = 0; i < n; i++) {
            if (FAILED(device_->CreateTexture2D(&sd, nullptr, t.staging[i].Put()))) return false;
            t.stagingPending[i] = false;
        }
        t.stagingNext = 0;
    }
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = (UINT)t.w;
    dd.Height = (UINT)t.h;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&dd, nullptr, t.depth.Put()))) return false;
    if (FAILED(device_->CreateDepthStencilView(t.depth.Get(), nullptr, t.dsv.Put()))) return false;
    t.lastPresentEmpty = false;
    t.presentedLastFrame = false;
    return true;
}

void Renderer::ReleaseGpuTargetResources(Target& t) {
    t.rtv = nullptr;
    t.dsv = nullptr;
    t.depth = nullptr;
    t.color = nullptr;
    t.staging[0] = nullptr;
    t.staging[1] = nullptr;
    if (t.waitable) {
        CloseHandle(t.waitable);
        t.waitable = nullptr;
    }
    t.dcVisual = nullptr;
    t.dcTarget = nullptr;
    t.swap = nullptr;
}

bool Renderer::CreateTarget(Target& t, const MonitorDesc& m) {
    t.rc = m.rc;
    t.w = std::max(1, RectW(m.rc));
    t.h = std::max(1, RectH(m.rc));
    t.scale = m.scale;
    t.refresh = m.refresh;
    t.primary = m.primary;
    if (mode_ != RenderMode::Offscreen) {
        t.hwnd = CreateOverlayWindow(m.rc, mode_ == RenderMode::DirectComposition);
        if (!t.hwnd) return false;
        SetPropW(t.hwnd, L"SnowyLock", (HANDLE)1);
    }
    if (mode_ == RenderMode::LayeredWindow) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = t.w;
        bi.bmiHeader.biHeight = -t.h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr);
        t.memDC = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        void* bits = nullptr;
        t.dib = CreateDIBSection(t.memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!t.dib) return false;
        t.dibBits = (uint8_t*)bits;
        t.oldBmp = SelectObject(t.memDC, t.dib);
    }
    if (!CreateTargetGpuResources(t)) return false;
    if (t.hwnd) {
        ShowWindow(t.hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(t.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        t.windowShown = true;
    }
    return true;
}

void Renderer::DestroyTarget(Target& t) {
    ReleaseGpuTargetResources(t);
    if (t.memDC) {
        if (t.oldBmp) SelectObject(t.memDC, t.oldBmp);
        DeleteDC(t.memDC);
        t.memDC = nullptr;
    }
    if (t.dib) {
        DeleteObject(t.dib);
        t.dib = nullptr;
        t.dibBits = nullptr;
    }
    if (t.hwnd) {
        DestroyWindow(t.hwnd);
        t.hwnd = nullptr;
    }
}

bool Renderer::SetMonitors(const std::vector<MonitorDesc>& mons) {
    bool same = mons.size() == monitors_.size();
    for (size_t i = 0; same && i < mons.size(); i++) {
        const auto& a = mons[i];
        const auto& b = monitors_[i];
        same = EqualRect(&a.rc, &b.rc) && a.scale == b.scale && a.primary == b.primary;
    }
    if (same && targets_.size() == mons.size()) {
        monitors_ = mons;
        return true;
    }
    for (auto& t : targets_) DestroyTarget(t);
    targets_.clear();
    monitors_ = mons;
    targets_.resize(mons.size());
    for (size_t i = 0; i < mons.size(); i++) {
        if (!CreateTarget(targets_[i], mons[i])) {
            Logf("Failed to create overlay for monitor %u", (unsigned)i);
            if (mode_ == RenderMode::DirectComposition) {
                // Switch everything to the fallback path and retry once.
                for (auto& t : targets_) DestroyTarget(t);
                targets_.clear();
                targets_.resize(mons.size());
                mode_ = RenderMode::LayeredWindow;
                dcomp_ = nullptr;
                Logf("Retrying with layered-window fallback");
                for (size_t j = 0; j < mons.size(); j++) {
                    if (!CreateTarget(targets_[j], mons[j])) return false;
                }
                return true;
            }
            return false;
        }
    }
    return true;
}

std::vector<HWND> Renderer::OverlayWindows() const {
    std::vector<HWND> v;
    for (auto& t : targets_)
        if (t.hwnd) v.push_back(t.hwnd);
    return v;
}

HANDLE Renderer::PacingWaitable() const {
    for (auto& t : targets_) {
        if (t.primary && t.waitable && t.windowShown && t.presentedLastFrame) return t.waitable;
    }
    return nullptr;
}

void Renderer::ReassertTopmost() {
    for (auto& t : targets_) {
        if (!t.hwnd || !t.windowShown) continue;
        // Only bother if some other visible top-most window sits above us.
        bool needed = false;
        for (HWND h = GetWindow(t.hwnd, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV)) {
            if (!IsWindowVisible(h) || GetPropW(h, L"SnowyIgnore")) continue;
            if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) {
                needed = true;
                break;
            }
        }
        if (needed)
            SetWindowPos(t.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void Renderer::SetCaptureExcluded(bool exclude) {
    captureExcluded_ = exclude;
    auto fn = (PFN_SetWindowDisplayAffinity)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                           "SetWindowDisplayAffinity");
    if (!fn) return;
    for (auto& t : targets_)
        if (t.hwnd) fn(t.hwnd, exclude ? WDA_EXCLUDEFROMCAPTURE : 0);
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------
bool Renderer::EnsureBuffer(ComPtr<ID3D11Buffer>& buf, size_t& cap, size_t needBytes) {
    if (buf && cap >= needBytes) return true;
    size_t n = std::max<size_t>(4096, cap);
    while (n < needBytes) n *= 2;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)n;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buf = nullptr;
    if (FAILED(device_->CreateBuffer(&bd, nullptr, buf.Put()))) {
        cap = 0;
        return false;
    }
    cap = n;
    return true;
}

bool Renderer::Upload(ComPtr<ID3D11Buffer>& buf, size_t& cap, const void* data, size_t bytes) {
    if (bytes == 0) return true;
    if (!EnsureBuffer(buf, cap, bytes)) return false;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    memcpy(m.pData, data, bytes);
    ctx_->Unmap(buf.Get(), 0);
    return true;
}

bool Renderer::UploadHeights(const std::vector<float>& h) {
    int rows = std::max(1, (int)((h.size() + kHeightsWidth - 1) / kHeightsWidth));
    if (!heightsTex_ || rows > heightsRows_) {
        int newRows = std::max(8, heightsRows_);
        while (newRows < rows) newRows *= 2;
        heightsTex_ = nullptr;
        heightsSRV_ = nullptr;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = kHeightsWidth;
        td.Height = (UINT)newRows;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, heightsTex_.Put()))) return false;
        if (FAILED(device_->CreateShaderResourceView(heightsTex_.Get(), nullptr, heightsSRV_.Put()))) return false;
        heightsRows_ = newRows;
    }
    if (h.empty()) return true;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(heightsTex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    size_t remaining = h.size();
    const float* src = h.data();
    for (int row = 0; row < rows; row++) {
        size_t n = std::min<size_t>(remaining, kHeightsWidth);
        memcpy((uint8_t*)m.pData + (size_t)row * m.RowPitch, src, n * sizeof(float));
        src += n;
        remaining -= n;
    }
    ctx_->Unmap(heightsTex_.Get(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void Renderer::DrawTarget(Target& t, const DrawLists& L) {
    ID3D11RenderTargetView* rtv = t.rtv.Get();
    ctx_->OMSetRenderTargets(1, &rtv, t.dsv.Get());
    const float clear[4] = {0, 0, 0, 0};
    ctx_->ClearRenderTargetView(rtv, clear);
    ctx_->ClearDepthStencilView(t.dsv.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);

    D3D11_VIEWPORT vp = {0, 0, (float)t.w, (float)t.h, 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(raster_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        FrameCB cb = {{(float)t.rc.left, (float)t.rc.top, 1.f / (float)t.w, 1.f / (float)t.h},
                      {L.time, t.scale, L.outline, 0.f}};
        memcpy(m.pData, &cb, sizeof(cb));
        ctx_->Unmap(cb_.Get(), 0);
    }
    ID3D11Buffer* cbs[1] = {cb_.Get()};
    ctx_->VSSetConstantBuffers(0, 1, cbs);
    ctx_->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srv = heightsSRV_.Get();
    ctx_->PSSetShaderResources(0, 1, &srv);

    UINT zero = 0;
    auto bind = [&](ID3D11Buffer* vb, UINT stride, ID3D11InputLayout* il, ID3D11VertexShader* vs,
                    ID3D11PixelShader* ps) {
        ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &zero);
        ctx_->IASetInputLayout(il);
        ctx_->VSSetShader(vs, nullptr, 0);
        ctx_->PSSetShader(ps, nullptr, 0);
    };

    // 1. Occluders (depth only)
    if (!L.occluders.empty()) {
        bind(occVB_.Get(), sizeof(OccluderInst), occIL_.Get(), occVS_.Get(), occPS_.Get());
        ctx_->OMSetBlendState(blendNoColor_.Get(), nullptr, 0xffffffff);
        ctx_->OMSetDepthStencilState(dsWrite_.Get(), 0);
        ctx_->DrawInstanced(4, (UINT)L.occluders.size(), 0, 0);
    }
    ctx_->OMSetBlendState(blendPremul_.Get(), nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsTest_.Get(), 0);

    auto drawFlakes = [&](uint32_t start, uint32_t end) {
        if (end <= start) return;
        bind(flakeVB_.Get(), sizeof(FlakeInst), flakeIL_.Get(), flakeVS_.Get(), flakePS_.Get());
        ctx_->DrawInstanced(4, end - start, 0, start);
    };
    auto drawPiles = [&](uint32_t start, uint32_t end) {
        if (end <= start) return;
        bind(pileVB_.Get(), sizeof(PileInst), pileIL_.Get(), pileVS_.Get(), pilePS_.Get());
        ctx_->DrawInstanced(4, end - start, 0, start);
    };
    auto drawShapes = [&](uint32_t start, uint32_t end) {
        if (end <= start) return;
        bind(shapeVB_.Get(), sizeof(ShapeInst), shapeIL_.Get(), shapeVS_.Get(), shapePS_.Get());
        ctx_->DrawInstanced(4, end - start, 0, start);
    };

    const uint32_t nPiles = (uint32_t)L.piles.size();
    const uint32_t nShapes = (uint32_t)L.shapes.size();
    const uint32_t nFlakes = (uint32_t)L.flakes.size();
    drawFlakes(0, L.flakeFarEnd);                       // 2. distant flakes
    drawPiles(0, L.capPileCount);                       // 3. snow on windows
    drawShapes(0, L.shapeAttachedEnd);                  // 4. icicles
    drawShapes(L.shapeAttachedEnd, L.shapeSnowmanEnd);  // 5. snowmen
    drawPiles(L.capPileCount, nPiles);                  // 6. snow on the taskbar
    drawShapes(L.shapeSnowmanEnd, nShapes);             // 7. falling chunks, debris, powder
    drawFlakes(L.flakeFarEnd, nFlakes);                 // 8. falling snow

    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);
}

void Renderer::DoCapture(Target& t) {
    ComPtr<ID3D11Texture2D> src;
    if (t.color) {
        src = t.color;
    } else if (t.swap) {
        t.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), src.PutVoid());
    }
    if (!src || !t.staging[0]) return;
    // In layered mode staging[] is in use for presentation; use a temporary texture.
    ComPtr<ID3D11Texture2D> staging = t.staging[0];
    if (mode_ == RenderMode::LayeredWindow) {
        D3D11_TEXTURE2D_DESC sd;
        t.staging[0]->GetDesc(&sd);
        staging = nullptr;
        if (FAILED(device_->CreateTexture2D(&sd, nullptr, staging.Put()))) return;
    }
    ctx_->CopyResource(staging.Get(), src.Get());
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
    captureW_ = t.w;
    captureH_ = t.h;
    captureData_.resize((size_t)t.w * t.h * 4);
    for (int y = 0; y < t.h; y++)
        memcpy(&captureData_[(size_t)y * t.w * 4], (uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)t.w * 4);
    ctx_->Unmap(staging.Get(), 0);
    captureReady_ = true;
}

bool Renderer::PresentTarget(Target& t, int syncInterval, bool pacing) {
    if (mode_ == RenderMode::DirectComposition) {
        HRESULT hr = t.swap->Present(pacing ? (UINT)syncInterval : 0u, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            Logf("Present: device lost (0x%08lx)", (unsigned long)hr);
            return false;
        }
        return true;
    }
    if (mode_ == RenderMode::Offscreen) {
        ctx_->Flush();
        return true;
    }
    // Layered window: copy into staging, read back the previous frame, UpdateLayeredWindow.
    int cur = t.stagingNext;
    int prev = 1 - cur;
    ctx_->CopyResource(t.staging[cur].Get(), t.color.Get());
    t.stagingPending[cur] = true;
    if (t.stagingPending[prev]) {
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = ctx_->Map(t.staging[prev].Get(), 0, D3D11_MAP_READ, 0, &m);
        if (hr == DXGI_ERROR_DEVICE_REMOVED) return false;
        if (SUCCEEDED(hr)) {
            for (int y = 0; y < t.h; y++)
                memcpy(t.dibBits + (size_t)y * t.w * 4, (uint8_t*)m.pData + (size_t)y * m.RowPitch,
                       (size_t)t.w * 4);
            ctx_->Unmap(t.staging[prev].Get(), 0);
            POINT dst = {t.rc.left, t.rc.top};
            SIZE sz = {t.w, t.h};
            POINT src = {0, 0};
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(t.hwnd, nullptr, &dst, &sz, t.memDC, &src, 0, &bf, ULW_ALPHA);
        }
        t.stagingPending[prev] = false;
    }
    t.stagingNext = prev;
    return true;
}

bool Renderer::Render(const DrawLists& L, const std::vector<char>& show, const std::vector<char>& content,
                      int syncInterval) {
    if (!device_) return false;
    // Window visibility first (hidden overlays cost nothing to the compositor).
    for (size_t i = 0; i < targets_.size(); i++) {
        Target& t = targets_[i];
        bool want = i < show.size() ? show[i] != 0 : true;
        if (t.hwnd && want != t.windowShown) {
            ShowWindow(t.hwnd, want ? SW_SHOWNOACTIVATE : SW_HIDE);
            t.windowShown = want;
            if (want) {
                SetWindowPos(t.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                t.lastPresentEmpty = false;
            }
        }
    }

    if (!Upload(occVB_, occCap_, L.occluders.data(), L.occluders.size() * sizeof(OccluderInst)) ||
        !Upload(pileVB_, pileCap_, L.piles.data(), L.piles.size() * sizeof(PileInst)) ||
        !Upload(shapeVB_, shapeCap_, L.shapes.data(), L.shapes.size() * sizeof(ShapeInst)) ||
        !Upload(flakeVB_, flakeCap_, L.flakes.data(), L.flakes.size() * sizeof(FlakeInst)) ||
        !UploadHeights(L.heights)) {
        HRESULT reason = device_->GetDeviceRemovedReason();
        if (FAILED(reason)) {
            Logf("Upload failed: device removed 0x%08lx", (unsigned long)reason);
            return false;
        }
        return true;
    }

    bool ok = true;
    for (size_t i = 0; i < targets_.size(); i++) {
        Target& t = targets_[i];
        t.presentedLastFrame = false;
        bool visible = (mode_ == RenderMode::Offscreen) || t.windowShown;
        if (!visible) continue;
        bool hasContent = i < content.size() ? content[i] != 0 : true;
        bool pacing = t.primary && t.waitable != nullptr;
        bool wantCapture = captureRequest_ == (int)i;
        // Skip work entirely if nothing to draw and the last frame was already empty.
        if (!hasContent && t.lastPresentEmpty && !wantCapture) continue;
        DrawTarget(t, L);
        if (wantCapture) {
            DoCapture(t);
            captureRequest_ = -1;
        }
        if (!PresentTarget(t, syncInterval, pacing)) {
            ok = false;
            break;
        }
        t.presentedLastFrame = true;
        t.lastPresentEmpty = !hasContent;
    }
    if (ok) {
        HRESULT reason = device_->GetDeviceRemovedReason();
        if (FAILED(reason)) {
            Logf("Device removed: 0x%08lx", (unsigned long)reason);
            ok = false;
        }
    }
    return ok;
}

void Renderer::RequestCapture(size_t i) {
    captureRequest_ = (int)i;
    captureReady_ = false;
}

bool Renderer::TakeCapture(std::vector<uint8_t>& bgra, int& w, int& h) {
    if (!captureReady_) return false;
    bgra.swap(captureData_);
    w = captureW_;
    h = captureH_;
    captureReady_ = false;
    return true;
}
