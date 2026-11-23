// Snowy - "Now and then, it builds a snowman on your taskbar."
#include "world.h"

enum SnowmanPhase {
    SM_ROLL_BASE = 0,
    SM_ROLL_MID,
    SM_LIFT_MID,
    SM_ROLL_HEAD,
    SM_LIFT_HEAD,
    SM_DECORATE,
    SM_ALIVE,
    SM_MELT,
};

static const float kDecorDuration = 2.9f;

bool World::StartSnowman(size_t mi, bool force) {
    if (mi >= mons_.size()) return false;
    MonState& m = mons_[mi];
    const RECT& rc = m.d.rc;
    float sc = m.d.scale;
    int existing = 0;
    for (auto& o : snowmen_)
        if (!o.dead && o.mon == (int)mi) existing++;
    if (existing >= (force ? 4 : 1)) return false;

    Snowman s;
    s.mon = (int)mi;
    s.scale = sc;
    float H = rng_.Range(100.f, 150.f) * sc;
    s.R[0] = H / 4.1f;
    s.R[1] = s.R[0] * 0.72f;
    s.R[2] = s.R[1] * 0.68f;
    float W = (float)RectW(rc);
    bool found = false;
    for (int attempt = 0; attempt < 16 && !found; attempt++) {
        float x = (float)rc.left + rng_.Range(0.1f, 0.9f) * W;
        bool ok = true;
        for (auto& o : snowmen_)
            if (!o.dead && o.mon == (int)mi && std::fabs(o.x - x) < (o.R[0] + s.R[0]) * 3.2f) ok = false;
        if (ok) {
            s.x = x;
            found = true;
        }
    }
    if (!found) return false;
    float leftRoom = s.x - (float)rc.left, rightRoom = (float)rc.right - s.x;
    s.rollSide = rightRoom > leftRoom ? 1 : -1;
    s.rollFrom = Clamp(s.x - (float)s.rollSide * rng_.Range(120.f, 210.f) * sc, (float)rc.left + s.R[0],
                       (float)rc.right - s.R[0]);
    s.dir = rng_.Chance(0.5f) ? 1 : -1;
    float hr = rng_.Next01();
    s.hat = hr < 0.38f ? 0 : (hr < 0.68f ? 1 : 2);
    static const float kScarves[5][3] = {{0.86f, 0.10f, 0.12f}, {0.10f, 0.52f, 0.26f}, {0.16f, 0.36f, 0.82f},
                                         {0.95f, 0.66f, 0.12f}, {0.56f, 0.22f, 0.66f}};
    int pick = rng_.Chance(0.5f) ? 0 : rng_.IRange(1, 4);
    for (int k = 0; k < 3; k++) s.scarf[k] = kScarves[pick][k];
    if (s.hat == 1) {
        s.hatCol[0] = 0.09f;
        s.hatCol[1] = 0.09f;
        s.hatCol[2] = 0.10f;
    } else {
        for (int k = 0; k < 3; k++) s.hatCol[k] = s.scarf[k];
    }
    s.baseY = m.ground.SurfaceY(s.x) + 0.12f * s.R[0];
    s.lifetime = rng_.Range(600.f, 1080.f);
    s.seed = rng_.Range(0.f, 100.f);
    s.nextWave = rng_.Range(3.f, 7.f);
    s.nextBlink = rng_.Range(2.f, 5.f);
    s.phase = SM_ROLL_BASE;
    s.phaseT = 0;
    snowmen_.push_back(s);
    return true;
}

// Builds the decorations of a snowman in absolute screen coordinates.
void World::SnowmanParts(const Snowman& s, std::vector<SmPart>& parts) const {
    parts.clear();
    Vec2 H = s.pos[2], M = s.pos[1];
    float r3 = s.rad[2];
    float r2 = s.rad[1];
    float d = (float)s.dir;
    const float white[4] = {1, 1, 1, 1};
    auto add = [&](int group, float appear, Vec2 pivot, Vec2 a, Vec2 b, float ra, float rb, int style,
                   const float col[4], float p2, float p3, int kind, float seed) {
        SmPart p;
        p.group = group;
        p.appear = appear;
        p.pivot = pivot;
        p.sh.a = a;
        p.sh.b = b;
        p.sh.ra = ra;
        p.sh.rb = rb;
        p.sh.style = (uint8_t)style;
        p.sh.kind = (uint8_t)kind;
        p.sh.p2 = p2;
        p.sh.p3 = p3;
        p.sh.seed = seed;
        for (int k = 0; k < 4; k++) p.sh.col[k] = col[k];
        parts.push_back(p);
    };

    // Arms (drawn first: they go "into" the middle ball).
    for (int side = -1; side <= 1; side += 2) {
        float sd = (float)side;
        Vec2 S = M + Vec2(sd * 0.80f * r2, -0.22f * r2);
        Vec2 dv(sd * std::cos(0.44f), -std::sin(0.44f));
        if (side == 1 && s.waveT >= 0) {
            float env = std::sin(kPi * Saturate(s.waveT / 1.8f));
            float th = (0.45f + 0.42f * std::sin(s.waveT * 13.f)) * env;
            dv = Rotate(dv, -th);
        }
        float L = 1.25f * r2;
        Vec2 E = S + dv * L;
        int grp = side < 0 ? 3 : 4;
        float appear = side < 0 ? 1.85f : 2.05f;
        add(grp, appear, S, S, E, 0.065f * r2, 0.032f * r2, SS_WOOD, white, 0, 0, 0, s.seed + sd);
        Vec2 t1o = S + dv * (L * 0.6f);
        Vec2 t1d = Rotate(dv, -sd * 0.62f);
        add(grp, appear + 0.05f, S, t1o, t1o + t1d * (0.34f * r2), 0.036f * r2, 0.018f * r2, SS_WOOD, white, 0, 0, 0,
            s.seed + 2.f * sd);
        Vec2 t2o = S + dv * (L * 0.82f);
        Vec2 t2d = Rotate(dv, sd * 0.55f);
        add(grp, appear + 0.08f, S, t2o, t2o + t2d * (0.24f * r2), 0.03f * r2, 0.016f * r2, SS_WOOD, white, 0, 0, 0,
            s.seed + 3.f * sd);
    }

    // Coal buttons.
    for (int k = 0; k < 3; k++) {
        Vec2 c = M + Vec2(d * 0.05f * r2, (-0.34f + 0.34f * (float)k) * r2);
        add(1, 1.05f + 0.12f * (float)k, c, c, c, 0.085f * r2, 0.085f * r2, SS_COAL, white, 0, 0, 0,
            s.seed + 9.f + (float)k);
    }

    // Scarf: hanging tail, neck band, fringe.
    const float scarf[4] = {s.scarf[0], s.scarf[1], s.scarf[2], 1};
    Vec2 tail0 = H + Vec2(-d * 0.38f * r3, 0.92f * r3);
    Vec2 tail1 = tail0 + Vec2(-d * 0.20f * r3, 1.25f * r3);
    Vec2 band0 = H + Vec2(-0.82f * r3, 0.80f * r3);
    Vec2 band1 = H + Vec2(0.82f * r3, 0.88f * r3);
    add(2, 1.55f, tail0, tail0, tail1, 0.19f * r3, 0.17f * r3, SS_FABRIC, scarf, 3.f, 0.25f, 0, s.seed + 20.f);
    add(2, 1.55f, (band0 + band1) * 0.5f, band0, band1, 0.21f * r3, 0.21f * r3, SS_FABRIC, scarf, 4.f, 0.f, 0,
        s.seed + 21.f);
    for (int k = 0; k < 4; k++) {
        Vec2 f0 = tail1 + Vec2((-0.13f + 0.085f * (float)k) * r3, 0.10f * r3);
        Vec2 f1 = f0 + Vec2(-d * 0.03f * r3, 0.22f * r3);
        add(2, 1.65f, tail1, f0, f1, 0.035f * r3, 0.03f * r3, SS_FABRIC, scarf, 0.f, 0.f, 0, s.seed + 22.f);
    }

    // Face: eyes, nose, smile.
    bool blink = s.blinkT >= 0;
    for (int k = -1; k <= 1; k += 2) {
        Vec2 e = H + Vec2(d * 0.10f * r3 + (float)k * 0.30f * r3, -0.16f * r3);
        if (blink)
            add(0, 0.0f, e, e - Vec2(0.09f * r3, 0), e + Vec2(0.09f * r3, 0), 0.035f * r3, 0.035f * r3, SS_COAL, white,
                0, 0, 0, s.seed);
        else
            add(0, 0.0f, e, e, e, 0.10f * r3, 0.10f * r3, SS_COAL, white, 0, 0, 0, s.seed + (float)k);
    }
    Vec2 n0 = H + Vec2(d * 0.10f * r3, 0.06f * r3);
    Vec2 n1 = H + Vec2(d * 1.30f * r3, 0.16f * r3);
    add(0, 0.30f, n0, n0, n1, 0.14f * r3, 0.025f * r3, SS_CARROT, white, 0, 0, 0, s.seed);
    for (int k = 0; k < 5; k++) {
        float th = (50.f + 20.f * (float)k) * kPi / 180.f;
        Vec2 c = H + Vec2(d * 0.06f * r3, -0.02f * r3) + Vec2(std::cos(th), std::sin(th)) * (0.48f * r3);
        add(0, 0.55f + 0.08f * (float)k, c, c, c, 0.055f * r3, 0.055f * r3, SS_COAL, white, 0, 0, 0,
            s.seed + 3.f + (float)k);
    }

    // Hat.
    if (s.hat == 1) {
        float tilt = d * 0.10f;
        Vec2 up = Rotate(Vec2(0, -1), tilt);
        Vec2 brimC = H + up * (0.80f * r3);
        Vec2 crownC = H + up * (0.80f * r3 + 0.50f * r3);
        Vec2 bandC = H + up * (0.80f * r3 + 0.15f * r3);
        const float hc[4] = {s.hatCol[0], s.hatCol[1], s.hatCol[2], 1};
        const float red[4] = {0.74f, 0.08f, 0.10f, 1};
        add(5, 2.30f, brimC, crownC, Vec2(0.62f * r3, 0.50f * r3), 0.06f * r3, tilt, SS_FLAT, hc, 1.f, 0, 2, 0);
        add(5, 2.30f, brimC, bandC, Vec2(0.63f * r3, 0.11f * r3), 0.03f * r3, tilt, SS_FLAT, red, 1.f, 0, 2, 0);
        add(5, 2.30f, brimC, brimC, Vec2(0.98f * r3, 0.08f * r3), 0.07f * r3, tilt, SS_FLAT, hc, 1.f, 0, 1, 0);
    } else if (s.hat == 2) {
        const float hc[4] = {s.hatCol[0], s.hatCol[1], s.hatCol[2], 1};
        const float fold[4] = {s.hatCol[0] * 0.8f, s.hatCol[1] * 0.8f, s.hatCol[2] * 0.8f, 1};
        Vec2 domeC = H + Vec2(0, -0.62f * r3);
        Vec2 foldC = H + Vec2(0, -0.30f * r3);
        Vec2 pom = H + Vec2(0, -1.10f * r3);
        add(5, 2.30f, foldC, domeC, Vec2(0.86f * r3, 0.44f * r3), 0.43f * r3, 0.f, SS_FABRIC, hc, 0, 0, 2, s.seed + 30.f);
        add(5, 2.30f, foldC, foldC, Vec2(0.92f * r3, 0.15f * r3), 0.12f * r3, 0.f, SS_FABRIC, fold, 0, 0, 1,
            s.seed + 31.f);
        add(5, 2.40f, pom, pom, pom, 0.21f * r3, 0.21f * r3, SS_SNOW, white, 0, 0, 0, s.seed + 32.f);
    }
}

static void PopTransform(const LocalShape& in, Vec2 pivot, float pop, LocalShape& out) {
    out = in;
    if (in.kind == 0) {
        out.a = pivot + (in.a - pivot) * pop;
        out.b = pivot + (in.b - pivot) * pop;
        out.ra = in.ra * pop;
        out.rb = in.rb * pop;
    } else {
        out.a = pivot + (in.a - pivot) * pop;
        out.b = in.b * pop;
        out.ra = in.ra * pop;
    }
}

void World::BuildSnowmanShapes(const Snowman& s, std::vector<ShapeInst>& out) const {
    std::vector<SmPart> parts;
    SnowmanParts(s, parts);
    float decor = -1.f;
    if (s.phase == SM_DECORATE) decor = s.decorT;
    if (s.phase == SM_ALIVE) decor = 99.f;
    if (s.phase == SM_MELT && !s.decorDropped) decor = 99.f;
    float alpha = s.phase == SM_MELT ? Saturate((1.f - s.melt) / 0.3f) : 1.f;

    auto emitGroup = [&](int g) {
        if (decor < 0) return;
        for (const SmPart& p : parts) {
            if (p.group != g) continue;
            float pop = PopCurve((decor - p.appear) / 0.3f);
            if (pop <= 0.001f) continue;
            LocalShape ls;
            PopTransform(p.sh, p.pivot, pop, ls);
            float col[4] = {ls.col[0], ls.col[1], ls.col[2], ls.col[3] * alpha};
            if (ls.kind == 0)
                EmitShape(out, ls.a, ls.b, ls.ra, ls.rb, ls.style, col, 0.f, 0.f, ls.seed, ls.p2, ls.p3, 0);
            else
                EmitShape(out, ls.a, ls.b, ls.ra, ls.rb, ls.style, col, 0.f, 0.f, ls.seed, ls.p2, ls.p3, ls.kind);
        }
    };
    const float snow[4] = {1, 1, 1, alpha};
    auto ball = [&](int i) {
        if (!s.vis[i]) return;
        EmitShape(out, s.pos[i], s.pos[i], s.rad[i], s.rad[i], SS_SNOW, snow, 0.f, s.rot[i], s.seed + (float)i * 7.f);
    };
    emitGroup(3);
    emitGroup(4);
    ball(0);
    ball(1);
    emitGroup(1);
    ball(2);
    emitGroup(2);
    emitGroup(0);
    emitGroup(5);
}

void World::DropDecorAsDebris(const Snowman& s, Vec2 vel, bool includeFace) {
    std::vector<SmPart> parts;
    SnowmanParts(s, parts);
    float sc = s.scale;
    for (int g = 0; g <= 5; g++) {
        if (!includeFace && (g == 0 || g == 5)) continue;
        if (g == 1) {
            // each button separately
            for (const SmPart& p : parts) {
                if (p.group != 1) continue;
                Debris d;
                d.p = p.sh.a;
                d.v = vel * rng_.Range(0.3f, 0.6f) + Vec2(rng_.Range(-80.f, 80.f), -rng_.Range(40.f, 160.f)) * sc;
                d.w = rng_.Range(-6.f, 6.f);
                d.radius = p.sh.ra;
                d.scale = sc;
                LocalShape ls = p.sh;
                ls.a = Vec2(0, 0);
                ls.b = Vec2(0, 0);
                d.shapes.push_back(ls);
                debris_.push_back(d);
            }
            continue;
        }
        Vec2 c(0, 0);
        int n = 0;
        for (const SmPart& p : parts)
            if (p.group == g) {
                c += p.sh.a;
                n++;
            }
        if (n == 0) continue;
        c = c * (1.f / (float)n);
        Debris d;
        d.p = c;
        d.v = vel * rng_.Range(0.3f, 0.7f) + Vec2(rng_.Range(-90.f, 90.f), -rng_.Range(60.f, 220.f)) * sc;
        d.w = rng_.Range(-7.f, 7.f);
        d.scale = sc;
        float rad = 0;
        for (const SmPart& p : parts) {
            if (p.group != g) continue;
            LocalShape ls = p.sh;
            ls.a = p.sh.a - c;
            if (ls.kind == 0) ls.b = p.sh.b - c;
            rad = std::max(rad, ls.a.Len() * 0.35f + ls.ra);
            d.shapes.push_back(ls);
        }
        d.radius = std::max(rad, 2.f * sc);
        debris_.push_back(d);
    }
}

void World::KnockOver(Snowman& s, Vec2 vel) {
    float sc = s.scale;
    for (int i = 0; i < 2; i++) {
        if (!s.vis[i]) continue;
        Body b;
        b.kind = BK_BALL;
        b.p = s.pos[i];
        b.r = s.rad[i];
        b.hl = 0;
        b.v = vel * (0.25f + 0.15f * (float)i) +
              Vec2(rng_.Range(-60.f, 60.f) * sc, -rng_.Range(100.f, 260.f) * sc * (float)(i + 1));
        b.w = rng_.Range(-4.f, 4.f);
        b.ang = s.rot[i];
        b.scale = sc;
        b.seed = s.seed + (float)i * 7.f;
        newBodies_.push_back(b);
        SpawnPuffs(s.pos[i], 4, s.rad[i] * 0.6f, sc, vel * 0.2f, 120.f * sc);
    }
    bool decorated = s.phase >= SM_DECORATE;
    if (s.vis[2]) {
        // The head flies off in one piece, face and hat included.
        Debris d;
        d.p = s.pos[2];
        d.v = vel * 0.55f + Vec2(0, -rng_.Range(250.f, 450.f) * sc);
        d.w = rng_.Range(-6.f, 6.f);
        d.scale = sc;
        d.radius = s.rad[2];
        LocalShape head;
        head.a = Vec2(0, 0);
        head.b = Vec2(0, 0);
        head.ra = head.rb = s.rad[2];
        head.style = SS_SNOW;
        head.seed = s.seed + 14.f;
        d.shapes.push_back(head);
        if (decorated) {
            std::vector<SmPart> parts;
            SnowmanParts(s, parts);
            for (const SmPart& p : parts) {
                if (p.group != 0 && p.group != 5) continue;
                if (s.phase == SM_DECORATE && s.decorT < p.appear) continue;
                LocalShape ls = p.sh;
                ls.a = p.sh.a - s.pos[2];
                if (ls.kind == 0) ls.b = p.sh.b - s.pos[2];
                d.shapes.push_back(ls);
            }
        }
        debris_.push_back(d);
    }
    if (decorated) DropDecorAsDebris(s, vel, false);
    s.dead = true;
    if (s.mon < (int)mons_.size()) mons_[(size_t)s.mon].snowmanTimer = rng_.Range(45.f, 110.f);
}

void World::UpdateSnowmen(float dt, const InputState& in) {
    for (size_t mi = 0; mi < mons_.size(); mi++) {
        MonState& m = mons_[mi];
        if (!s_.snowman || !m.active) continue;
        bool has = false;
        for (auto& s : snowmen_)
            if (!s.dead && s.mon == (int)mi) has = true;
        if (has) continue;
        m.snowmanTimer -= dt * timeScale_;
        if (m.snowmanTimer <= 0) {
            if (StartSnowman(mi, false)) {
                float base[3] = {1300.f, 560.f, 170.f};
                m.snowmanTimer = base[std::min(2, std::max(0, s_.snowmanFreq))] * rng_.Range(0.75f, 1.35f);
            } else {
                m.snowmanTimer = 20.f;
            }
        }
    }

    float cursorSpeed = cursorVel_.Len();
    for (Snowman& s : snowmen_) {
        if (s.dead) continue;
        if (s.mon >= (int)mons_.size()) {
            s.dead = true;
            continue;
        }
        MonState& m = mons_[(size_t)s.mon];
        if (!m.active) continue;
        Pile& g = m.ground;
        float sc = s.scale;
        const RECT& rc = m.d.rc;
        s.phaseT += dt;

        float melt = s.phase == SM_MELT ? s.melt : 0.f;
        float sink = 0.12f * s.R[0];
        float surf = std::min(g.SurfaceY(s.x), g.baseY);
        if (surf + sink > s.baseY) s.baseY = surf + sink;  // ground got lower: settle down

        auto stacked = [&](int i) -> Vec2 {
            float r0 = s.rad[0], r1 = s.rad[1], r2 = s.rad[2];
            float by = s.baseY + s.R[0] * 0.45f * melt;
            Vec2 c0(s.x, by - r0);
            if (i == 0) return c0;
            Vec2 c1(s.x, c0.y - (r0 + r1) * 0.86f);
            if (i == 1) return c1;
            return Vec2(s.x, c1.y - (r1 + r2) * 0.86f);
        };
        auto rollBall = [&](int i, float fromX, float toX, float dur) -> bool {
            float t = Saturate(s.phaseT / dur);
            float x = Lerp(fromX, toX, EaseInOut(t));
            float r = Lerp(s.R[i] * 0.28f, s.R[i], std::pow(t, 0.8f));
            float prevX = s.vis[i] ? s.pos[i].x : x;
            float prevR = s.vis[i] ? s.rad[i] : r;
            s.vis[i] = true;
            s.rad[i] = r;
            float sy = std::min(g.SurfaceY(x), g.baseY);
            s.pos[i] = Vec2(x, sy + 0.10f * r - r);
            s.rot[i] += (x - prevX) / std::max(r, 1.f);
            float dV = kPi * (r * r - prevR * prevR);
            if (dV > 0) g.Take(x, r * 0.9f, dV * 0.15f);
            return t >= 1.f;
        };
        auto lift = [&](int i, Vec2 from, Vec2 to, float dur) -> bool {
            float t = Saturate(s.phaseT / dur);
            Vec2 p = Lerp(from, to, EaseInOut(t));
            p.y -= s.R[0] * 0.55f * 4.f * t * (1.f - t);
            s.pos[i] = p;
            s.rot[i] += dt * 1.5f * (float)-s.rollSide;
            return t >= 1.f;
        };

        switch (s.phase) {
        case SM_ROLL_BASE:
            if (rollBall(0, s.rollFrom, s.x, 2.6f)) {
                s.phase = SM_ROLL_MID;
                s.phaseT = 0;
                s.rollFrom = Clamp(s.x + (float)s.rollSide * (s.R[0] + s.R[1] + rng_.Range(110.f, 190.f) * sc),
                                   (float)rc.left + s.R[1], (float)rc.right - s.R[1]);
            }
            break;
        case SM_ROLL_MID: {
            s.pos[0] = stacked(0);
            float rest = s.x + (float)s.rollSide * (s.R[0] + s.R[1] * 1.08f);
            if (rollBall(1, s.rollFrom, rest, 2.0f)) {
                s.phase = SM_LIFT_MID;
                s.phaseT = 0;
                s.liftFrom = s.pos[1];
            }
            break;
        }
        case SM_LIFT_MID:
            s.pos[0] = stacked(0);
            if (lift(1, s.liftFrom, stacked(1), 0.7f)) {
                s.phase = SM_ROLL_HEAD;
                s.phaseT = 0;
                SpawnPuffs(stacked(1) + Vec2(0, s.R[1] * 0.8f), 4, s.R[1] * 0.5f, sc, Vec2(0, -30.f * sc), 50.f * sc);
                s.rollFrom = Clamp(s.x + (float)s.rollSide * (s.R[0] + s.R[2] + rng_.Range(100.f, 170.f) * sc),
                                   (float)rc.left + s.R[2], (float)rc.right - s.R[2]);
            }
            break;
        case SM_ROLL_HEAD: {
            s.pos[0] = stacked(0);
            s.pos[1] = stacked(1);
            float rest = s.x + (float)s.rollSide * (s.R[0] + s.R[2] * 1.12f);
            if (rollBall(2, s.rollFrom, rest, 1.6f)) {
                s.phase = SM_LIFT_HEAD;
                s.phaseT = 0;
                s.liftFrom = s.pos[2];
            }
            break;
        }
        case SM_LIFT_HEAD:
            s.pos[0] = stacked(0);
            s.pos[1] = stacked(1);
            if (lift(2, s.liftFrom, stacked(2), 0.65f)) {
                s.phase = SM_DECORATE;
                s.phaseT = 0;
                s.decorT = 0;
                SpawnPuffs(stacked(2) + Vec2(0, s.R[2] * 0.8f), 3, s.R[2] * 0.5f, sc, Vec2(0, -30.f * sc), 40.f * sc);
            }
            break;
        case SM_DECORATE:
            for (int i = 0; i < 3; i++) s.pos[i] = stacked(i);
            s.decorT += dt;
            if (s.decorT > kDecorDuration) {
                s.phase = SM_ALIVE;
                s.phaseT = 0;
                s.waveT = 0;  // say hello
            }
            break;
        case SM_ALIVE: {
            for (int i = 0; i < 3; i++) s.pos[i] = stacked(i);
            s.age += dt * timeScale_;
            if (s.waveT >= 0) {
                s.waveT += dt;
                if (s.waveT > 1.8f) {
                    s.waveT = -1;
                    s.nextWave = rng_.Range(8.f, 18.f);
                }
            } else {
                s.nextWave -= dt;
                // Wave when the cursor comes close.
                bool isNear = in.cursorValid && (in.cursor - s.pos[1]).Len() < s.R[0] * 3.f && cursorSpeed < 600.f * sc;
                if (s.nextWave <= 0 || (isNear && s.nextWave < 6.f)) s.waveT = 0;
            }
            if (s.blinkT >= 0) {
                s.blinkT += dt;
                if (s.blinkT > 0.14f) s.blinkT = -1;
            } else {
                s.nextBlink -= dt;
                if (s.nextBlink <= 0) {
                    s.blinkT = 0;
                    s.nextBlink = rng_.Range(2.5f, 6.5f);
                }
            }
            if (s.age > s.lifetime) {
                s.phase = SM_MELT;
                s.phaseT = 0;
            }
            break;
        }
        case SM_MELT:
            s.melt = Saturate(s.melt + dt / (melting_ > 0 ? 4.f : 22.f));
            for (int i = 0; i < 3; i++) s.rad[i] = s.R[i] * (1.f - 0.6f * s.melt);
            for (int i = 0; i < 3; i++) s.pos[i] = stacked(i);
            if (!s.decorDropped && s.melt > 0.06f) {
                DropDecorAsDebris(s, Vec2(0, 0), true);
                s.decorDropped = true;
            }
            if (s.melt >= 1.f) s.dead = true;
            break;
        }

        // A fast flick of the cursor knocks the snowman over.
        if (s_.cursorPlay && in.cursorValid && cursorSpeed > 950.f * sc && s.phase != SM_MELT) {
            for (int i = 0; i < 3; i++) {
                if (s.vis[i] && (in.cursor - s.pos[i]).Len() < s.rad[i]) {
                    KnockOver(s, cursorVel_);
                    break;
                }
            }
        }
    }
    snowmen_.erase(std::remove_if(snowmen_.begin(), snowmen_.end(), [](const Snowman& s) { return s.dead; }),
                   snowmen_.end());
}
