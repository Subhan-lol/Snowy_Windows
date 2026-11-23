// Snowy - the snow simulation: falling flakes, snow piling up on windows and
// on the taskbar, icicles, chunks that slide off shaken windows, powder,
// water drops, wind, cursor interaction and the occasional snowman.
#pragma once
#include "common.h"
#include "renderer.h"
#include "settings.h"
#include "tracker.h"

#include <functional>
#include <memory>
#include <unordered_map>

// A 1-D heightfield of snow resting on a horizontal edge.
struct Pile {
    float x0 = 0;      // global x of the left edge of cell 0 (px)
    float baseY = 0;   // y of the supporting edge (px)
    float cellW = 2;   // px per cell
    std::vector<float> h;
    float maxH = 30;
    float endR = 12;   // rounding radius at the ends
    float scale = 1;
    float seed = 0;
    bool ground = false;
    float spillL = 0, spillR = 0;  // volume that fell off the ends (px^2)

    float Width() const { return (float)h.size() * cellW; }
    float X1() const { return x0 + Width(); }
    float HeightAt(float x) const;
    float SurfaceY(float x) const { return baseY - HeightAt(x); }
    float Total() const;
    float MaxHeight() const;
    void Deposit(float x, float volume, float radius);
    // Remove snow above 'levelY' within [xa, xb]; returns removed volume (px^2).
    float CutAbove(float xa, float xb, float levelY);
    // Take up to 'volume' around x; returns volume taken.
    float Take(float x, float radius, float volume);
    void Relax(float dt, float maxSlope, float smoothing);
    void Constrain();
    void Clear() { std::fill(h.begin(), h.end(), 0.f); spillL = spillR = 0; }
};

struct Icicle {
    float xRel = 0;      // from the window's left edge (px)
    float len = 0;       // current length (px)
    float target = 0;    // final length (px)
    float width = 2;     // root radius (px)
    float growRate = 1;  // px per second
    float seed = 0;
    float tilt = 0;      // tip offset (px)
    float dripTimer = 5;
    float dripGrow = 0;  // a water bead forming at the tip (0..1)
    float detachIn = -1; // >= 0: scheduled to snap off
};

struct WinSnow {
    HWND hwnd = nullptr;
    RECT rc{};
    int z = 0;
    float depth = 0.5f;
    float scale = 1;
    int mon = 0;
    float overhang = 4;
    Pile cap;
    std::vector<Icicle> icicles;
    std::vector<RECT> occ;      // windows in front of us overlapping our snow
    Vec2 vel, acc;              // smoothed motion (px/s, px/s^2)
    float slideVel = 0, slideOff = 0;
    float moveAccum = 0;
    float stillTime = 0;
    float icicleTimer = 2;
    double reversals[8] = {};
    int revHead = 0;
    int lastSignX = 0, lastSignY = 0;
    float shakeCooldown = 0;
    bool seen = false;

    bool Occluded(float x, float y) const {
        for (const RECT& r : occ)
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return true;
        return false;
    }
};

enum BodyKind : uint8_t { BK_CHUNK = 0, BK_ICICLE = 1, BK_BALL = 2, BK_SHARD = 3, BK_DROP = 4 };

struct Body {
    Vec2 p, v;
    float ang = 0, w = 0;
    float hl = 0, r = 1;   // capsule half-length & radius (icicle: length & root radius)
    float scale = 1;
    float seed = 0;
    float life = 0;
    float maxLife = 10;
    float delay = 0;
    float depth = 0;
    uint8_t kind = BK_CHUNK;
    uint8_t bounces = 0;
    uint8_t generation = 0;
    bool dead = false;
    HWND ignore = nullptr;  // window whose own snow this body must not land on
};

struct Puff {
    Vec2 p, v;
    float r = 2, dr = 4, life = 0, maxLife = 1, alpha = 0.6f, scale = 1;
};

struct LocalShape {
    Vec2 a, b;
    float ra = 1, rb = 1;
    uint8_t style = SS_FLAT, kind = 0;
    float p2 = 0, p3 = 0;
    float seed = 0;
    float col[4] = {1, 1, 1, 1};
};

struct SmPart {
    LocalShape sh;
    int group = 0;      // 0 face, 1 buttons, 2 scarf, 3 left arm, 4 right arm, 5 hat
    float appear = 0;   // seconds into the decorating phase
    Vec2 pivot;         // scale origin for the "pop" animation
};

struct Debris {
    Vec2 p, v;
    float ang = 0, w = 0;
    std::vector<LocalShape> shapes;
    float life = 0, restTime = 0, radius = 4, scale = 1;
    bool resting = false;
};

struct Flake {
    Vec2 p, v;
    float size = 2;
    float fall = 60;
    float swayAmp = 10, swayFreq = 1, swayPhase = 0;
    float rot = 0, rotSpeed = 0;
    float alpha = 1;
    float windMul = 1;
    uint8_t type = 0;   // 0 dot, 1 crystal, 2 bokeh
    uint8_t layer = 1;  // 0 far, 1 mid (lands on things), 2 near
};

struct Snowman {
    int mon = 0;
    float x = 0, baseY = 0;
    float R[3] = {};
    Vec2 pos[3];
    float rad[3] = {};
    float rot[3] = {};
    bool vis[3] = {};
    int phase = 0;
    float phaseT = 0;
    float rollFrom = 0;
    Vec2 liftFrom;
    bool decorDropped = false;
    int dir = 1;        // which way the carrot points
    int rollSide = 1;   // side the balls roll in from
    int hat = 0;        // 0 none, 1 top hat, 2 beanie
    float scarf[3] = {0.85f, 0.1f, 0.12f};
    float hatCol[3] = {0.1f, 0.1f, 0.12f};
    float decorT = 0;
    float waveT = -1, nextWave = 6;
    float blinkT = -1, nextBlink = 3;
    float age = 0;
    float lifetime = 900;
    float melt = 0;
    float seed = 0;
    float scale = 1;
    bool dead = false;
};

struct MonState {
    MonitorDesc d;
    Pile ground;
    std::vector<Flake> flakes;
    float spawnAcc = 0;
    bool active = true;
    bool plowing = false;
    float plowX = 0;
    float snowmanTimer = 0;
    bool content = false;
};

struct InputState {
    Vec2 cursor;
    bool cursorValid = false;
    HWND moveSize = nullptr;
};

class World {
public:
    World();
    void SetSettings(const Settings& s);
    void SetMonitors(const std::vector<MonitorDesc>& mons);
    void SetMonitorActive(size_t i, bool active);
    void SetTimeScale(float s) { timeScale_ = s; }
    void SetSeed(uint64_t s) { rng_.Seed(s); }
    void SetDensityMul(float m) { densityMul_ = m; }

    void SyncWindows(const std::vector<WinSnapshot>& wins, HWND moveSize, float dt,
                     const std::function<GoneReason(HWND)>& whyGone);
    void Update(float dt, const InputState& in);
    void BuildDrawLists(DrawLists& out);
    const std::vector<char>& MonitorContent() const { return monContent_; }
    bool AnythingAnimating() const;

    // Actions
    void Melt();
    void Avalanche();
    void BuildSnowmanNow();
    void Shovel();
    void ForceMoveOverride(HWND h) { moveOverride_ = h; }

    size_t FlakeCount() const;
    size_t WindowCount() const { return wins_.size(); }

private:
    // flakes
    void UpdateFlakes(MonState& m, size_t mi, float dt, const InputState& in);
    void SpawnFlake(MonState& m, Flake& f, bool initial);
    bool FindLanding(float x, float prevY, float newY, Pile*& pile, WinSnow*& owner);
    bool FindLandingIgnoring(float x, float prevY, float newY, HWND ignore, Pile*& pile, WinSnow*& owner);
    void RebuildSurfaceIndex();

    // windows
    void InitCap(WinSnow& w);
    void UpdateWindowMotion(WinSnow& w, const RECT& rc, bool inMove, float dt);
    void RemapCap(WinSnow& w, const RECT& rc);
    void ShiftCap(WinSnow& w, int cells);
    void DetachCap(WinSnow& w, Vec2 baseVel, float spread, bool slab);
    void DetachIcicles(WinSnow& w, Vec2 baseVel, bool all);
    void DropIcicle(WinSnow& w, const Icicle& ic, Vec2 vel);
    void SpawnChunksFromProfile(const float* h, int n, float x0, float cellW, float baseY, float scale, Vec2 vel,
                                float spread, bool slab, const WinSnow* owner);
    void UpdateIcicles(WinSnow& w, float dt);
    int MonitorAt(float x, float y) const;
    float DepthForZ(int z) const;

    // bodies & particles
    void UpdateBodies(float dt);
    void UpdatePuffs(float dt);
    void UpdateDebris(float dt);
    void ImpactChunk(Body& b, Pile* pile, WinSnow* owner, float surfY);
    void Shatter(const Body& b, float surfY);
    void SpawnPuffs(Vec2 p, int n, float radius, float scale, Vec2 baseVel, float speed);
    void SpawnDrop(Vec2 p, float scale, Vec2 vel);

    // piles
    void UpdatePiles(float dt);
    void EmitSpill(Pile& p, WinSnow* owner);
    void Spindrift(Pile& p, WinSnow* owner, float dt);

    // cursor
    void CursorPlay(const InputState& in, float dt);

    // snowman (snowman.cpp)
    void UpdateSnowmen(float dt, const InputState& in);
    bool StartSnowman(size_t mi, bool force);
    void BuildSnowmanShapes(const Snowman& s, std::vector<ShapeInst>& out) const;
    void KnockOver(Snowman& s, Vec2 vel);
    void SnowmanParts(const Snowman& s, std::vector<SmPart>& parts) const;
    void DropDecorAsDebris(const Snowman& s, Vec2 vel, bool includeFace);

    // drawing helpers
    void EmitShape(std::vector<ShapeInst>& out, Vec2 a, Vec2 b, float ra, float rb, int style, const float col[4],
                   float depth, float rot = 0, float seed = 0, float p2 = 0, float p3 = 0, int kind = 0) const;

    float Wind(float x, float y) const;

    Settings s_;
    std::vector<MonState> mons_;
    std::vector<char> monContent_;
    std::unordered_map<HWND, std::unique_ptr<WinSnow>> wins_;
    struct Occ {
        RECT rc;
        int z;
    };
    std::vector<Occ> occluders_;
    int zCount_ = 0;
    std::vector<Body> bodies_;
    std::vector<Body> newBodies_;
    std::vector<Puff> puffs_;
    std::vector<Debris> debris_;
    std::vector<Snowman> snowmen_;

    struct SurfRef {
        Pile* pile;
        WinSnow* owner;
    };
    std::vector<SurfRef> surfaces_;
    std::vector<std::vector<int>> buckets_;
    float bucketX0_ = 0;
    float bucketW_ = 64;

    Rng rng_;
    float time_ = 0;
    float timeScale_ = 1;
    float windBase_ = 0;
    float gustPhase_ = 0;
    float windNow_ = 0;
    float melting_ = 0;
    float avalancheDelay_ = -1;
    Vec2 cursorPrev_;
    Vec2 cursorVel_;
    bool cursorPrevValid_ = false;
    HWND moveOverride_ = nullptr;
    float densityMul_ = 1.f;
};
