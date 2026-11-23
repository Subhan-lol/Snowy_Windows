#!/usr/bin/env python3
"""Generates res/snowy.ico: a snowflake on a night-sky rounded square.

Each size is drawn separately (thicker strokes at small sizes) and stored as
PNG inside the .ico container. Requires Pillow.
"""
import io
import math
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def draw_icon(size: int) -> Image.Image:
    ss = 4  # supersampling
    S = size * ss
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Rounded-square background with a vertical night-sky gradient.
    grad = Image.new("RGBA", (S, S))
    gp = grad.load()
    for y in range(S):
        t = y / (S - 1)
        r = int(70 * (1 - t) + 28 * t)
        g = int(64 * (1 - t) + 92 * t)
        b = int(196 * (1 - t) + 214 * t)
        for x in range(S):
            gp[x, y] = (r, g, b, 255)
    mask = Image.new("L", (S, S), 0)
    md = ImageDraw.Draw(mask)
    pad = int(S * 0.03)
    md.rounded_rectangle([pad, pad, S - pad - 1, S - pad - 1], radius=int(S * 0.22), fill=255)
    img.paste(grad, (0, 0), mask)

    # A little snow cap on the top edge (only visible at larger sizes).
    if size >= 32:
        cap = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        cd = ImageDraw.Draw(cap)
        h = S * 0.11
        cd.rounded_rectangle([pad + S * 0.06, pad - h * 0.25, S - pad - S * 0.06, pad + h], radius=int(h * 0.6),
                             fill=(255, 255, 255, 235))
        img = Image.alpha_composite(img, cap)

    # Snowflake
    flake = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    fd = ImageDraw.Draw(flake)
    cx = cy = S / 2 + (S * 0.03 if size >= 32 else 0)
    R = S * (0.34 if size >= 32 else 0.38)
    w = max(S * (0.055 if size >= 32 else 0.09), ss * 1.2)
    white = (255, 255, 255, 255)
    for k in range(6):
        a = math.pi / 2 + k * math.pi / 3
        dx, dy = math.cos(a), -math.sin(a)
        x1, y1 = cx + dx * R, cy + dy * R
        fd.line([(cx, cy), (x1, y1)], fill=white, width=int(w))
        fd.ellipse([x1 - w / 2, y1 - w / 2, x1 + w / 2, y1 + w / 2], fill=white)
        if size >= 24:
            for frac, blen in ((0.55, 0.30), (0.78, 0.2)):
                bx, by = cx + dx * R * frac, cy + dy * R * frac
                for side in (-1, 1):
                    ba = a + side * math.pi / 3
                    ex, ey = bx + math.cos(ba) * R * blen, by - math.sin(ba) * R * blen
                    fd.line([(bx, by), (ex, ey)], fill=white, width=int(w * 0.8))
                    fd.ellipse([ex - w * 0.4, ey - w * 0.4, ex + w * 0.4, ey + w * 0.4], fill=white)
    hexr = R * 0.16
    fd.regular_polygon((cx, cy, hexr), 6, fill=white)
    shadow = flake.split()[3].filter(ImageFilter.GaussianBlur(S * 0.02))
    sh = Image.new("RGBA", (S, S), (10, 20, 60, 0))
    sh.putalpha(shadow.point(lambda v: int(v * 0.45)))
    img = Image.alpha_composite(img, sh)
    img = Image.alpha_composite(img, flake)
    return img.resize((size, size), Image.LANCZOS)


def write_ico(path: Path, images):
    entries = []
    blobs = []
    for im in images:
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        blobs.append(buf.getvalue())
    offset = 6 + 16 * len(images)
    header = struct.pack("<HHH", 0, 1, len(images))
    for im, blob in zip(images, blobs):
        w, h = im.size
        entries.append(struct.pack("<BBBBHHII", 0 if w >= 256 else w, 0 if h >= 256 else h, 0, 0, 1, 32,
                                   len(blob), offset))
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(header)
        for e in entries:
            f.write(e)
        for b in blobs:
            f.write(b)


def main():
    root = Path(__file__).resolve().parent.parent
    out = root / "res" / "snowy.ico"
    imgs = [draw_icon(s) for s in SIZES]
    write_ico(out, imgs)
    imgs[-1].save(root / "docs" / "icon.png")
    print("wrote", out)


if __name__ == "__main__":
    sys.exit(main())
