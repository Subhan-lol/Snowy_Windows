// Snowy - snow simulation.
#include "world.h"

// Tunables (DIP units unless noted; multiplied by the monitor scale).
static const float kCellDip = 2.0f;          // heightfield resolution
static const float kGravity = 2300.f;        // DIP / s^2 for chunks and icicles
static const float kOverhangDip = 4.5f;      // snow overhanging the window edges
static const float kFlakeVolumeDip2 = 10.0f;  // snow volume added by one landed flake

// ---------------------------------------------------------------------------
// Pile
// ---------------------------------------------------------------------------
float Pile::HeightAt(float x) const {
    int n = (int)h.size();
    if (n == 0 || x < x0 || x > x0 + Width()) return 0.f;
    float u = (x - x0) / cellW - 0.5f;
    float fl = std::floor(u);
    int i = (int)fl;
    float f = u - fl;
    float a = (i >= 0 && i < n) ? h[i] : (i < 0 ? h[0] * 0.5f : 0.f);
    float b = (i + 1 >= 0 && i + 1 < n) ? h[i + 1] : (i + 1 >= n ? h[n - 1] * 0.5f : 0.f);
    return a + (b - a) * f;
}

float Pile::Total() const {
    float s = 0;
    for (float v : h) s += v;
    return s * cellW;
}

float Pile::MaxHeight() const {
    float m = 0;
    for (float v : h) m = std::max(m, v);
    return m;
}

void Pile::Deposit(float x, float volume, float radius) {
    int n = (int)h.size();
    if (n == 0 || volume <= 0) return;
    float c = (x - x0) / cellW - 0.5f;
    float rc = std::max(radius / cellW, 0.75f);
    int i0 = std::max(0, (int)std::floor(c - rc));
    int i1 = std::min(n - 1, (int)std::ceil(c + rc));
    if (i0 > i1) return;
    float wsum = 0;
    for (int i = i0; i <= i1; i++) {
        float d = ((float)i - c) / (rc + 0.5f);
        float w = 1.f - d * d;
        if (w > 0) wsum += w;
    }
    if (wsum <= 0) return;
    float k = volume / (cellW * wsum);
    for (int i = i0; i <= i1; i++) {
        float d = ((float)i - c) / (rc + 0.5f);
        float w = 1.f - d * d;
        if (w > 0) h[i] += w * k;
    }
}

float Pile::CutAbove(float xa, float xb, float levelY) {
    int n = (int)h.size();
    if (n == 0) return 0;
    float allowed = std::max(0.f, baseY - levelY);
    int i0 = std::max(0, (int)std::floor((xa - x0) / cellW));
    int i1 = std::min(n - 1, (int)std::floor((xb - x0) / cellW));
    float removed = 0;
    for (int i = i0; i <= i1; i++) {
        if (h[i] > allowed) {
            removed += (h[i] - allowed) * cellW;
            h[i] = allowed;
        }
    }
    return removed;
}

float Pile::Take(float x, float radius, float volume) {
    int n = (int)h.size();
    if (n == 0 || volume <= 0) return 0;
    float c = (x - x0) / cellW - 0.5f;
    float rc = std::max(radius / cellW, 0.75f);
    int i0 = std::max(0, (int)std::floor(c - rc));
    int i1 = std::min(n - 1, (int)std::ceil(c + rc));
    float avail = 0;
    for (int i = i0; i <= i1; i++) avail += h[i] * cellW;
    if (avail <= 0) return 0;
    float frac = std::min(1.f, volume / avail);
    for (int i = i0; i <= i1; i++) h[i] -= h[i] * frac;
    return avail * frac;
}

void Pile::Relax(float rate, float maxSlope, float smoothing) {
    int n = (int)h.size();
    if (n < 2) return;
    float lim = maxSlope * cellW;
    float k = 0.5f * rate;
    auto flow = [&](int i, int j) {
        float d = h[i] - h[j];
        if (d > lim) {
            float m = (d - lim) * k;
            h[i] -= m;
            h[j] += m;
        } else if (d < -lim) {
            float m = (-d - lim) * k;
            h[j] -= m;
            h[i] += m;
        }
    };
    for (int pass = 0; pass < 3; pass++) {
        if (pass & 1)
            for (int i = n - 2; i >= 0; i--) flow(i, i + 1);
        else
            for (int i = 0; i + 1 < n; i++) flow(i, i + 1);
    }
    if (smoothing > 0) {
        // Mass-conserving diffusion: fresh lumps settle into a smooth layer.
        float s = std::min(0.24f, smoothing);
        float prevFlux = 0.f;
        float hPrev = h[0];
        for (int i = 0; i + 1 < n; i++) {
            float cur = hPrev;
            float next = h[(size_t)i + 1];
            float flux = s * (cur - next);
            h[(size_t)i] = cur - flux + prevFlux;
            prevFlux = flux;
            hPrev = next;
        }
        h[(size_t)n - 1] = hPrev + prevFlux;
    }
}

void Pile::Constrain() {
    int n = (int)h.size();
    if (n == 0) return;
    float W = Width();
    for (int i = 0; i < n; i++) {
        float lim = maxH;
        bool endZone = false;
        if (!ground && endR > 0.1f) {
            float xc = ((float)i + 0.5f) * cellW;
            float dEnd = std::min(xc, W - xc);
            if (dEnd < endR) {
                float u = 1.f - dEnd / endR;
                float prof = endR * std::sqrt(std::max(0.f, 1.f - u * u));
                if (prof < lim) {
                    lim = prof;
                    endZone = true;
                }
            }
        }
        if (h[i] > lim) {
            float ex = (h[i] - lim) * cellW;
            h[i] = lim;
            if (!ground) {
                // Overflow at the rounded ends falls off; overflow on a full cap mostly compacts.
                float s = endZone ? ex : ex * 0.08f;
                if (i < n / 2)
                    spillL += s;
                else
                    spillR += s;
            }
        }
        if (h[i] < 0) h[i] = 0;
    }
}

// ---------------------------------------------------------------------------
// World setup
// ---------------------------------------------------------------------------
World::World() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    rng_.Seed((uint64_t)c.QuadPart * 0x9E3779B97F4A7C15ull);
}

void World::SetSettings(const Settings& s) {
    bool groundOn = s.snowOnTaskbar;
    s_ = s;
    (void)groundOn;
}

int World::MonitorAt(float x, float y) const {
    for (size_t i = 0; i < mons_.size(); i++) {
        const RECT& r = mons_[i].d.rc;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return (int)i;
    }
    return -1;
}

float World::DepthForZ(int z) const { return 0.05f + 0.9f * (float)(z + 1) / (float)(zCount_ + 2); }

void World::SetMonitors(const std::vector<MonitorDesc>& mons) {
    surfaces_.clear();
    for (auto& b : buckets_) b.clear();
    std::vector<MonState> old;
    old.swap(mons_);
    bool firstTime = old.empty();
    mons_.resize(mons.size());
    for (size_t i = 0; i < mons.size(); i++) {
        MonState& m = mons_[i];
        m.d = mons[i];
        float sc = m.d.scale;
        Pile& g = m.ground;
        g.ground = true;
        g.scale = sc;
        g.cellW = kCellDip * sc;
        g.x0 = (float)m.d.rc.left;
        g.baseY = (float)m.d.work.bottom;
        int n = std::max(2, (int)std::ceil(RectW(m.d.rc) / g.cellW));
        g.h.assign((size_t)n, 0.f);
        g.seed = rng_.Range(0, 100);
        g.endR = 0;
        // carry over snow and flakes from a matching monitor
        for (auto& o : old) {
            if (o.d.handle == m.d.handle || EqualRect(&o.d.rc, &m.d.rc)) {
                for (int k = 0; k < n; k++) {
                    float x = g.x0 + ((float)k + 0.5f) * g.cellW;
                    g.h[(size_t)k] = o.ground.HeightAt(x) * (sc / std::max(0.01f, o.d.scale));
                }
                if (EqualRect(&o.d.rc, &m.d.rc)) m.flakes.swap(o.flakes);
                m.snowmanTimer = o.snowmanTimer;
                break;
            }
        }
        if (m.snowmanTimer <= 0) {
            float first[3] = {420.f, 170.f, 55.f};
            m.snowmanTimer = first[std::min(2, std::max(0, s_.snowmanFreq))] * rng_.Range(0.8f, 1.25f);
        }
        if (firstTime && s_.snowing) {
            // Start with some snow already in the air.
            float areaM = (RectW(m.d.rc) / sc) * (RectH(m.d.rc) / sc) / 1e6f;
            int initial = (int)(areaM * s_.DensityPerMDip() * 0.45f);
            for (int k = 0; k < initial; k++) {
                Flake f;
                SpawnFlake(m, f, true);
                m.flakes.push_back(f);
            }
        }
    }
    // Snowmen on monitors that vanished disappear.
    for (auto& s : snowmen_)
        if (s.mon >= (int)mons_.size()) s.dead = true;
    monContent_.assign(mons_.size(), 1);
}

void World::SetMonitorActive(size_t i, bool active) {
    if (i < mons_.size()) mons_[i].active = active;
}

size_t World::FlakeCount() const {
    size_t n = 0;
    for (auto& m : mons_) n += m.flakes.size();
    return n;
}

float World::Wind(float x, float y) const {
    float sp = std::sin(x * 0.0021f + time_ * 0.31f) * std::cos(y * 0.0017f - time_ * 0.23f);
    return windNow_ + sp * (7.f + std::fabs(windNow_) * 0.25f);
}

bool World::AnythingAnimating() const {
    if (!bodies_.empty() || !puffs_.empty() || !debris_.empty() || !snowmen_.empty()) return true;
    if (melting_ > 0) return true;
    for (auto& m : mons_)
        if (!m.flakes.empty() || m.plowing) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Flakes
// ---------------------------------------------------------------------------
void World::SpawnFlake(MonState& m, Flake& f, bool initial) {
    float sc = m.d.scale;
    const RECT& r = m.d.rc;
    float sizeMul = s_.SizeMul();
    float speedMul = s_.SpeedMul();
    float roll = rng_.Next01();
    if (roll < 0.22f) {
        f.layer = 0;
        f.type = 0;
        f.size = rng_.Range(0.65f, 1.3f) * sc * sizeMul;
        f.fall = rng_.Range(22.f, 40.f) * sc * speedMul;
        f.alpha = rng_.Range(0.30f, 0.55f);
        f.windMul = 0.55f;
        f.swayAmp = rng_.Range(4.f, 10.f) * sc;
    } else if (roll < 0.984f) {
        f.layer = 1;
        bool crystal = s_.flakeStyle == 2 ? rng_.Chance(0.85f) : (s_.flakeStyle == 1 ? false : rng_.Chance(0.2f));
        if (crystal) {
            f.type = 1;
            f.size = rng_.Range(3.0f, 6.2f) * sc * sizeMul;
            f.alpha = rng_.Range(0.85f, 1.0f);
        } else {
            f.type = 0;
            float t = rng_.Next01();
            f.size = (1.1f + t * t * 2.5f) * sc * sizeMul;
            f.alpha = rng_.Range(0.72f, 1.0f);
        }
        f.fall = (rng_.Range(40.f, 60.f) + f.size / sc * 7.f) * sc * speedMul;
        f.windMul = rng_.Range(0.8f, 1.05f);
        f.swayAmp = rng_.Range(8.f, 22.f) * sc;
    } else {
        f.layer = 2;
        f.type = 2;
        f.size = rng_.Range(5.f, 10.f) * sc * sizeMul;
        f.fall = rng_.Range(110.f, 160.f) * sc * speedMul;
        f.alpha = rng_.Range(0.08f, 0.18f);
        f.windMul = 1.25f;
        f.swayAmp = rng_.Range(10.f, 25.f) * sc;
    }
    f.swayFreq = rng_.Range(0.5f, 1.4f);
    f.swayPhase = rng_.Range(0.f, kTwoPi);
    f.rot = rng_.Range(0.f, kTwoPi);
    f.rotSpeed = rng_.Range(-1.2f, 1.2f);
    float margin = 30.f * sc;
    f.p.x = rng_.Range((float)r.left - margin, (float)r.right + margin);
    f.p.y = initial ? rng_.Range((float)r.top - 10.f * sc, (float)r.bottom - 30.f * sc)
                    : (float)r.top - rng_.Range(4.f, 50.f) * sc;
    f.v = Vec2(windNow_ * sc * f.windMul, f.fall);
}

bool World::FindLanding(float x, float prevY, float newY, Pile*& pile, WinSnow*& owner) {
    if (buckets_.empty()) return false;
    int b = (int)((x - bucketX0_) / bucketW_);
    if (b < 0 || b >= (int)buckets_.size()) return false;
    float bestY = 1e30f;
    bool found = false;
    for (int si : buckets_[(size_t)b]) {
        SurfRef& s = surfaces_[(size_t)si];
        Pile& p = *s.pile;
        if (x < p.x0 || x >= p.X1()) continue;
        float sy = p.SurfaceY(x);
        bool crossed = prevY < sy && newY >= sy;
        bool inside = newY >= sy && newY <= p.baseY + 1.f && prevY <= p.baseY + 1.f;
        if (!crossed && !inside) continue;
        if (s.owner && s.owner->Occluded(x, sy)) continue;
        if (sy < bestY) {
            bestY = sy;
            pile = s.pile;
            owner = s.owner;
            found = true;
        }
    }
    return found;
}

void World::RebuildSurfaceIndex() {
    surfaces_.clear();
    for (auto& m : mons_) surfaces_.push_back({&m.ground, nullptr});
    if (s_.snowOnWindows) {
        for (auto& kv : wins_) surfaces_.push_back({&kv.second->cap, kv.second.get()});
    }
    if (mons_.empty()) {
        buckets_.clear();
        return;
    }
    float minX = 1e30f, maxX = -1e30f;
    for (auto& m : mons_) {
        minX = std::min(minX, (float)m.d.rc.left);
        maxX = std::max(maxX, (float)m.d.rc.right);
    }
    minX -= 200.f;
    maxX += 200.f;
    bucketX0_ = minX;
    size_t nb = (size_t)((maxX - minX) / bucketW_) + 1;
    if (buckets_.size() != nb) buckets_.resize(nb);
    for (auto& v : buckets_) v.clear();
    for (size_t si = 0; si < surfaces_.size(); si++) {
        const Pile& p = *surfaces_[si].pile;
        int b0 = std::max(0, (int)((p.x0 - minX) / bucketW_));
        int b1 = std::min((int)nb - 1, (int)((p.X1() - minX) / bucketW_));
        for (int b = b0; b <= b1; b++) buckets_[(size_t)b].push_back((int)si);
    }
}

void World::UpdateFlakes(MonState& m, size_t mi, float dt, const InputState& in) {
    (void)mi;
    const RECT& r = m.d.rc;
    float sc = m.d.scale;
    float margin = 30.f * sc;
    float left = (float)r.left - margin, right = (float)r.right + margin, W = right - left;
    float areaM = (RectW(r) / sc) * (RectH(r) / sc) / 1e6f;
    size_t target = (size_t)std::min(6000.f, areaM * s_.DensityPerMDip() * densityMul_);
    bool canSpawn = s_.snowing && m.active && melting_ <= 0.f;

    float cursorR = 90.f * sc;
    float cursorSpeed = cursorVel_.Len();
    bool cursorOn = s_.cursorPlay && in.cursorValid && cursorSpeed > 40.f * sc && in.cursor.x >= left &&
                    in.cursor.x <= right;
    float volumeMul = kFlakeVolumeDip2 * sc * sc * s_.AccumulationMul() * timeScale_;

    for (size_t i = 0; i < m.flakes.size();) {
        Flake& f = m.flakes[i];
        float wind = Wind(f.p.x, f.p.y) * sc * f.windMul;
        float sway = std::sin(time_ * f.swayFreq + f.swayPhase) * f.swayAmp;
        f.v.x += (wind + sway - f.v.x) * std::min(1.f, dt * 2.2f);
        f.v.y += (f.fall - f.v.y) * std::min(1.f, dt * 1.8f);
        if (cursorOn) {
            Vec2 d = f.p - in.cursor;
            float d2 = d.Len2();
            if (d2 < cursorR * cursorR) {
                float dist = std::sqrt(d2) + 0.001f;
                float fo = 1.f - dist / cursorR;
                fo *= fo;
                f.v += (cursorVel_ - f.v) * (fo * std::min(1.f, dt * 6.f));
                f.v += d * (fo * cursorSpeed * 1.1f * dt / dist);
            }
        }
        float prevY = f.p.y;
        f.p += f.v * dt;
        f.rot += f.rotSpeed * dt;
        bool dead = false;
        if (f.layer == 1) {
            Pile* pile = nullptr;
            WinSnow* owner = nullptr;
            if (FindLanding(f.p.x, prevY, f.p.y, pile, owner)) {
                if (pile->maxH > 0.5f) {
                    float sizeK = Clamp(f.size / sc / 2.2f, 0.5f, 2.2f);
                    float vol = volumeMul * sizeK;
                    pile->Deposit(f.p.x, vol, std::max(5.f * sc, 1.6f * std::sqrt(vol)));
                }
                dead = true;
            }
        }
        if (!dead && f.p.y > (float)r.bottom + 20.f * sc) dead = true;
        if (f.p.x < left)
            f.p.x += W;
        else if (f.p.x > right)
            f.p.x -= W;
        if (dead) {
            if (canSpawn && m.flakes.size() <= target) {
                SpawnFlake(m, f, false);
                ++i;
            } else {
                m.flakes[i] = m.flakes.back();
                m.flakes.pop_back();
            }
        } else {
            ++i;
        }
    }
    if (canSpawn && m.flakes.size() < target) {
        float need = (float)(target - m.flakes.size());
        float rate = (float)target * (55.f * sc * s_.SpeedMul()) / std::max(1.f, (float)RectH(r)) * 1.4f;
        m.spawnAcc += rate * dt;
        while (m.spawnAcc >= 1.f && need >= 1.f) {
            Flake f;
            SpawnFlake(m, f, false);
            m.flakes.push_back(f);
            m.spawnAcc -= 1.f;
            need -= 1.f;
        }
        if (need < 1.f) m.spawnAcc = 0;
    }
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------
void World::InitCap(WinSnow& w) {
    float sc = w.scale;
    w.overhang = kOverhangDip * sc;
    Pile& c = w.cap;
    c.cellW = kCellDip * sc;
    c.x0 = (float)w.rc.left - w.overhang;
    c.baseY = (float)w.rc.top;
    int n = std::max(2, (int)std::ceil((RectW(w.rc) + 2.f * w.overhang) / c.cellW));
    c.h.assign((size_t)n, 0.f);
    c.maxH = s_.snowOnWindows ? s_.MaxCapDip() * sc : 0.f;
    c.endR = Clamp(s_.MaxCapDip() * 0.55f, 6.f, 15.f) * sc;
    c.scale = sc;
    c.seed = rng_.Range(0.f, 100.f);
    c.ground = false;
    c.spillL = c.spillR = 0;
    w.icicleTimer = rng_.Range(1.f, 4.f);
    for (double& t : w.reversals) t = -100.0;
    w.slideOff = w.slideVel = 0;
}

void World::SyncWindows(const std::vector<WinSnapshot>& snap, HWND moveSize, float dt,
                        const std::function<GoneReason(HWND)>& whyGone) {
    for (auto& kv : wins_) kv.second->seen = false;
    occluders_.clear();
    zCount_ = (int)snap.size();
    for (const WinSnapshot& s : snap) {
        occluders_.push_back({s.rc, s.z});
        if (s.kind != WK_SURFACE) continue;
        float cx = (s.rc.left + s.rc.right) * 0.5f;
        int mi = MonitorAt(cx, (float)s.rc.top + 1.f);
        if (mi < 0) continue;
        const MonState& m = mons_[(size_t)mi];
        float sc = m.d.scale;
        bool eligible = !s.maximized && (float)s.rc.top >= (float)m.d.rc.top + 8.f * sc &&
                        (float)RectW(s.rc) >= 80.f * sc && (float)RectH(s.rc) >= 32.f * sc &&
                        (float)s.rc.top < m.ground.baseY - 20.f * sc;
        auto it = wins_.find(s.hwnd);
        if (it == wins_.end()) {
            if (!eligible) continue;
            auto w = std::make_unique<WinSnow>();
            w->hwnd = s.hwnd;
            w->rc = s.rc;
            w->z = s.z;
            w->mon = mi;
            w->scale = sc;
            InitCap(*w);
            w->seen = true;
            wins_[s.hwnd] = std::move(w);
            continue;
        }
        WinSnow& w = *it->second;
        w.seen = true;
        w.z = s.z;
        if (!eligible) {
            // Maximized, pushed off the top of the screen, etc.: the snow falls off.
            DetachCap(w, w.vel * 0.5f, 40.f * sc, true);
            DetachIcicles(w, w.vel * 0.5f, true);
            w.seen = false;
            continue;
        }
        w.mon = mi;
        if (std::fabs(w.scale - sc) > 0.01f) {
            // Moved to a monitor with a different DPI: start fresh there.
            DetachCap(w, w.vel * 0.5f, 40.f * sc, true);
            DetachIcicles(w, w.vel * 0.5f, true);
            w.scale = sc;
            w.rc = s.rc;
            InitCap(w);
            w.icicles.clear();
            continue;
        }
        bool inMove = (moveSize == s.hwnd) || (moveOverride_ == s.hwnd);
        UpdateWindowMotion(w, s.rc, inMove, dt);
    }
    for (auto it = wins_.begin(); it != wins_.end();) {
        WinSnow& w = *it->second;
        if (!w.seen) {
            GoneReason why = whyGone ? whyGone(it->first) : GoneReason::Closed;
            if (why == GoneReason::Closed || why == GoneReason::Minimized || why == GoneReason::Hidden) {
                // "Close one and it all comes down."
                DetachCap(w, Vec2(0, 0), 18.f * w.scale, true);
                DetachIcicles(w, Vec2(0, 0), true);
            }
            it = wins_.erase(it);
        } else {
            ++it;
        }
    }
    // Depth for occlusion, and the list of windows in front of each snow cap.
    float gap = 0.9f / (float)(zCount_ + 2);
    for (auto& kv : wins_) {
        WinSnow& w = *kv.second;
        w.depth = DepthForZ(w.z) - gap * 0.45f;
        w.occ.clear();
        RECT region = {(LONG)w.cap.x0 - 2, (LONG)((float)w.rc.top - w.cap.maxH - 14.f * w.scale),
                       (LONG)w.cap.X1() + 2, w.rc.top + 2};
        for (const Occ& o : occluders_) {
            if (o.z >= w.z) break;
            if (RectsIntersect(o.rc, region)) w.occ.push_back(o.rc);
        }
    }
    RebuildSurfaceIndex();
}

void World::UpdateWindowMotion(WinSnow& w, const RECT& rc, bool inMove, float dt) {
    const RECT o = w.rc;
    float sc = w.scale;
    int dl = rc.left - o.left, dr = rc.right - o.right, dtp = rc.top - o.top, db = rc.bottom - o.bottom;
    float idt = 1.f / std::max(dt, 0.001f);
    w.shakeCooldown = std::max(0.f, w.shakeCooldown - dt);

    if (dl == 0 && dr == 0 && dtp == 0 && db == 0) {
        w.vel *= ExpDecay(20.f, dt);
        w.acc = Vec2(0, 0);
        w.stillTime += dt;
        if (w.stillTime > 0.25f) {
            w.moveAccum = 0;
            w.lastSignX = w.lastSignY = 0;
        }
        w.slideVel *= ExpDecay(12.f, dt);
        return;
    }
    w.stillTime = 0;
    int ow = RectW(o), oh = RectH(o), nw = RectW(rc), nh = RectH(rc);
    bool sizeChanged = ow != nw || oh != nh;
    float disp = (float)std::max(std::max(std::abs(dl), std::abs(dr)), std::max(std::abs(dtp), std::abs(db)));
    bool jump = false;
    if (!inMove && disp > 120.f * sc) jump = true;
    if (sizeChanged && disp > 40.f * sc &&
        (std::abs(nw - ow) > (int)(ow * 0.25f) || std::abs(nh - oh) > (int)(oh * 0.25f)))
        jump = true;
    if (jump) {
        // Snapped / maximized / teleported: the snow drops where the window used to be.
        DetachCap(w, Vec2(0, 0), 25.f * sc, true);
        DetachIcicles(w, Vec2(0, 0), true);
        w.rc = rc;
        InitCap(w);
        w.icicles.clear();
        w.vel = w.acc = Vec2(0, 0);
        return;
    }

    if (!sizeChanged) {
        Vec2 d((float)dl, (float)dtp);
        w.cap.x0 += d.x;
        w.cap.baseY += d.y;
        Vec2 vInst = d * idt;
        Vec2 prevVel = w.vel;
        w.vel = w.vel + (vInst - w.vel) * std::min(1.f, dt / 0.04f);
        Vec2 aInst = (w.vel - prevVel) * idt;
        w.acc = w.acc + (aInst - w.acc) * std::min(1.f, dt / 0.05f);
        w.moveAccum += d.Len();

        // Shake detection: several fast direction reversals within a second.
        float vthr = 280.f * sc;
        auto reversal = [&](int& lastSign, float v) {
            if (std::fabs(v) < vthr) return;
            int sg = v > 0 ? 1 : -1;
            if (lastSign != 0 && sg != lastSign) w.reversals[(w.revHead++) & 7] = (double)time_;
            lastSign = sg;
        };
        reversal(w.lastSignX, w.vel.x);
        reversal(w.lastSignY, w.vel.y);
        int recent = 0;
        for (double t : w.reversals)
            if ((double)time_ - t < 1.0) recent++;
        if (recent >= 3 && w.shakeCooldown <= 0.f) {
            Vec2 v = w.vel * 0.85f + Vec2(0, -140.f * sc);
            DetachCap(w, v, 170.f * sc, false);
            DetachIcicles(w, w.vel * 0.6f, true);
            w.shakeCooldown = 0.6f;
            for (double& t : w.reversals) t = -100.0;
        }

        // Inertia: when the window is jerked hard, the snow lags behind and slides off the end.
        float thr = 16000.f * sc;
        float ax = w.acc.x;
        if (std::fabs(ax) > thr) w.slideVel -= (ax - Sign(ax) * thr) * dt * 0.30f;
        w.slideVel *= ExpDecay(7.f, dt);
        w.slideOff += w.slideVel * dt;
        int cells = (int)(w.slideOff / w.cap.cellW);
        if (cells != 0) {
            ShiftCap(w, cells);
            w.slideOff -= (float)cells * w.cap.cellW;
        }

        // "Move a window and its icicles snap off."
        if (w.moveAccum > 3.f * sc) {
            for (auto& ic : w.icicles)
                if (ic.detachIn < 0) ic.detachIn = rng_.Range(0.f, 0.12f);
        }
    } else {
        // Resizing: snow stays put in screen space, edges move underneath it.
        if (dl != 0 || dr != 0) RemapCap(w, rc);
        if (dtp != 0) w.cap.baseY = (float)rc.top;
        for (auto& ic : w.icicles) {
            float ax = (float)o.left + ic.xRel;
            bool outside = ax < (float)rc.left + 2.f * sc || ax > (float)rc.right - 2.f * sc;
            if ((db != 0 || outside) && ic.detachIn < 0) ic.detachIn = rng_.Range(0.f, 0.05f);
            ic.xRel = ax - (float)rc.left;
        }
        w.vel = Vec2(0, 0);
        w.acc = Vec2(0, 0);
    }
    w.rc = rc;
}

void World::RemapCap(WinSnow& w, const RECT& rc) {
    Pile& c = w.cap;
    float newX0 = (float)rc.left - w.overhang;
    int newN = std::max(2, (int)std::ceil((RectW(rc) + 2.f * w.overhang) / c.cellW));
    std::vector<float> nh((size_t)newN, 0.f);
    std::vector<float> spill(c.h.size(), 0.f);
    bool anySpill = false;
    for (size_t i = 0; i < c.h.size(); i++) {
        float x = c.x0 + ((float)i + 0.5f) * c.cellW;
        int j = (int)std::floor((x - newX0) / c.cellW);
        if (j >= 0 && j < newN) {
            nh[(size_t)j] = std::max(nh[(size_t)j], c.h[i]);
        } else if (c.h[i] > 0.5f * c.scale) {
            spill[i] = c.h[i];
            anySpill = true;
        }
    }
    if (anySpill)
        SpawnChunksFromProfile(spill.data(), (int)spill.size(), c.x0, c.cellW, c.baseY, c.scale, Vec2(0, 0),
                               30.f * c.scale, true, nullptr);
    c.h.swap(nh);
    c.x0 = newX0;
}

void World::ShiftCap(WinSnow& w, int cells) {
    Pile& c = w.cap;
    int n = (int)c.h.size();
    if (cells == 0 || n == 0) return;
    int k = std::min(std::abs(cells), n);
    std::vector<float> spill;
    Vec2 v = w.vel + Vec2(w.slideVel, 0);
    if (cells > 0) {
        spill.assign(c.h.end() - k, c.h.end());
        for (int i = n - 1; i >= k; i--) c.h[(size_t)i] = c.h[(size_t)(i - k)];
        for (int i = 0; i < k; i++) c.h[(size_t)i] = 0;
        SpawnChunksFromProfile(spill.data(), k, c.X1(), c.cellW, c.baseY, c.scale, v, 50.f * c.scale, false, nullptr);
    } else {
        spill.assign(c.h.begin(), c.h.begin() + k);
        for (int i = 0; i + k < n; i++) c.h[(size_t)i] = c.h[(size_t)(i + k)];
        for (int i = n - k; i < n; i++) c.h[(size_t)i] = 0;
        SpawnChunksFromProfile(spill.data(), k, c.x0 - (float)k * c.cellW, c.cellW, c.baseY, c.scale, v,
                               50.f * c.scale, false, nullptr);
    }
}

void World::SpawnChunksFromProfile(const float* h, int n, float x0, float cellW, float baseY, float sc, Vec2 vel,
                                   float spread, bool slab, const WinSnow* owner) {
    float minH = 1.3f * sc;
    HWND ignore = owner ? owner->hwnd : nullptr;
    int i = 0;
    while (i < n) {
        float xc = x0 + ((float)i + 0.5f) * cellW;
        if (h[i] < minH || (owner && owner->Occluded(xc, baseY - h[i] * 0.5f))) {
            if (h[i] > 0.5f * sc && rng_.Chance(0.06f) && !(owner && owner->Occluded(xc, baseY - 1.f)))
                SpawnPuffs(Vec2(xc, baseY - h[i] * 0.5f), 1, h[i], sc, vel * 0.5f, 30.f * sc);
            i++;
            continue;
        }
        float segLen = Clamp(h[i] * rng_.Range(1.7f, 3.6f), 6.f * sc, 42.f * sc);
        int segCells = std::max(2, (int)(segLen / cellW));
        int j = std::min(n, i + segCells);
        float sum = 0, cnt = 0;
        for (int k = i; k < j; k++) {
            if (h[k] >= minH * 0.5f) {
                sum += h[k];
                cnt += 1.f;
            }
        }
        if (cnt < 1.f) {
            i = j;
            continue;
        }
        float avgH = sum / cnt;
        float len = (float)(j - i) * cellW;
        Body b;
        b.kind = BK_CHUNK;
        b.r = std::max(avgH * 0.52f, 1.0f * sc);
        b.hl = std::max(0.f, len * 0.5f - b.r * 0.8f);
        b.p = Vec2(x0 + (float)(i + j) * 0.5f * cellW, baseY - avgH * 0.5f);
        b.v = vel + Vec2(rng_.Range(-spread, spread), rng_.Range(-spread, spread * 0.35f));
        b.ang = rng_.Range(-0.06f, 0.06f);
        b.w = slab ? rng_.Range(-1.0f, 1.0f) : rng_.Range(-5.5f, 5.5f);
        b.scale = sc;
        b.seed = rng_.Range(0.f, 100.f);
        b.delay = slab ? rng_.Range(0.04f, 0.13f) : 0.f;
        b.maxLife = 30.f;
        b.ignore = ignore;
        newBodies_.push_back(b);
        i = j;
    }
}

void World::DetachCap(WinSnow& w, Vec2 baseVel, float spread, bool slab) {
    Pile& c = w.cap;
    if (c.h.empty()) return;
    SpawnChunksFromProfile(c.h.data(), (int)c.h.size(), c.x0, c.cellW, c.baseY, w.scale, baseVel, spread, slab, &w);
    c.Clear();
}

void World::DropIcicle(WinSnow& w, const Icicle& ic, Vec2 vel) {
    if (ic.len < 1.5f * w.scale) return;
    if (w.Occluded((float)w.rc.left + ic.xRel, (float)w.rc.bottom + ic.len * 0.5f)) return;
    Body b;
    b.kind = BK_ICICLE;
    b.scale = w.scale;
    b.hl = ic.len;
    b.r = ic.width;
    b.ang = std::atan2(-ic.tilt, ic.len);
    b.p = Vec2((float)w.rc.left + ic.xRel + ic.tilt * 0.35f, (float)w.rc.bottom + ic.len * 0.35f);
    b.v = vel + Vec2(rng_.Range(-20.f, 20.f) * w.scale, rng_.Range(0.f, 30.f) * w.scale);
    b.w = rng_.Range(-1.6f, 1.6f);
    b.seed = ic.seed;
    b.maxLife = 30.f;
    b.ignore = w.hwnd;
    newBodies_.push_back(b);
}

void World::DetachIcicles(WinSnow& w, Vec2 baseVel, bool all) {
    (void)all;
    for (auto& ic : w.icicles) DropIcicle(w, ic, baseVel);
    w.icicles.clear();
}

void World::UpdateIcicles(WinSnow& w, float dt) {
    float sc = w.scale;
    for (size_t i = 0; i < w.icicles.size();) {
        Icicle& ic = w.icicles[i];
        if (ic.detachIn >= 0) {
            ic.detachIn -= dt;
            if (ic.detachIn < 0) {
                Icicle copy = ic;
                w.icicles[i] = w.icicles.back();
                w.icicles.pop_back();
                DropIcicle(w, copy, w.vel * 0.6f);
                continue;
            }
        }
        ++i;
    }
    bool meltIcicles = !s_.icicles || melting_ > 0;
    if (meltIcicles) {
        for (auto& ic : w.icicles) ic.len -= (melting_ > 0 ? 14.f : 6.f) * sc * dt;
        w.icicles.erase(std::remove_if(w.icicles.begin(), w.icicles.end(),
                                       [](const Icicle& ic) { return ic.len <= 0.f; }),
                        w.icicles.end());
        return;
    }
    float capFill = w.cap.maxH > 0 ? w.cap.Total() / std::max(1.f, w.cap.Width() * w.cap.maxH) : 0.f;
    if (w.stillTime > 1.2f && capFill > 0.06f) {
        float rate = timeScale_ * (0.4f + capFill * 1.2f);
        for (auto& ic : w.icicles)
            if (ic.len < ic.target && ic.detachIn < 0) ic.len = std::min(ic.target, ic.len + ic.growRate * rate * dt);
        w.icicleTimer -= dt * rate;
        int maxCount = (int)((float)RectW(w.rc) / (15.f * sc));
        if (w.icicleTimer <= 0 && (int)w.icicles.size() < maxCount) {
            w.icicleTimer = rng_.Range(1.2f, 4.5f);
            for (int attempt = 0; attempt < 6; attempt++) {
                float margin = 12.f * sc;
                float xr = rng_.Range(margin, (float)RectW(w.rc) - margin);
                bool ok = true;
                for (auto& o2 : w.icicles)
                    if (std::fabs(o2.xRel - xr) < 7.f * sc) {
                        ok = false;
                        break;
                    }
                if (!ok) continue;
                Icicle ic;
                ic.xRel = xr;
                float r = rng_.Next01();
                float L = r < 0.55f ? rng_.Range(4.f, 11.f) : (r < 0.9f ? rng_.Range(11.f, 24.f) : rng_.Range(24.f, 46.f));
                ic.target = L * sc;
                ic.len = 0.6f * sc;
                ic.width = Clamp(1.4f + L * 0.10f, 1.6f, 5.5f) * sc;
                ic.growRate = rng_.Range(0.8f, 2.0f) * sc;
                ic.seed = rng_.Range(0.f, 100.f);
                ic.tilt = rng_.Range(-0.6f, 0.6f) * sc;
                ic.dripTimer = rng_.Range(3.f, 12.f);
                w.icicles.push_back(ic);
                break;
            }
        }
    }
    // Water drips from the longer icicles.
    for (auto& ic : w.icicles) {
        if (ic.len < 12.f * sc || ic.detachIn >= 0) continue;
        if (ic.dripGrow <= 0) {
            ic.dripTimer -= dt;
            if (ic.dripTimer <= 0) ic.dripGrow = 0.01f;
        } else {
            ic.dripGrow += dt / 0.9f;
            if (ic.dripGrow >= 1.f) {
                Vec2 tip((float)w.rc.left + ic.xRel + ic.tilt, (float)w.rc.bottom + ic.len + 1.f * sc);
                if (!w.Occluded(tip.x, tip.y)) SpawnDrop(tip, sc, Vec2(0, 20.f * sc));
                ic.dripGrow = 0;
                ic.dripTimer = rng_.Range(5.f, 16.f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Bodies & particles
// ---------------------------------------------------------------------------
static Vec2 LowestPoint(const Body& b) {
    if (b.kind == BK_ICICLE) {
        Vec2 dir = Rotate(Vec2(0, 1), b.ang);
        Vec2 root = b.p - dir * (b.hl * 0.35f);
        Vec2 tip = b.p + dir * (b.hl * 0.65f);
        Vec2 lo = root.y + b.r > tip.y ? root + Vec2(0, b.r) : tip;
        return lo;
    }
    if (b.kind == BK_CHUNK || b.kind == BK_BALL) {
        Vec2 dir(std::cos(b.ang), std::sin(b.ang));
        Vec2 e1 = b.p + dir * b.hl, e2 = b.p - dir * b.hl;
        Vec2 lo = e1.y > e2.y ? e1 : e2;
        return lo + Vec2(0, b.r);
    }
    return b.p + Vec2(0, b.r);
}

bool World::FindLandingIgnoring(float x, float prevY, float newY, HWND ignore, Pile*& pile, WinSnow*& owner) {
    if (!FindLanding(x, prevY, newY, pile, owner)) return false;
    if (ignore && owner && owner->hwnd == ignore) {
        // Retry without the ignored window's own cap: temporarily look below it.
        if (buckets_.empty()) return false;
        int b = (int)((x - bucketX0_) / bucketW_);
        if (b < 0 || b >= (int)buckets_.size()) return false;
        float bestY = 1e30f;
        bool found = false;
        for (int si : buckets_[(size_t)b]) {
            SurfRef& s = surfaces_[(size_t)si];
            if (s.owner && s.owner->hwnd == ignore) continue;
            Pile& p = *s.pile;
            if (x < p.x0 || x >= p.X1()) continue;
            float sy = p.SurfaceY(x);
            bool crossed = prevY < sy && newY >= sy;
            bool inside = newY >= sy && newY <= p.baseY + 1.f && prevY <= p.baseY + 1.f;
            if (!crossed && !inside) continue;
            if (s.owner && s.owner->Occluded(x, sy)) continue;
            if (sy < bestY) {
                bestY = sy;
                pile = s.pile;
                owner = s.owner;
                found = true;
            }
        }
        return found;
    }
    return true;
}

void World::SpawnPuffs(Vec2 p, int n, float radius, float sc, Vec2 baseVel, float speed) {
    for (int k = 0; k < n; k++) {
        if (puffs_.size() > 2500) return;
        Puff q;
        q.p = p + Vec2(rng_.Range(-radius, radius), rng_.Range(-radius * 0.5f, radius * 0.2f));
        float ang = rng_.Range(-kPi, 0.f);
        float sp = rng_.Range(0.3f, 1.f) * speed;
        q.v = baseVel + Vec2(std::cos(ang) * sp, std::sin(ang) * sp);
        q.r = rng_.Range(1.8f, 3.6f) * sc + radius * 0.22f;
        q.dr = rng_.Range(4.f, 10.f) * sc;
        q.maxLife = rng_.Range(0.45f, 1.0f);
        q.alpha = rng_.Range(0.40f, 0.70f);
        q.scale = sc;
        puffs_.push_back(q);
    }
}

void World::SpawnDrop(Vec2 p, float sc, Vec2 vel) {
    Body d;
    d.kind = BK_DROP;
    d.p = p;
    d.v = vel;
    d.r = 1.35f * sc;
    d.scale = sc;
    d.maxLife = 8.f;
    newBodies_.push_back(d);
}

void World::ImpactChunk(Body& b, Pile* pile, WinSnow* owner, float sy) {
    (void)owner;
    float sc = b.scale;
    float speed = b.v.Len();
    float area = kPi * b.r * b.r + 4.f * b.hl * b.r;
    bool canBreak = b.generation < 2 && b.r > 3.5f * sc && speed > 420.f * sc;
    if (canBreak) {
        int pieces = rng_.IRange(2, 3);
        Vec2 dir(std::cos(b.ang), std::sin(b.ang));
        for (int k = 0; k < pieces; k++) {
            Body c = b;
            c.generation = (uint8_t)(b.generation + 1);
            c.r = b.r * rng_.Range(0.55f, 0.72f);
            c.hl = b.hl * rng_.Range(0.15f, 0.45f);
            float off = ((float)k - (float)(pieces - 1) * 0.5f) * (b.hl + b.r) * 0.7f;
            c.p = b.p + dir * off;
            c.p.y = std::min(c.p.y, sy - c.r - 1.f);
            c.v = Vec2(b.v.x * 0.45f + rng_.Range(-130.f, 130.f) * sc, -std::fabs(b.v.y) * rng_.Range(0.10f, 0.26f));
            c.w = rng_.Range(-9.f, 9.f);
            c.delay = 0;
            c.ignore = nullptr;
            newBodies_.push_back(c);
        }
        if (pile->maxH > 0.5f) pile->Deposit(b.p.x, area * 0.22f, std::max(10.f * sc, (b.hl + b.r) * 2.0f));
        SpawnPuffs(Vec2(b.p.x, sy), 5, b.r, sc, Vec2(b.v.x * 0.2f, -50.f * sc), 90.f * sc);
    } else {
        if (pile->maxH > 0.5f) pile->Deposit(b.p.x, area, std::max(10.f * sc, (b.hl + b.r) * 2.0f));
        SpawnPuffs(Vec2(b.p.x, sy), 2 + (int)(b.r / sc / 3.f), b.r, sc, Vec2(b.v.x * 0.15f, -35.f * sc), 60.f * sc);
    }
    b.dead = true;
}

void World::Shatter(const Body& b, float sy) {
    float sc = b.scale;
    Vec2 tip = LowestPoint(b);
    int n = std::min(12, 4 + (int)(b.hl / sc / 5.f));
    for (int k = 0; k < n; k++) {
        Body s;
        s.kind = BK_SHARD;
        s.scale = sc;
        s.p = Vec2(tip.x + rng_.Range(-2.f, 2.f) * sc, sy - 1.5f * sc);
        float ang = rng_.Range(-kPi * 0.92f, -kPi * 0.08f);
        float sp = rng_.Range(80.f, 300.f) * sc;
        s.v = Vec2(std::cos(ang) * sp, std::sin(ang) * sp);
        s.hl = rng_.Range(1.0f, 3.5f) * sc;
        s.r = rng_.Range(0.5f, 1.1f) * sc;
        s.ang = rng_.Range(0.f, kTwoPi);
        s.w = rng_.Range(-15.f, 15.f);
        s.maxLife = rng_.Range(0.55f, 1.1f);
        s.seed = rng_.Range(0.f, 100.f);
        newBodies_.push_back(s);
    }
    SpawnPuffs(Vec2(tip.x, sy), 2, 3.f * sc, sc, Vec2(0, -30.f * sc), 45.f * sc);
}

void World::UpdateBodies(float dt) {
    float maxY = -1e30f;
    for (auto& m : mons_) maxY = std::max(maxY, (float)m.d.rc.bottom);
    int steps = std::max(1, (int)std::ceil(dt / (1.f / 120.f)));
    float h = dt / (float)steps;
    for (Body& b : bodies_) {
        if (b.dead) continue;
        b.life += dt;
        if (b.delay > 0) {
            b.delay -= dt;
            if (b.delay > 0) continue;
        }
        float g = kGravity * b.scale;
        bool resting = b.kind == BK_SHARD && b.bounces >= 2;
        for (int s = 0; s < steps && !b.dead && !resting; s++) {
            Vec2 prevLow = LowestPoint(b);
            float drag = b.kind == BK_SHARD ? 1.2f : (b.kind == BK_DROP ? 0.25f : 0.55f);
            b.v.y += g * h;
            b.v *= ExpDecay(drag, h);
            b.p += b.v * h;
            b.ang += b.w * h;
            Vec2 low = LowestPoint(b);
            Pile* pile = nullptr;
            WinSnow* owner = nullptr;
            if (FindLandingIgnoring(low.x, prevLow.y, low.y, b.ignore, pile, owner)) {
                float sy = pile->SurfaceY(low.x);
                switch (b.kind) {
                case BK_CHUNK:
                case BK_BALL:
                    ImpactChunk(b, pile, owner, sy);
                    break;
                case BK_ICICLE:
                    Shatter(b, sy);
                    b.dead = true;
                    break;
                case BK_SHARD:
                    b.p.y -= (low.y - sy);
                    if (b.bounces == 0 && b.v.y > 70.f * b.scale) {
                        b.v.y *= -0.3f;
                        b.v.x *= 0.5f;
                        b.w *= 0.5f;
                        b.bounces = 1;
                    } else {
                        b.v = Vec2(0, 0);
                        b.w = 0;
                        b.bounces = 2;
                        resting = true;
                    }
                    break;
                case BK_DROP:
                    if (b.generation == 0) {
                        for (int k = 0; k < 2; k++) {
                            Body d;
                            d.kind = BK_DROP;
                            d.generation = 1;
                            d.p = Vec2(low.x, sy - 1.f * b.scale);
                            d.v = Vec2(rng_.Range(-50.f, 50.f), -rng_.Range(40.f, 90.f)) * b.scale;
                            d.r = 0.6f * b.scale;
                            d.scale = b.scale;
                            d.maxLife = 0.35f;
                            newBodies_.push_back(d);
                        }
                    }
                    b.dead = true;
                    break;
                }
            }
            if (b.p.y > maxY + 200.f) b.dead = true;
        }
        if ((b.kind == BK_SHARD || b.kind == BK_DROP) && b.life > b.maxLife) b.dead = true;
        if (b.life > 60.f) b.dead = true;
    }
    bodies_.erase(std::remove_if(bodies_.begin(), bodies_.end(), [](const Body& b) { return b.dead; }),
                  bodies_.end());
}

void World::UpdatePuffs(float dt) {
    for (Puff& q : puffs_) {
        float sc = q.scale;
        q.v.y += 220.f * sc * dt;
        q.v *= ExpDecay(3.2f, dt);
        q.v.x += (Wind(q.p.x, q.p.y) * sc * 0.7f - q.v.x) * std::min(1.f, dt * 0.8f);
        q.p += q.v * dt;
        q.r += q.dr * dt;
        q.life += dt;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const Puff& q) { return q.life >= q.maxLife; }),
                 puffs_.end());
}

void World::UpdateDebris(float dt) {
    for (Debris& d : debris_) {
        d.life += dt;
        if (d.resting) {
            d.restTime += dt;
            continue;
        }
        Vec2 prevLow = d.p + Vec2(0, d.radius);
        d.v.y += kGravity * d.scale * dt;
        d.v *= ExpDecay(0.5f, dt);
        d.p += d.v * dt;
        d.ang += d.w * dt;
        Vec2 low = d.p + Vec2(0, d.radius);
        Pile* pile = nullptr;
        WinSnow* owner = nullptr;
        if (FindLanding(low.x, prevLow.y, low.y, pile, owner)) {
            float sy = pile->SurfaceY(low.x);
            d.p.y = sy - d.radius * 0.55f;
            if (d.v.y > 250.f * d.scale) {
                d.v = Vec2(d.v.x * 0.4f, -d.v.y * 0.22f);
                d.w *= 0.5f;
                SpawnPuffs(Vec2(low.x, sy), 2, d.radius, d.scale, Vec2(0, -20.f * d.scale), 40.f * d.scale);
            } else {
                d.resting = true;
                d.v = Vec2(0, 0);
                d.w = 0;
            }
        }
        if (d.life > 40.f) d.restTime = 100.f;
    }
    debris_.erase(std::remove_if(debris_.begin(), debris_.end(), [](const Debris& d) { return d.restTime > 9.f; }),
                  debris_.end());
}

// ---------------------------------------------------------------------------
// Piles
// ---------------------------------------------------------------------------
void World::EmitSpill(Pile& p, WinSnow* owner) {
    float sc = p.scale;
    float unit = 30.f * sc * sc;
    Vec2 v = owner ? owner->vel * 0.5f : Vec2(0, 0);
    while (p.spillL > unit) {
        float area = std::min(p.spillL, 260.f * sc * sc);
        p.spillL -= area;
        float r = std::sqrt(area / kPi);
        float hEnd = p.h.empty() ? 0.f : p.h[0];
        Vec2 pos(p.x0 - r * 0.6f, p.baseY - std::max(hEnd, r));
        if (owner && owner->Occluded(pos.x, pos.y)) continue;
        Body b;
        b.kind = BK_CHUNK;
        b.r = r;
        b.hl = r * rng_.Range(0.f, 0.6f);
        b.p = pos;
        b.v = v + Vec2(-rng_.Range(10.f, 45.f) * sc, -rng_.Range(0.f, 20.f) * sc);
        b.w = rng_.Range(-3.f, 3.f);
        b.scale = sc;
        b.seed = rng_.Range(0.f, 100.f);
        b.ignore = owner ? owner->hwnd : nullptr;
        newBodies_.push_back(b);
    }
    while (p.spillR > unit) {
        float area = std::min(p.spillR, 260.f * sc * sc);
        p.spillR -= area;
        float r = std::sqrt(area / kPi);
        float hEnd = p.h.empty() ? 0.f : p.h.back();
        Vec2 pos(p.X1() + r * 0.6f, p.baseY - std::max(hEnd, r));
        if (owner && owner->Occluded(pos.x, pos.y)) continue;
        Body b;
        b.kind = BK_CHUNK;
        b.r = r;
        b.hl = r * rng_.Range(0.f, 0.6f);
        b.p = pos;
        b.v = v + Vec2(rng_.Range(10.f, 45.f) * sc, -rng_.Range(0.f, 20.f) * sc);
        b.w = rng_.Range(-3.f, 3.f);
        b.scale = sc;
        b.seed = rng_.Range(0.f, 100.f);
        b.ignore = owner ? owner->hwnd : nullptr;
        newBodies_.push_back(b);
    }
}

void World::Spindrift(Pile& p, WinSnow* owner, float dt) {
    float wind = std::fabs(windNow_);
    const float thr = 55.f;
    if (wind < thr || p.h.empty()) return;
    float sc = p.scale;
    float expected = (wind - thr) * 0.004f * (p.Width() / sc) / 100.f * dt;
    int n = (int)p.h.size();
    while (rng_.Next01() < expected) {
        expected -= 1.f;
        int i = rng_.IRange(0, n - 1);
        if (p.h[(size_t)i] < 2.f * sc) continue;
        float x = p.x0 + ((float)i + 0.5f) * p.cellW;
        float y = p.baseY - p.h[(size_t)i];
        if (owner && owner->Occluded(x, y)) continue;
        p.h[(size_t)i] -= 0.5f * sc;
        Puff q;
        q.p = Vec2(x, y - 1.f * sc);
        q.v = Vec2(windNow_ * sc * rng_.Range(0.9f, 1.4f), -rng_.Range(10.f, 45.f) * sc);
        q.r = rng_.Range(1.4f, 2.8f) * sc;
        q.dr = rng_.Range(2.f, 5.f) * sc;
        q.maxLife = rng_.Range(1.0f, 1.8f);
        q.alpha = rng_.Range(0.25f, 0.45f);
        q.scale = sc;
        puffs_.push_back(q);
    }
}

void World::UpdatePiles(float dt) {
    float relax = std::min(1.f, dt * 30.f);
    float meltRate = 0;
    if (melting_ > 0) meltRate = 1.f;
    for (auto& m : mons_) {
        Pile& g = m.ground;
        float sc = g.scale;
        float target = s_.snowOnTaskbar ? s_.MaxGroundDip() * sc : 0.f;
        // Lower the cap gently when the setting shrinks.
        if (g.maxH > target) g.maxH = std::max(target, g.maxH - 60.f * sc * dt);
        else g.maxH = target;
        if (meltRate > 0) {
            for (float& v : g.h) v = std::max(0.f, v - (8.f + v * 0.6f) * sc * dt / std::max(sc, 0.01f));
        }
        if (m.plowing) {
            float x0 = m.plowX;
            m.plowX += 950.f * sc * dt;
            float removed = g.CutAbove(x0 - 4.f * sc, m.plowX, g.baseY + 1.f);
            if (removed > 0) {
                float top = g.SurfaceY(m.plowX + 6.f * sc);
                int nP = std::min(8, 1 + (int)(removed / (60.f * sc * sc)));
                SpawnPuffs(Vec2(m.plowX, top), nP, 6.f * sc, sc, Vec2(380.f * sc, -260.f * sc), 160.f * sc);
                if (removed > 120.f * sc * sc && rng_.Chance(0.6f)) {
                    Body b;
                    b.kind = BK_CHUNK;
                    b.r = std::min(10.f * sc, std::sqrt(removed * 0.3f / kPi));
                    b.hl = b.r * rng_.Range(0.2f, 0.9f);
                    b.p = Vec2(m.plowX + 4.f * sc, top - b.r);
                    b.v = Vec2(rng_.Range(250.f, 520.f) * sc, -rng_.Range(300.f, 620.f) * sc);
                    b.w = rng_.Range(-8.f, 8.f);
                    b.scale = sc;
                    b.seed = rng_.Range(0.f, 100.f);
                    newBodies_.push_back(b);
                }
            }
            if (m.plowX > (float)m.d.rc.right + 30.f * sc) m.plowing = false;
        }
        g.Relax(relax, 0.5f, 5.0f * dt);
        g.Constrain();
        Spindrift(g, nullptr, dt);
    }
    for (auto& kv : wins_) {
        WinSnow& w = *kv.second;
        Pile& c = w.cap;
        float sc = c.scale;
        float target = s_.snowOnWindows ? s_.MaxCapDip() * sc : 0.f;
        if (c.maxH > target) c.maxH = std::max(target, c.maxH - 40.f * sc * dt);
        else c.maxH = target;
        c.endR = Clamp(s_.MaxCapDip() * 0.55f, 6.f, 15.f) * sc;
        if (meltRate > 0) {
            float before = c.Total();
            for (float& v : c.h) v = std::max(0.f, v - (5.f + v * 0.5f) * dt);
            // drips along the bottom edge while melting
            if (before > 0 && rng_.Chance(std::min(1.f, (float)RectW(w.rc) / sc / 900.f * dt * 25.f))) {
                float x = (float)w.rc.left + rng_.Range(8.f * sc, (float)RectW(w.rc) - 8.f * sc);
                if (!w.Occluded(x, (float)w.rc.bottom + 2.f)) SpawnDrop(Vec2(x, (float)w.rc.bottom + 1.f), sc, Vec2(0, 0));
            }
        }
        c.Relax(relax, 0.8f, 6.0f * dt);
        c.Constrain();
        if (!s_.snowOnWindows) c.spillL = c.spillR = 0;
        EmitSpill(c, &w);
        Spindrift(c, &w, dt);
        UpdateIcicles(w, dt);
    }
}

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------
void World::CursorPlay(const InputState& in, float dt) {
    if (!in.cursorValid) {
        cursorPrevValid_ = false;
        cursorVel_ = Vec2(0, 0);
        return;
    }
    if (cursorPrevValid_) {
        int cm = MonitorAt(in.cursor.x, in.cursor.y);
        float csc = cm >= 0 ? mons_[(size_t)cm].d.scale : 1.f;
        if ((in.cursor - cursorPrev_).Len() > 350.f * csc) {
            // Teleport (SetCursorPos, monitor hop...): not a real swipe.
            cursorPrev_ = in.cursor;
            cursorVel_ = Vec2(0, 0);
            return;
        }
        Vec2 vInst = (in.cursor - cursorPrev_) * (1.f / std::max(dt, 0.001f));
        cursorVel_ = cursorVel_ + (vInst - cursorVel_) * std::min(1.f, dt / 0.03f);
    }
    if (s_.cursorPlay && cursorPrevValid_ && !buckets_.empty()) {
        int mi = MonitorAt(in.cursor.x, in.cursor.y);
        float sc = mi >= 0 ? mons_[(size_t)mi].d.scale : 1.f;
        float speed = cursorVel_.Len();
        if (speed > 140.f * sc) {
            float rad = 7.f * sc;
            Vec2 a = cursorPrev_, b = in.cursor;
            int steps = std::min(24, std::max(1, (int)((b - a).Len() / rad)));
            Vec2 dirv = cursorVel_ * (1.f / speed);
            for (int k = 1; k <= steps; k++) {
                Vec2 p = Lerp(a, b, (float)k / (float)steps);
                int bk = (int)((p.x - bucketX0_) / bucketW_);
                if (bk < 0 || bk >= (int)buckets_.size()) continue;
                for (int si : buckets_[(size_t)bk]) {
                    SurfRef& s = surfaces_[(size_t)si];
                    Pile& pile = *s.pile;
                    if (p.x < pile.x0 || p.x > pile.X1()) continue;
                    float sy = pile.SurfaceY(p.x);
                    if (p.y + rad < sy || p.y - rad > pile.baseY) continue;
                    if (s.owner && s.owner->Occluded(p.x, p.y)) continue;
                    float removed = pile.CutAbove(p.x - rad, p.x + rad, p.y + rad * 0.6f);
                    if (removed > 0.5f * sc * sc) {
                        int nPuffs = std::min(3, 1 + (int)(removed / (20.f * sc * sc)));
                        SpawnPuffs(Vec2(p.x, std::min(p.y, sy + 2.f * sc)), nPuffs, rad * 0.6f, sc,
                                   cursorVel_ * 0.35f + Vec2(0, -60.f * sc), 70.f * sc);
                        if (removed > 45.f * sc * sc) {
                            Body c;
                            c.kind = BK_CHUNK;
                            c.r = Clamp(std::sqrt(removed * 0.45f / kPi), 2.f * sc, 9.f * sc);
                            c.hl = c.r * rng_.Range(0.f, 0.7f);
                            c.p = Vec2(p.x + dirv.x * rad * 1.5f, std::min(p.y, sy) - c.r);
                            c.v = cursorVel_ * rng_.Range(0.35f, 0.6f) + Vec2(0, -rng_.Range(150.f, 320.f) * sc);
                            c.w = rng_.Range(-8.f, 8.f);
                            c.scale = sc;
                            c.seed = rng_.Range(0.f, 100.f);
                            c.ignore = nullptr;
                            newBodies_.push_back(c);
                        }
                    }
                }
            }
        }
    }
    cursorPrev_ = in.cursor;
    cursorPrevValid_ = true;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void World::Update(float dt, const InputState& in) {
    time_ += dt;
    // Wind: steady component plus slow gusts.
    gustPhase_ += dt * 0.11f;
    float gust = Fbm1D(gustPhase_) * s_.GustDip();
    float targetWind = s_.WindDip() + gust;
    windNow_ += (targetWind - windNow_) * std::min(1.f, dt * 1.5f);
    if (melting_ > 0) melting_ = std::max(0.f, melting_ - dt);
    if (avalancheDelay_ >= 0) {
        avalancheDelay_ -= dt;
        if (avalancheDelay_ < 0) {
            for (auto& kv : wins_) {
                WinSnow& w = *kv.second;
                float sc = w.scale;
                DetachCap(w, Vec2(rng_.Range(-60.f, 60.f) * sc, -rng_.Range(40.f, 160.f) * sc), 110.f * sc, false);
                DetachIcicles(w, Vec2(0, 0), true);
            }
        }
    }

    CursorPlay(in, dt);
    for (size_t i = 0; i < mons_.size(); i++) {
        if (!mons_[i].active) continue;
        UpdateFlakes(mons_[i], i, dt, in);
    }
    UpdatePiles(dt);
    UpdateSnowmen(dt, in);
    UpdateBodies(dt);
    if (!newBodies_.empty()) {
        if (bodies_.size() < 4000) bodies_.insert(bodies_.end(), newBodies_.begin(), newBodies_.end());
        newBodies_.clear();
    }
    UpdatePuffs(dt);
    UpdateDebris(dt);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void World::Melt() {
    melting_ = 6.f;
    for (auto& s : snowmen_)
        if (s.phase < 7) {
            s.phase = 7;
            s.phaseT = 0;
        }
}

void World::Avalanche() { avalancheDelay_ = 0.0f; }

void World::Shovel() {
    for (auto& m : mons_) {
        m.plowing = true;
        m.plowX = (float)m.d.rc.left - 30.f * m.d.scale;
    }
}

void World::BuildSnowmanNow() {
    // Pick the monitor under the cursor if possible, else the primary.
    POINT pt;
    int mi = -1;
    if (GetCursorPos(&pt)) mi = MonitorAt((float)pt.x, (float)pt.y);
    if (mi < 0) {
        for (size_t i = 0; i < mons_.size(); i++)
            if (mons_[i].d.primary) mi = (int)i;
    }
    if (mi < 0 && !mons_.empty()) mi = 0;
    if (mi >= 0) StartSnowman((size_t)mi, true);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void World::EmitShape(std::vector<ShapeInst>& out, Vec2 a, Vec2 b, float ra, float rb, int style, const float col[4],
                      float depth, float rot, float seed, float p2, float p3, int kind) const {
    ShapeInst s;
    s.ax = a.x;
    s.ay = a.y;
    s.bx = b.x;
    s.by = b.y;
    s.ra = ra;
    s.rb = rb;
    s.rot = rot;
    s.seed = seed;
    s.r = col[0];
    s.g = col[1];
    s.b = col[2];
    s.a = col[3];
    s.style = (float)style;
    s.kind = (float)kind;
    s.p2 = p2;
    s.p3 = p3;
    s.depth = depth;
    s.pad[0] = s.pad[1] = s.pad[2] = 0;
    out.push_back(s);
}

void World::BuildDrawLists(DrawLists& L) {
    L.Clear();
    L.time = time_;
    L.outline = s_.flakeOutline ? 1.f : 0.f;
    monContent_.assign(mons_.size(), 0);
    auto markRect = [&](float x0, float y0, float x1, float y1) {
        for (size_t i = 0; i < mons_.size(); i++) {
            const RECT& r = mons_[i].d.rc;
            if (x0 < r.right && x1 > r.left && y0 < r.bottom && y1 > r.top) monContent_[i] = 1;
        }
    };
    auto markPoint = [&](Vec2 p, float rad) { markRect(p.x - rad, p.y - rad, p.x + rad, p.y + rad); };

    for (const Occ& o : occluders_)
        L.occluders.push_back({(float)o.rc.left, (float)o.rc.top, (float)o.rc.right, (float)o.rc.bottom, DepthForZ(o.z)});

    // Distant flakes.
    for (size_t mi = 0; mi < mons_.size(); mi++) {
        const MonState& m = mons_[mi];
        if (!m.active) continue;
        if (!m.flakes.empty()) monContent_[mi] = 1;
        for (const Flake& f : m.flakes) {
            if (f.layer != 0) continue;
            L.flakes.push_back({f.p.x, f.p.y, f.size, f.rot, f.alpha, (float)f.type, 0.f, 0.f});
        }
    }
    L.flakeFarEnd = (uint32_t)L.flakes.size();

    auto addPile = [&](const Pile& p, float depth, int style) {
        if (p.h.empty()) return;
        float mx = p.MaxHeight();
        if (mx < 0.25f) return;
        PileInst pi;
        pi.x0 = p.x0;
        pi.baseY = p.baseY;
        pi.cellW = p.cellW;
        pi.count = (float)p.h.size();
        pi.offset = (float)L.heights.size();
        pi.maxH = mx + 2.f * p.scale;
        pi.depth = depth;
        pi.style = (float)style;
        pi.seed = p.seed;
        pi.skirt = style ? 3.f * p.scale : 1.5f * p.scale;
        pi.alpha = 1.f;
        pi.scale = p.scale;
        L.heights.insert(L.heights.end(), p.h.begin(), p.h.end());
        L.piles.push_back(pi);
        markRect(p.x0, p.baseY - mx - 4.f * p.scale, p.X1(), p.baseY + 4.f * p.scale);
    };
    for (auto& kv : wins_) addPile(kv.second->cap, kv.second->depth, 0);
    L.capPileCount = (uint32_t)L.piles.size();

    // Icicles (attached to windows, occluded by windows in front).
    static const float kWhite[4] = {1, 1, 1, 1};
    for (auto& kv : wins_) {
        const WinSnow& w = *kv.second;
        float sc = w.scale;
        for (const Icicle& ic : w.icicles) {
            if (ic.len < 0.8f * sc) continue;
            Vec2 root((float)w.rc.left + ic.xRel, (float)w.rc.bottom - 1.f * sc);
            Vec2 tip(root.x + ic.tilt, (float)w.rc.bottom + ic.len);
            float wr = std::min(ic.width, ic.len * 0.45f + 0.8f * sc);
            EmitShape(L.shapes, root, tip, wr, 0.3f * sc, SS_ICE, kWhite, w.depth, 0.f, ic.seed);
            if (ic.dripGrow > 0) {
                float r = 1.35f * sc * std::sqrt(ic.dripGrow);
                Vec2 c = tip + Vec2(0, r * 0.6f);
                EmitShape(L.shapes, c, c, r, r, SS_WATER, kWhite, w.depth);
            }
            markPoint(tip, ic.len);
        }
    }
    L.shapeAttachedEnd = (uint32_t)L.shapes.size();

    // Snowmen.
    for (const Snowman& s : snowmen_) {
        if (s.dead) continue;
        size_t before = L.shapes.size();
        BuildSnowmanShapes(s, L.shapes);
        if (L.shapes.size() > before) markPoint(Vec2(s.x, s.baseY - s.R[0] * 2.f), s.R[0] * 6.f);
    }
    L.shapeSnowmanEnd = (uint32_t)L.shapes.size();

    // Snow on the taskbar, in front of everything.
    for (auto& m : mons_) addPile(m.ground, 0.f, 1);

    // Contact shadows under snowmen.
    for (const Snowman& s : snowmen_) {
        if (s.dead || !s.vis[0]) continue;
        float r0 = s.rad[0];
        float a = 0.28f * (1.f - s.melt);
        float col[4] = {0.22f, 0.30f, 0.45f, a};
        Vec2 c(s.pos[0].x, s.baseY - 0.5f * s.scale);
        EmitShape(L.shapes, c - Vec2(r0 * 0.85f, 0), c + Vec2(r0 * 0.85f, 0), r0 * 0.24f, r0 * 0.24f, SS_SOFT, col, 0.f);
    }

    // Falling chunks, icicles, shards, drops.
    for (const Body& b : bodies_) {
        float sc = b.scale;
        switch (b.kind) {
        case BK_CHUNK:
        case BK_BALL: {
            Vec2 dir(std::cos(b.ang), std::sin(b.ang));
            Vec2 e1 = b.p - dir * b.hl, e2 = b.p + dir * b.hl;
            EmitShape(L.shapes, e1, e2, b.r, b.r, SS_SNOW, kWhite, 0.f, b.ang, b.seed);
            markPoint(b.p, b.hl + b.r);
            break;
        }
        case BK_ICICLE: {
            Vec2 dir = Rotate(Vec2(0, 1), b.ang);
            Vec2 root = b.p - dir * (b.hl * 0.35f);
            Vec2 tip = b.p + dir * (b.hl * 0.65f);
            EmitShape(L.shapes, root, tip, std::min(b.r, b.hl * 0.45f + 0.8f * sc), 0.3f * sc, SS_ICE, kWhite, 0.f, 0.f,
                      b.seed);
            markPoint(b.p, b.hl);
            break;
        }
        case BK_SHARD: {
            float fade = Saturate((b.maxLife - b.life) / 0.3f);
            float col[4] = {1, 1, 1, fade};
            Vec2 dir(std::cos(b.ang), std::sin(b.ang));
            EmitShape(L.shapes, b.p - dir * b.hl, b.p + dir * b.hl, b.r, b.r * 0.4f, SS_ICE, col, 0.f, 0.f, b.seed);
            markPoint(b.p, b.hl);
            break;
        }
        case BK_DROP: {
            float fade = Saturate((b.maxLife - b.life) / 0.2f);
            float col[4] = {1, 1, 1, fade};
            float sp = b.v.Len();
            Vec2 dir = sp > 1.f ? b.v * (1.f / sp) : Vec2(0, 1);
            float stretch = std::min(sp * 0.012f, 3.f) * sc;
            EmitShape(L.shapes, b.p - dir * stretch, b.p, b.r * 0.55f, b.r, SS_WATER, col, 0.f);
            markPoint(b.p, b.r + stretch);
            break;
        }
        }
    }
    // Snowman debris (carrots, coal, sticks, hats).
    for (const Debris& d : debris_) {
        float fade = d.resting ? Saturate(1.f - (d.restTime - 6.f) / 3.f) : 1.f;
        for (const LocalShape& ls : d.shapes) {
            float col[4] = {ls.col[0], ls.col[1], ls.col[2], ls.col[3] * fade};
            if (ls.kind == 0) {
                Vec2 a = d.p + Rotate(ls.a, d.ang);
                Vec2 b = d.p + Rotate(ls.b, d.ang);
                EmitShape(L.shapes, a, b, ls.ra, ls.rb, ls.style, col, 0.f, d.ang, ls.seed, ls.p2, ls.p3, 0);
            } else {
                Vec2 c = d.p + Rotate(ls.a, d.ang);
                EmitShape(L.shapes, c, ls.b, ls.ra, ls.rb + d.ang, ls.style, col, 0.f, d.ang, ls.seed, ls.p2, ls.p3,
                          ls.kind);
            }
        }
        markPoint(d.p, d.radius * 3.f);
    }
    // Powder puffs.
    for (const Puff& q : puffs_) {
        float t = q.life / q.maxLife;
        float a = q.alpha * std::pow(std::max(0.f, 1.f - t), 1.5f);
        float col[4] = {0.97f, 0.985f, 1.f, a};
        EmitShape(L.shapes, q.p, q.p, q.r, q.r, SS_SOFT, col, 0.f);
        markPoint(q.p, q.r);
    }

    // Falling snow in front.
    for (size_t mi = 0; mi < mons_.size(); mi++) {
        const MonState& m = mons_[mi];
        if (!m.active) continue;
        for (const Flake& f : m.flakes) {
            if (f.layer == 0) continue;
            L.flakes.push_back({f.p.x, f.p.y, f.size, f.rot, f.alpha, (float)f.type, 0.f, 0.f});
        }
    }
}
