"""Turn a Hershey single-stroke font rendering of 'hello' into omascribe strokes."""
import json, math, sys, uuid
from HersheyFonts import HersheyFonts

def uid(): return str(uuid.uuid4())

def polylines(font, text):
    """Chain the font's line segments into polylines."""
    segs = list(font.lines_for_text(text))
    polys = []
    cur = []
    for (a, b) in segs:
        if cur and cur[-1] == a:
            cur.append(b)
        else:
            if cur: polys.append(cur)
            cur = [a, b]
    if cur: polys.append(cur)
    return polys

def catmull(points, samples=8):
    if len(points) < 3:
        return points
    pts = [points[0]] + points + [points[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i-1], pts[i], pts[i+1], pts[i+2]
        for k in range(samples):
            t = k / samples; t2, t3 = t*t, t*t*t
            x = 0.5*((2*p1[0]) + (-p0[0]+p2[0])*t + (2*p0[0]-5*p1[0]+4*p2[0]-p3[0])*t2 + (-p0[0]+3*p1[0]-3*p2[0]+p3[0])*t3)
            y = 0.5*((2*p1[1]) + (-p0[1]+p2[1])*t + (2*p0[1]-5*p1[1]+4*p2[1]-p3[1])*t2 + (-p0[1]+3*p1[1]-3*p2[1]+p3[1])*t3)
            out.append((x, y))
    out.append(points[-1])
    return out

def pressure_along(curve):
    p = []
    for i, (x, y) in enumerate(curve):
        if i == 0: p.append(0.7); continue
        dx, dy = x - curve[i-1][0], y - curve[i-1][1]
        L = math.hypot(dx, dy) or 1
        p.append(0.62 + 0.38 * max(0.0, dy / L))
    q = p[:]
    for i in range(1, len(p)-1): q[i] = (p[i-1] + p[i] + p[i+1]) / 3
    return q

def hello_strokes(fontname, text, x0, y0, target_w, width):
    f = HersheyFonts()
    f.load_default_font(fontname)
    f.normalize_rendering(100)
    polys = polylines(f, text)
    xs = [p[0] for pl in polys for p in pl]; ys = [p[1] for pl in polys for p in pl]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    scale = target_w / (maxx - minx)
    strokes = []
    for pl in polys:
        pts = [(x0 + (x - minx) * scale, y0 + (maxy - y) * scale) for x, y in pl]  # Hershey is y-up
        ext = max(max(a[0] for a in pts) - min(a[0] for a in pts), max(a[1] for a in pts) - min(a[1] for a in pts))
        if ext < 6:   # a stray dot in the glyph data, not part of the word
            continue
        curve = catmull(pts, 8)
        pr = pressure_along(curve)
        flat = []
        for (x, y), p in zip(curve, pr): flat += [round(x, 2), round(y, 2), round(min(1, max(0.1, p)), 3)]
        strokes.append({"id": uid(), "tool": "fineliner", "color": "ink", "width": width, "p": flat})
    h = (maxy - miny) * scale
    return strokes, h

if __name__ == "__main__":
    fontname = sys.argv[1]
    strokes, h = hello_strokes(fontname, "hello", 140, 200, 600, 11.0)
    doc = {"format": "omascribe", "version": 1, "id": "t", "title": fontname, "strokes": strokes}
    json.dump(doc, open(f"h-{fontname}.omascribe", "w"))
    print(fontname, "polylines", len(strokes), "height", round(h))
