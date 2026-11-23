// Snowy - snowfall for your Windows desktop.
// Shared helpers: COM smart pointer, small math, RNG, noise, logging.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define SNOWY_VERSION_W L"1.0.0"
#define SNOWY_VERSION_A "1.0.0"

// ---------------------------------------------------------------------------
// Minimal COM smart pointer (works with both MSVC and MinGW headers).
// ---------------------------------------------------------------------------
template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) {
            if (o.p_) o.p_->AddRef();
            Reset();
            p_ = o.p_;
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            Reset();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }
    ComPtr& operator=(std::nullptr_t) { Reset(); return *this; }

    void Reset() {
        if (p_) {
            T* t = p_;
            p_ = nullptr;
            t->Release();
        }
    }
    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    T** Put() { Reset(); return &p_; }
    void** PutVoid() { Reset(); return reinterpret_cast<void**>(&p_); }
    explicit operator bool() const { return p_ != nullptr; }

    template <class U>
    HRESULT As(ComPtr<U>& out) const {
        if (!p_) return E_POINTER;
        return p_->QueryInterface(__uuidof(U), out.PutVoid());
    }

private:
    T* p_ = nullptr;
};

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    float Len() const { return std::sqrt(x * x + y * y); }
    float Len2() const { return x * x + y * y; }
};

inline Vec2 Rotate(Vec2 v, float a) {
    float c = std::cos(a), s = std::sin(a);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Saturate(float v) { return Clamp(v, 0.f, 1.f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2 Lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline float SmoothStep(float e0, float e1, float x) {
    float t = Saturate((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}
inline float Sign(float v) { return v < 0.f ? -1.f : 1.f; }
inline float ExpDecay(float rate, float dt) { return std::exp(-rate * dt); }

// Overshooting "pop" curve: 0 -> ~1.12 -> 1 over t in [0,1].
inline float PopCurve(float t) {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    float s = 1.70158f * 1.5f;
    float u = t - 1.f;
    return 1.f + u * u * ((s + 1.f) * u + s);
}
inline float EaseInOut(float t) {
    t = Saturate(t);
    return t * t * (3.f - 2.f * t);
}

// ---------------------------------------------------------------------------
// Random numbers (xorshift64*) and value noise.
// ---------------------------------------------------------------------------
class Rng {
public:
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s_(seed ? seed : 1) {}
    void Seed(uint64_t seed) { s_ = seed ? seed : 1; }
    uint32_t NextU32() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return (uint32_t)((s_ * 2685821657736338717ull) >> 32);
    }
    float Next01() { return (NextU32() >> 8) * (1.0f / 16777216.0f); }
    float Range(float a, float b) { return a + (b - a) * Next01(); }
    int IRange(int a, int b) {  // inclusive
        int v = a + (int)(Next01() * (float)(b - a + 1));
        return v > b ? b : v;
    }
    bool Chance(float p) { return Next01() < p; }
    float SignF() { return Next01() < 0.5f ? -1.f : 1.f; }
    float Normal() {
        float u1 = std::max(Next01(), 1e-7f), u2 = Next01();
        return std::sqrt(-2.f * std::log(u1)) * std::cos(kTwoPi * u2);
    }

private:
    uint64_t s_;
};

inline float Hash1(int i) {
    uint32_t h = (uint32_t)i * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28) + 4u)) ^ h) * 277803737u;
    h = (h >> 22) ^ h;
    return (float)(h & 0xFFFFFFu) / 16777215.0f * 2.f - 1.f;
}
// Smooth 1D value noise in [-1, 1].
inline float Noise1D(float x) {
    float fl = std::floor(x);
    int i = (int)fl;
    float f = x - fl;
    float u = f * f * (3.f - 2.f * f);
    return Hash1(i) * (1.f - u) + Hash1(i + 1) * u;
}
inline float Fbm1D(float x) {
    return Noise1D(x) * 0.6f + Noise1D(x * 2.13f + 17.f) * 0.3f + Noise1D(x * 4.37f + 41.f) * 0.1f;
}

// ---------------------------------------------------------------------------
// Misc utilities (implemented in util.cpp)
// ---------------------------------------------------------------------------
double NowSeconds();
std::wstring AppDataDir();          // %APPDATA%\Snowy (created on demand)
std::wstring LocalDataDir();        // %LOCALAPPDATA%\Snowy (created on demand)
std::wstring ExePath();
std::string Narrow(const std::wstring& w);
std::wstring Widen(const std::string& s);

void LogOpen(bool verbose);
void LogClose();
void Logf(const char* fmt, ...);

inline bool RectsIntersect(const RECT& a, const RECT& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
inline int RectW(const RECT& r) { return r.right - r.left; }
inline int RectH(const RECT& r) { return r.bottom - r.top; }
