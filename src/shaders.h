// Snowy - HLSL shaders (compiled at runtime with D3DCompile, shader model 4.0).
// Kept deliberately conservative so they compile with both the Windows HLSL
// compiler and Wine's vkd3d-based compiler (used for testing on Linux).
#pragma once

static const char kShaderSource[] = R"HLSL(
cbuffer FrameCB : register(b0)
{
    float4 gView;   // origin.x, origin.y, 1/width, 1/height (global physical pixels)
    float4 gMisc;   // time, dpi scale, outline strength, unused
};

float2 ToClip(float2 p)
{
    float2 n = (p - gView.xy) * gView.zw;
    return float2(n.x * 2.0 - 1.0, 1.0 - n.y * 2.0);
}

float2 Corner(uint vid)
{
    return float2((float)(vid & 1u), (float)((vid >> 1) & 1u));
}

uint HashU(uint x)
{
    x = x ^ (x >> 16);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16);
    return x;
}

float Hash2(float2 p)
{
    float2 f = floor(p) + 65536.0;
    uint h = HashU((uint)f.x * 1597334677u ^ HashU((uint)f.y + 0x9e3779b9u));
    return (float)(h & 0xffffffu) * (1.0 / 16777215.0);
}

float VNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = p - i;
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash2(i);
    float b = Hash2(i + float2(1.0, 0.0));
    float c = Hash2(i + float2(0.0, 1.0));
    float d = Hash2(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// ---------------------------------------------------------------------------
// Occluders: window rectangles written to the depth buffer (z-order).
// ---------------------------------------------------------------------------
struct OccIn
{
    float4 rect : ORECT;
    float depth : ODEPTH;
    uint vid : SV_VertexID;
};

float4 OccVS(OccIn i) : SV_Position
{
    float2 c = Corner(i.vid);
    float2 p = lerp(i.rect.xy, i.rect.zw, c);
    return float4(ToClip(p), i.depth, 1.0);
}

float4 OccPS(float4 pos : SV_Position) : SV_Target
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

)HLSL"
R"HLSL(
// ---------------------------------------------------------------------------
// Snow piles: heightfields resting on window tops and on the taskbar.
// ---------------------------------------------------------------------------
Texture2D<float> gHeights : register(t0);

struct PileIn
{
    float4 a : PA;   // x0, baseY, cellW, count
    float4 b : PB;   // offset, maxH, depth, style
    float4 c : PC;   // seed, skirt, alpha, scale
    uint vid : SV_VertexID;
};

struct PileV
{
    float4 pos : SV_Position;
    float2 wp : WPOS;
    float4 a : PA;
    float4 b : PB;
    float4 c : PC;
};

PileV PileVS(PileIn i)
{
    PileV o;
    float2 cr = Corner(i.vid);
    float w = i.a.z * i.a.w;
    float pad = 5.0 * i.c.w + 2.0;
    float x0 = i.a.x - 2.0;
    float x1 = i.a.x + w + 2.0;
    float y0 = i.a.y - i.b.y - pad;
    float y1 = i.a.y + i.c.y + 1.0;
    float2 p = float2(lerp(x0, x1, cr.x), lerp(y0, y1, cr.y));
    o.pos = float4(ToClip(p), i.b.z, 1.0);
    o.wp = p;
    o.a = i.a;
    o.b = i.b;
    o.c = i.c;
    return o;
}

float LoadH(float idx, float offset, float count)
{
    if (idx < 0.0 || idx > count - 1.0)
        return 0.0;
    int k = (int)(offset + idx + 0.5);
    return gHeights.Load(int3(k & 2047, k >> 11, 0));
}

float CatRom(float p0, float p1, float p2, float p3, float t)
{
    return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
}

float PileHeight(float u, float offset, float count)
{
    float x = u - 0.5;
    float i0 = floor(x);
    float t = x - i0;
    float h = CatRom(LoadH(i0 - 1.0, offset, count), LoadH(i0, offset, count),
                     LoadH(i0 + 1.0, offset, count), LoadH(i0 + 2.0, offset, count), t);
    return max(h, 0.0);
}

float4 PilePS(PileV v) : SV_Target
{
    float x0 = v.a.x;
    float baseY = v.a.y;
    float cellW = v.a.z;
    float count = v.a.w;
    float offset = v.b.x;
    float style = v.b.w;
    float seed = v.c.x;
    float skirt = v.c.y;
    float alphaMul = v.c.z;
    float sc = v.c.w;

    float lx = v.wp.x - x0;
    float u = lx / cellW;
    if (u < -0.5 || u > count + 0.5)
        discard;

    float h = PileHeight(u, offset, count);
    float hl = PileHeight(u - 1.0, offset, count);
    float hr = PileHeight(u + 1.0, offset, count);
    float sy = baseY - h;
    float py = v.wp.y;
    float d = py - sy;                 // distance below the snow surface (px)
    float bottom = baseY + skirt;

    // Faint dark halo just above the surface: keeps snow readable on white windows.
    float halo = 0.0;
    if (d < 0.0)
    {
        halo = saturate(1.0 + d / (2.6 * sc));
        halo = halo * halo * saturate(h / (2.0 * sc)) * 0.15 * gMisc.z;
    }

    float cov = saturate(d + 0.6) * saturate(bottom - py + 0.5);
    cov *= saturate(h / (1.1 * sc));
    if (cov <= 0.002 && halo <= 0.002)
        discard;

    float shadeRange = (style > 0.5) ? min(max(h, 1.0), 40.0 * sc) : max(h, 1.0);
    float t = saturate(d / shadeRange);
    float slope = (hr - hl) / (2.0 * cellW);
    float2 n = normalize(float2(-slope, 1.0));
    float diff = saturate(dot(n, normalize(float2(-0.55, 1.0))));

    float2 lp = float2(lx, py - baseY);
    float grain = VNoise(lp / (2.3 * sc) + seed) * 0.6 + VNoise(lp / (7.5 * sc) + seed * 1.7) * 0.4;

    float3 white = float3(1.0, 1.0, 1.0);
    float3 mid = float3(0.925, 0.945, 0.985);
    float3 shade = float3(0.70, 0.77, 0.90);
    float3 col = lerp(white, mid, smoothstep(0.03, 0.40, t));
    col = lerp(col, shade, smoothstep(0.42, 1.0, t) * ((style > 0.5) ? 0.55 : 0.85));
    col *= 0.88 + 0.12 * diff;
    col *= 0.965 + 0.07 * grain;

    // contact shadow where snow meets the window edge
    float ao = saturate((py - (baseY - 2.5 * sc)) / (2.5 * sc));
    col *= 1.0 - 0.13 * ao * step(py, baseY + 0.5);
    // bright crest
    col += 0.05 * saturate(1.0 - d / (1.6 * sc));

    // sparkles
    float2 cell = floor(lp / (3.0 * sc));
    float r = Hash2(cell + seed * 13.0);
    if (r > 0.972 && t < 0.65)
    {
        float2 cc = (cell + 0.5) * 3.0 * sc;
        float dist = length(lp - cc) / sc;
        float tw = pow(saturate(sin(gMisc.x * (1.3 + r * 4.0) + r * 60.0)), 12.0);
        col += tw * saturate(1.3 - dist) * 0.55;
    }
    col = saturate(col);

    float a = cov * alphaMul;
    float ha = halo * (1.0 - a) * alphaMul;
    float3 haloCol = float3(0.12, 0.16, 0.24);
    return float4(col * a + haloCol * ha, a + ha);
}

)HLSL"
R"HLSL(
// ---------------------------------------------------------------------------
// Shapes: signed-distance primitives (round cones and rounded boxes) with
// per-style shading. Used for snow chunks, snowballs, icicles, coal, carrot,
// branches, scarf, hats, water drops and soft powder puffs.
// ---------------------------------------------------------------------------
struct ShapeIn
{
    float4 ab : SA;    // a.xy, b.xy        (box: center.xy, halfExtent.xy)
    float4 rr : SB;    // ra, rb, rot, seed (box: corner radius, angle, rot, seed)
    float4 col : SC;   // straight-alpha color
    float4 prm : SD;   // style, kind, p2, p3
    float depth : SE;
    uint vid : SV_VertexID;
};

struct ShapeV
{
    float4 pos : SV_Position;
    float2 wp : WPOS;
    float4 ab : SA;
    float4 rr : SB;
    float4 col : SC;
    float4 prm : SD;
};

ShapeV ShapeVS(ShapeIn i)
{
    float2 a = i.ab.xy;
    float2 b = i.ab.zw;
    float ra = i.rr.x;
    float rb = i.rr.y;
    float margin = 2.0 + gMisc.y * 1.5;
    float2 lo;
    float2 hi;
    if (i.prm.y > 0.5)
    {
        float e = length(b) + ra;
        lo = a - e;
        hi = a + e;
    }
    else
    {
        lo = min(a - ra, b - rb);
        hi = max(a + ra, b + rb);
        margin += max(ra, rb) * 0.2;   // room for lumpy silhouettes
    }
    lo -= margin;
    hi += margin;
    float2 cr = Corner(i.vid);
    float2 p = lerp(lo, hi, cr);
    ShapeV o;
    o.pos = float4(ToClip(p), i.depth, 1.0);
    o.wp = p;
    o.ab = i.ab;
    o.rr = i.rr;
    o.col = i.col;
    o.prm = i.prm;
    return o;
}

float Cross2(float2 a, float2 b)
{
    return a.x * b.y - a.y * b.x;
}

// Exact SDF of a 2D "uneven capsule" (round cone), after Inigo Quilez.
float SdUnevenCapsule(float2 p, float2 pa, float2 pb, float ra, float rb)
{
    p -= pa;
    pb -= pa;
    float h = dot(pb, pb);
    if (h < 0.0001)
        return length(p) - max(ra, rb);
    float bd = ra - rb;
    float c2 = h - bd * bd;
    if (c2 <= 0.0001)
        return (ra > rb) ? (length(p) - ra) : (length(p - pb) - rb);
    float2 q = float2(dot(p, float2(pb.y, -pb.x)), dot(p, pb)) / h;
    q.x = abs(q.x);
    float2 c = float2(sqrt(c2), bd);
    float k = Cross2(c, q);
    float m = dot(c, q);
    float n = dot(q, q);
    if (k < 0.0)
        return sqrt(h * n) - ra;
    if (k > c.x)
        return sqrt(h * (n + 1.0 - 2.0 * q.y)) - rb;
    return m - ra;
}

float4 ShapePS(ShapeV v) : SV_Target
{
    float2 p = v.wp;
    float2 a = v.ab.xy;
    float2 b = v.ab.zw;
    float ra = v.rr.x;
    float rb = v.rr.y;
    float rot = v.rr.z;
    float seed = v.rr.w;
    int style = (int)(v.prm.x + 0.5);
    float sc = gMisc.y;

    float d;
    float t;
    float2 off;
    float r;
    if (v.prm.y > 0.5)
    {
        // rounded box: a = center, b = half extents, ra = corner radius, rb = angle
        float cs = cos(rb);
        float sn = sin(rb);
        float2 q = p - a;
        q = float2(q.x * cs + q.y * sn, -q.x * sn + q.y * cs);
        float2 dq = abs(q) - b + ra;
        d = length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - ra;
        float2 lo2;
        if (v.prm.y > 1.5 || b.y > b.x)
        {
            // vertical cylinder (kind 2 forces this)
            t = q.y / (2.0 * b.y) + 0.5;
            lo2 = float2(q.x, 0.0);
            r = max(b.x, 0.5);
        }
        else
        {
            t = q.x / (2.0 * b.x) + 0.5;
            lo2 = float2(0.0, q.y);
            r = max(b.y, 0.5);
        }
        off = float2(lo2.x * cs - lo2.y * sn, lo2.x * sn + lo2.y * cs);
    }
    else
    {
        d = SdUnevenCapsule(p, a, b, ra, rb);
        float2 ba = b - a;
        float L2 = dot(ba, ba);
        t = (L2 > 0.0001) ? saturate(dot(p - a, ba) / L2) : 0.0;
        float2 c = a + ba * t;
        off = p - c;
        r = max(lerp(ra, rb, t), 0.5);
    }

    // texture coordinates that rotate with the object (rolling snowballs)
    float cr = cos(rot);
    float sr = sin(rot);
    float2 lc = p - a;
    lc = float2(lc.x * cr + lc.y * sr, -lc.x * sr + lc.y * cr);

    float3 L = normalize(float3(-0.45, -0.75, 0.6));
    float2 nxy = off / r;
    float nl = saturate(dot(nxy, nxy));
    float3 n = float3(nxy, sqrt(saturate(1.0 - nl)));
    float4 base = v.col;
    float3 col = base.rgb;
    float alpha = 0.0;

    if (style == 0)
    {
        // packed snow (chunks, snowballs)
        float lump = (VNoise(lc / (4.0 * sc) + seed) - 0.5) * 2.0;
        d += lump * min(r * 0.13, 2.6 * sc);
        float b1 = VNoise(lc / (2.6 * sc) + seed * 3.1);
        float b2 = VNoise(lc / (2.6 * sc) + seed * 3.1 + float2(5.2, 1.3));
        n = normalize(n + float3(b1 - 0.5, b2 - 0.5, 0.0) * 0.6);
        float dif = saturate(dot(n, L));
        float3 lit = float3(1.0, 1.0, 1.0);
        float3 shd = float3(0.60, 0.68, 0.85);
        col = lerp(shd, lit, smoothstep(0.0, 0.85, dif * 0.85 + 0.22));
        col *= 0.97 + 0.06 * VNoise(lc / (1.6 * sc) + seed * 7.0);
        col = lerp(col, float3(0.80, 0.86, 0.97), nl * nl * 0.22);
        col *= base.rgb;
        alpha = saturate(0.6 - d) * base.a;
    }
    else if (style == 1)
    {
        // ice (icicles, shards)
        float rip = sin(t * 17.0 + seed * 6.0) * 0.16 * r * (1.0 - t);
        d += rip;
        float hx = (nxy.x + 0.40) / 0.22;
        float hl = exp(-hx * hx);
        float dk = smoothstep(0.30, 0.95, nxy.x);
        col = lerp(float3(0.82, 0.93, 1.0), float3(0.52, 0.70, 0.92), dk * 0.75);
        col = lerp(col, float3(1.0, 1.0, 1.0), hl * 0.9);
        float root = 1.0 - smoothstep(0.0, 0.16, t);
        col = lerp(col, float3(0.97, 0.99, 1.0), root * 0.85);
        alpha = 0.48 + 0.40 * nl + hl * 0.30 + root * 0.40;
        alpha *= 1.0 - 0.35 * smoothstep(0.7, 1.0, t);
        alpha = saturate(alpha) * saturate(0.55 - d) * base.a;
        col *= base.rgb;
    }
    else if (style == 2)
    {
        // coal
        float lump = (VNoise(lc / (1.4 * sc) + seed) - 0.5) * 2.0;
        d += lump * r * 0.16;
        n = normalize(n + float3(VNoise(lc / sc + seed) - 0.5, VNoise(lc / sc + seed + 3.0) - 0.5, 0.0) * 0.9);
        float dif = saturate(dot(n, L));
        float spec = pow(saturate(dot(n, normalize(L + float3(0.0, 0.0, 1.0)))), 24.0);
        col = float3(0.06, 0.06, 0.07) + dif * 0.11 + spec * 0.55;
        alpha = saturate(0.5 - d) * base.a;
    }
    else if (style == 3)
    {
        // carrot
        float dif = saturate(dot(n, L));
        col = lerp(float3(1.0, 0.56, 0.13), float3(0.86, 0.34, 0.06), t);
        float groove = smoothstep(0.55, 1.0, sin(t * 28.0 + seed * 3.0)) * 0.25;
        col *= (0.55 + 0.55 * dif) * (1.0 - groove);
        col += pow(saturate(dot(n, normalize(L + float3(0.0, 0.0, 1.0)))), 16.0) * 0.25;
        alpha = saturate(0.5 - d) * base.a;
    }
    else if (style == 4)
    {
        // wood (stick arms)
        float dif = saturate(dot(n, L));
        float bark = VNoise(float2(t * 22.0, nxy.y * 3.0) + seed);
        col = float3(0.40, 0.25, 0.13) * (0.55 + 0.55 * dif) * (0.82 + 0.32 * bark);
        alpha = saturate(0.5 - d) * base.a;
    }
    else if (style == 5)
    {
        // fabric (scarf / hat) with optional stripes across the axis
        float dif = saturate(dot(n, L));
        float3 c = base.rgb;
        if (v.prm.z > 0.0)
        {
            float s = frac(t * v.prm.z + v.prm.w);
            float m = smoothstep(0.28, 0.34, s) * (1.0 - smoothstep(0.60, 0.66, s));
            c = lerp(c, float3(0.97, 0.97, 0.95), m);
        }
        float knit = VNoise(lc / (1.3 * sc) + seed) * 0.12;
        col = c * (0.58 + 0.48 * dif) * (0.94 + knit);
        alpha = saturate(0.5 - d) * base.a;
    }
    else if (style == 6)
    {
        // soft puff / shadow
        float f = saturate(-d / max(max(ra, rb), 1.0) * 1.15);
        f = f * f * (3.0 - 2.0 * f);
        col = base.rgb;
        alpha = f * base.a;
    }
    else if (style == 8)
    {
        // water drop
        float hx = (nxy.x + 0.35) / 0.25;
        float hy = (nxy.y + 0.35) / 0.30;
        float hl = exp(-hx * hx - hy * hy);
        col = lerp(float3(0.66, 0.83, 1.0), float3(1.0, 1.0, 1.0), hl);
        alpha = (0.35 + 0.40 * nl + hl * 0.5) * saturate(0.5 - d) * base.a;
    }
    else
    {
        // flat / lightly shaded
        float dif = saturate(dot(n, L));
        col = base.rgb * lerp(1.0, 0.70 + 0.45 * dif, v.prm.z);
        alpha = saturate(0.5 - d) * base.a;
    }

    if (alpha <= 0.002)
        discard;
    col = saturate(col);
    return float4(col * alpha, alpha);
}

)HLSL"
R"HLSL(
// ---------------------------------------------------------------------------
// Snowflakes: soft dots, six-armed crystals and out-of-focus bokeh.
// ---------------------------------------------------------------------------
struct FlakeIn
{
    float4 a : FA;   // x, y, size (radius), rotation
    float4 b : FB;   // alpha, type, seed, depth
    uint vid : SV_VertexID;
};

struct FlakeV
{
    float4 pos : SV_Position;
    float2 lp : LPOS;
    float4 a : FA;
    float4 b : FB;
};

FlakeV FlakeVS(FlakeIn i)
{
    float ext = i.a.z * 1.8 + 1.5;
    float2 cr = Corner(i.vid) * 2.0 - 1.0;
    float2 p = i.a.xy + cr * ext;
    FlakeV o;
    o.pos = float4(ToClip(p), i.b.w, 1.0);
    o.lp = cr * ext;
    o.a = i.a;
    o.b = i.b;
    return o;
}

// Polynomial atan2 (Wine's HLSL compiler lacks the intrinsic).
float Atan2Approx(float y, float x)
{
    float ax = abs(x);
    float ay = abs(y);
    float mx = max(ax, ay);
    float mn = min(ax, ay);
    float a = mn / max(mx, 0.000001);
    float s = a * a;
    float r = ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a;
    if (ay > ax)
        r = 1.57079637 - r;
    if (x < 0.0)
        r = 3.14159274 - r;
    if (y < 0.0)
        r = -r;
    return r;
}

float SdSeg(float2 p, float2 a, float2 b)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float h = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * h);
}

float4 FlakePS(FlakeV v) : SV_Target
{
    float size = v.a.z;
    float rot = v.a.w;
    float alpha = v.b.x;
    int type = (int)(v.b.y + 0.5);
    float2 p = v.lp;
    float cs = cos(rot);
    float sn = sin(rot);
    p = float2(p.x * cs + p.y * sn, -p.x * sn + p.y * cs);
    float rr = length(p);
    float r = rr / max(size, 0.01);
    float core = 0.0;
    float halo = 0.0;
    if (type == 1 && size > 2.2)
    {
        float ang = Atan2Approx(p.y, p.x);
        float sector = 1.04719755;
        float a6 = ang - sector * floor(ang / sector + 0.5);
        float2 q = float2(cos(a6), abs(sin(a6))) * rr;
        float L = size;
        float th = max(0.09 * L, 0.5);
        float dArm = SdSeg(q, float2(0.0, 0.0), float2(L, 0.0));
        float b1 = 0.42 * L;
        float b2 = 0.68 * L;
        float dBr = min(SdSeg(q, float2(b1, 0.0), float2(b1 + 0.16 * L, 0.27 * L)),
                        SdSeg(q, float2(b2, 0.0), float2(b2 + 0.11 * L, 0.19 * L)));
        float dHex = rr - 0.17 * L;
        float dd = min(min(dArm, dBr) - th, dHex);
        core = saturate(0.5 - dd);
        core = saturate(core + exp(-r * r * 4.0) * 0.3);
        halo = smoothstep(1.3, 0.55, r) * 0.28;
    }
    else if (type == 2)
    {
        float disc = smoothstep(1.0, 0.80, r);
        float ring = smoothstep(0.68, 0.92, r) * smoothstep(1.0, 0.92, r);
        core = disc * 0.55 + ring * 0.35;
    }
    else
    {
        float c = smoothstep(1.0, 0.40, r);
        float glow = exp(-r * r * 1.5) * 0.35;
        core = saturate(c + glow * 0.5);
        halo = smoothstep(1.75, 0.9, r) * 0.30;
    }
    float a = core * alpha;
    float h = halo * gMisc.z * alpha * (1.0 - core);
    if (a + h <= 0.003)
        discard;
    float3 dark = float3(0.10, 0.14, 0.22);
    return float4(float3(1.0, 1.0, 1.0) * a + dark * h * (1.0 - a), a + h * (1.0 - a));
}
)HLSL";
