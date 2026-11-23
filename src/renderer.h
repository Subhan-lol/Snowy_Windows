// Snowy - GPU renderer. One transparent, click-through, topmost overlay per
// monitor, drawn with Direct3D 11 and presented through DirectComposition
// (premultiplied-alpha swap chains). Falls back to UpdateLayeredWindow when
// DirectComposition is unavailable, and supports window-less offscreen targets
// for automated rendering tests.
#pragma once
#include "common.h"

#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>

struct OccluderInst {
    float x0, y0, x1, y1;
    float depth;
};

struct PileInst {
    float x0, baseY, cellW, count;
    float offset, maxH, depth, style;  // style: 0 = window cap, 1 = ground
    float seed, skirt, alpha, scale;
};

enum ShapeStyle : int {
    SS_SNOW = 0,
    SS_ICE = 1,
    SS_COAL = 2,
    SS_CARROT = 3,
    SS_WOOD = 4,
    SS_FABRIC = 5,
    SS_SOFT = 6,
    SS_FLAT = 7,
    SS_WATER = 8,
};

struct ShapeInst {
    float ax, ay, bx, by;      // round cone endpoints (box: center, half extents)
    float ra, rb, rot, seed;   // radii (box: corner radius, angle), texture rotation, seed
    float r, g, b, a;          // straight-alpha color
    float style, kind, p2, p3; // kind 0 = round cone, 1 = box
    float depth;
    float pad[3];
};

struct FlakeInst {
    float x, y, size, rot;
    float alpha, type, seed, depth;
};

struct DrawLists {
    std::vector<OccluderInst> occluders;
    std::vector<PileInst> piles;      // [0, capPileCount) window caps, rest = ground piles
    uint32_t capPileCount = 0;
    std::vector<float> heights;       // all pile heights, concatenated
    std::vector<ShapeInst> shapes;    // [0, attachedEnd) icicles, [attachedEnd, snowmanEnd) snowmen, rest front
    uint32_t shapeAttachedEnd = 0;
    uint32_t shapeSnowmanEnd = 0;
    std::vector<FlakeInst> flakes;    // [0, flakeFarEnd) far layer, rest front
    uint32_t flakeFarEnd = 0;
    float time = 0;
    float outline = 1;

    void Clear() {
        occluders.clear();
        piles.clear();
        heights.clear();
        shapes.clear();
        flakes.clear();
        capPileCount = shapeAttachedEnd = shapeSnowmanEnd = flakeFarEnd = 0;
    }
};

struct MonitorDesc {
    HMONITOR handle = nullptr;
    RECT rc{};
    RECT work{};
    float scale = 1.f;
    float refresh = 60.f;
    bool primary = false;
};

enum class RenderMode { DirectComposition, LayeredWindow, Offscreen };

class Renderer {
public:
    Renderer() = default;
    ~Renderer() { Shutdown(); }

    // mode: preferred mode (DirectComposition falls back to LayeredWindow automatically).
    bool Init(HINSTANCE inst, RenderMode preferred, bool forceWarp);
    void Shutdown();
    bool Recreate();  // after device loss

    bool SetMonitors(const std::vector<MonitorDesc>& mons);
    // Renders and presents. show[i]: overlay i should be visible. content[i]: anything to draw.
    // Returns false if the device was lost.
    bool Render(const DrawLists& lists, const std::vector<char>& show, const std::vector<char>& content,
                int syncInterval);

    RenderMode Mode() const { return mode_; }
    HANDLE PacingWaitable() const;  // nullptr if not available
    bool TargetCountOk(size_t n) const { return targets_.size() == n; }
    std::vector<HWND> OverlayWindows() const;
    void ReassertTopmost();
    void SetCaptureExcluded(bool exclude);
    std::string AdapterName() const { return adapterName_; }

    // Request a readback of target i on the next Render(); retrieve with TakeCapture().
    void RequestCapture(size_t i);
    bool TakeCapture(std::vector<uint8_t>& bgraPremul, int& w, int& h);

private:
    struct Target {
        HWND hwnd = nullptr;
        RECT rc{};
        int w = 0, h = 0;
        float scale = 1.f;
        float refresh = 60.f;
        bool primary = false;
        ComPtr<IDXGISwapChain1> swap;
        HANDLE waitable = nullptr;
        ComPtr<IDCompositionTarget> dcTarget;
        ComPtr<IDCompositionVisual> dcVisual;
        ComPtr<ID3D11Texture2D> color;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11Texture2D> depth;
        ComPtr<ID3D11DepthStencilView> dsv;
        ComPtr<ID3D11Texture2D> staging[2];
        bool stagingPending[2] = {false, false};
        int stagingNext = 0;
        HDC memDC = nullptr;
        HBITMAP dib = nullptr;
        HGDIOBJ oldBmp = nullptr;
        uint8_t* dibBits = nullptr;
        bool windowShown = false;
        bool lastPresentEmpty = false;
        bool presentedLastFrame = false;
    };

    bool CreateDevice(bool warp);
    bool CompileShaders();
    bool CreateStates();
    bool CreateTarget(Target& t, const MonitorDesc& m);
    void DestroyTarget(Target& t);
    void ReleaseGpuTargetResources(Target& t);
    bool CreateTargetGpuResources(Target& t);
    HWND CreateOverlayWindow(const RECT& rc, bool noRedirection);
    bool EnsureBuffer(ComPtr<ID3D11Buffer>& buf, size_t& cap, size_t needBytes);
    bool Upload(ComPtr<ID3D11Buffer>& buf, size_t& cap, const void* data, size_t bytes);
    bool UploadHeights(const std::vector<float>& h);
    void DrawTarget(Target& t, const DrawLists& L);
    bool PresentTarget(Target& t, int syncInterval, bool pacing);
    void DoCapture(Target& t);

    HINSTANCE inst_ = nullptr;
    RenderMode preferred_ = RenderMode::DirectComposition;
    RenderMode mode_ = RenderMode::DirectComposition;
    bool forceWarp_ = false;
    bool deviceLost_ = false;
    bool captureExcluded_ = false;
    std::string adapterName_;

    HMODULE d3dcompiler_ = nullptr;
    HMODULE dcompDll_ = nullptr;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<IDCompositionDevice> dcomp_;
    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_10_0;

    ComPtr<ID3D11VertexShader> occVS_, pileVS_, shapeVS_, flakeVS_;
    ComPtr<ID3D11PixelShader> occPS_, pilePS_, shapePS_, flakePS_;
    ComPtr<ID3D11InputLayout> occIL_, pileIL_, shapeIL_, flakeIL_;
    ComPtr<ID3D11BlendState> blendPremul_, blendNoColor_;
    ComPtr<ID3D11DepthStencilState> dsWrite_, dsTest_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11Buffer> cb_;

    ComPtr<ID3D11Buffer> occVB_, pileVB_, shapeVB_, flakeVB_;
    size_t occCap_ = 0, pileCap_ = 0, shapeCap_ = 0, flakeCap_ = 0;

    ComPtr<ID3D11Texture2D> heightsTex_;
    ComPtr<ID3D11ShaderResourceView> heightsSRV_;
    int heightsRows_ = 0;

    std::vector<Target> targets_;
    std::vector<MonitorDesc> monitors_;

    // capture
    int captureRequest_ = -1;
    bool captureReady_ = false;
    std::vector<uint8_t> captureData_;
    int captureW_ = 0, captureH_ = 0;
};
