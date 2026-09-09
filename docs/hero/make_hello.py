#!/usr/bin/env python3
"""Generate the hero note: a cursive 'hello' in one flowing stroke, pixel art
around it, and a typed line about agents. Writes hello.omascribe."""
import json, math, uuid, sys, os
K = float(os.environ.get("HERO_SCALE", "1"))   # layout scale: 1 for zen (1200 x 750), ~0.62 for a square window

def uid(): return str(uuid.uuid4())

def catmull(points, samples=24):
    """Smooth curve through points (Catmull-Rom), returns dense list of (x, y)."""
    pts = [points[0]] + points + [points[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i-1], pts[i], pts[i+1], pts[i+2]
        for k in range(samples):
            t = k / samples
            t2, t3 = t*t, t*t*t
            x = 0.5*((2*p1[0]) + (-p0[0]+p2[0])*t + (2*p0[0]-5*p1[0]+4*p2[0]-p3[0])*t2 + (-p0[0]+3*p1[0]-3*p2[0]+p3[0])*t3)
            y = 0.5*((2*p1[1]) + (-p0[1]+p2[1])*t + (2*p0[1]-5*p1[1]+4*p2[1]-p3[1])*t2 + (-p0[1]+3*p1[1]-3*p2[1]+p3[1])*t3)
            out.append((x, y))
    out.append(points[-1])
    return out

def pressure_along(curve):
    """Downstrokes heavy, upstrokes light: a broad-nib feel."""
    p = []
    for i, (x, y) in enumerate(curve):
        if i == 0: p.append(0.5); continue
        dx, dy = x - curve[i-1][0], y - curve[i-1][1]
        L = math.hypot(dx, dy) or 1
        down = dy / L            # +1 going down the page
        p.append(0.62 + 0.38 * max(0.0, down))
    # smooth
    q = p[:]
    for i in range(1, len(p)-1):
        q[i] = (p[i-1] + p[i] + p[i+1]) / 3
    return q

def stroke(curve, color="ink", width=6.0):
    pr = pressure_along(curve)
    flat = []
    for (x, y), p in zip(curve, pr):
        flat += [round(x, 2), round(y, 2), round(min(1.0, max(0.1, p)), 3)]
    return {"id": uid(), "tool": "fineliner", "color": color, "width": width, "p": flat}

# ---- 'hello' from the Hershey "scripts" single-stroke cursive, y flipped, smoothed, pressure added.
from hershey_hello import hello_strokes
strokes, hello_h = hello_strokes("scripts", "hello", 150 * K, 230 * K, 600 * K, 11.0 * K)

# ---- pixel art: dot pixels on a 12 px grid. 1 = ink, 2 = blue, 3 = red, 4 = gray
def pixels(art, x0, y0, cell=13, gap=1.5):
    x0, y0, cell, gap = x0 * K, y0 * K, cell * K, gap * K
    """Each pixel is three straight vertical passes (no turns), round caps ending at the cell edge."""
    cmap = {"1": "ink", "2": "blue", "3": "red", "4": "gray"}
    out = []
    side = cell - gap
    w = side / 3 * 1.18
    out_strokes = []
    for r, row in enumerate(art):
        for c, ch in enumerate(row):
            if ch in cmap:
                x, y = x0 + c * cell, y0 + r * cell
                for k in range(3):
                    cx = x + side * (2 * k + 1) / 6
                    y1, y2 = y + w / 2, y + side - w / 2
                    out.append({"id": uid(), "tool": "fineliner", "color": cmap[ch], "width": round(w, 2),
                                "p": [round(cx, 2), round(y1, 2), 1.0, round(cx, 2), round(y2, 2), 1.0]})
    return out

pencil = [   # a pencil, tip down-left
    "......3333",
    ".....31113",
    "....31113.",
    "...31113..",
    "..31113...",
    ".31113....",
    "44113.....",
    "441......",
    "4.........",
]
heart = [
    ".33..33.",
    "3333333 3",
    "33333333",
    ".333333.",
    "..3333..",
    "...33...",
]
laptop = [   # a small Framework-ish laptop, screen with a pen glyph
    "1111111111111",
    "1444444444441",
    "1442444424441",
    "1444244244441",
    "1444424444441",
    "1444444444441",
    "1111111111111",
    "444444444444444",
    "111111111111111",
]
cursor = [
    "1......",
    "11.....",
    "111....",
    "1111...",
    "11111..",
    "111111.",
    "111....",
    "1.11...",
    "...11..",
]
star = [
    "....2....",
    "...222...",
    "..22222..",
    "222222222",
    "..22222..",
    "...222...",
    "....2....",
]
sparkle = [
    "..2..",
    ".222.",
    "22222",
    ".222.",
    "..2..",
]
strokes += pixels(pencil, 760, 120)
strokes += pixels(heart, 1000, 320)
strokes += pixels(laptop, 60, 540)
strokes += pixels(cursor, 830, 300)
strokes += pixels(star, 60, 100)
strokes += pixels(sparkle, 420, 70)
strokes += pixels(sparkle, 930, 560)

texts = [{
    "id": uid(), "x": 300 * K, "y": 540 * K, "width": 560 * K, "size": 17 if K >= 0.9 else 14,
    "text": "Pen first, keyboard second. Ink stays vector, notes stay yours.\n\n"
            "Agents can read your notes and see your sketches, if you let them: "
            "the live readout hands them the page as JSON, SVG and PNG. "
            "Great for agentic development.",
}]

doc = {"format": "omascribe", "version": 1, "id": "hero-hello",
       "title": "Omascribe", "created": "2026-09-09T10:00:00.000Z", "modified": "2026-09-09T10:05:00.000Z",
       "height": 1400, "strokes": strokes, "texts": texts}
out = sys.argv[1] if len(sys.argv) > 1 else "hello.omascribe"
json.dump(doc, open(out, "w"))
print("strokes", len(strokes), "->", out)
