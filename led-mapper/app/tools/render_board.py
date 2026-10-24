"""Render the front of a .kicad_pcb (copper under mask, exposed pads, silkscreen)
to an image cropped to the board edge, for the LED Mapper's board view.

    python render_board.py Live_Map_4layers.kicad_pcb ../data/board_render.jpg [px_per_mm]

Needs Python 3 and Pillow (pip install pillow). Board text comes from the
outlines KiCad caches in the file, so it matches KiCad exactly.
"""
import sys, math, re
from PIL import Image, ImageDraw, ImageFont

def parse(s):
    tok = re.compile(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()"]+')
    stack = [[]]
    for m in tok.finditer(s):
        t = m.group(0)
        if t == '(': stack.append([])
        elif t == ')': x = stack.pop(); stack[-1].append(x)
        elif t[0] == '"': stack[-1].append(t[1:-1])
        else: stack[-1].append(t)
    return stack[0][0]
def find(n, k): return [c for c in n if isinstance(c, list) and c and c[0] == k]
def one(n, k):
    r = find(n, k); return r[0] if r else None

SRC, OUT, PX = sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 16
pcb = parse(open(SRC).read())
FONT = next((f for f in ('/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf', 'C:/Windows/Fonts/arialbd.ttf', '/Library/Fonts/Arial Bold.ttf') if __import__('os').path.exists(f)), None)
C_FR4, C_CU, C_PAD, C_SILK, C_HOLE = (12, 36, 27), (21, 60, 44), (201, 164, 58), (250, 250, 248), (8, 12, 10)

rect = [n for n in find(pcb, 'gr_rect') if one(n, 'layer')[1] == 'Edge.Cuts'][0]
X0, Y0 = float(one(rect, 'start')[1]), float(one(rect, 'start')[2])
X1, Y1 = float(one(rect, 'end')[1]), float(one(rect, 'end')[2])
W, H = round((X1 - X0) * PX), round((Y1 - Y0) * PX)
img = Image.new('RGB', (W, H), C_FR4)
d = ImageDraw.Draw(img)
P = lambda x, y: ((x - X0) * PX, (y - Y0) * PX)

def xy(pts): return [(float(p[1]), float(p[2])) for p in pts if p[0] == 'xy']
def layer_of(n):
    l = one(n, 'layer'); return l[1] if l else None
def width_of(n):
    s = one(n, 'stroke'); w = one(s, 'width') if s else one(n, 'width')
    return float(w[1]) if w else 0.12

def fp_xform(fp):
    at = one(fp, 'at'); fx, fy = float(at[1]), float(at[2]); r = math.radians(float(at[3]) if len(at) > 3 else 0)
    c, s = math.cos(r), math.sin(r)
    return lambda x, y: (fx + x * c + y * s, fy - x * s + y * c)

def line(pts, w, col):
    pts = [P(*p) for p in pts]
    d.line(pts, fill=col, width=max(1, round(w * PX)), joint='curve')
    r = w * PX / 2
    for x, y in (pts[0], pts[-1]): d.ellipse([x - r, y - r, x + r, y + r], fill=col)

def bezier(p0, p1, p2, p3, n=24):
    out = []
    for i in range(n + 1):
        t = i / n; u = 1 - t
        out.append((u**3*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t**3*p3[0], u**3*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t**3*p3[1]))
    return out

def arc3(a, m, b, n=24):
    (ax, ay), (mx, my), (bx, by) = a, m, b
    dd = 2 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my))
    if abs(dd) < 1e-9: return [a, b]
    ux = ((ax*ax+ay*ay)*(my-by) + (mx*mx+my*my)*(by-ay) + (bx*bx+by*by)*(ay-my)) / dd
    uy = ((ax*ax+ay*ay)*(bx-mx) + (mx*mx+my*my)*(ax-bx) + (bx*bx+by*by)*(mx-ax)) / dd
    r = math.hypot(ax-ux, ay-uy)
    ta, tm, tb = (math.atan2(p[1]-uy, p[0]-ux) for p in (a, m, b))
    def norm(t): return (t - ta) % (2*math.pi)
    sweep = norm(tb)
    if norm(tm) > sweep: sweep = sweep - 2*math.pi
    return [(ux + r*math.cos(ta + sweep*i/n), uy + r*math.sin(ta + sweep*i/n)) for i in range(n + 1)]

def shape(n, layer, col, T=lambda x, y: (x, y)):
    """Draw one graphic item (gr_* or fp_*) if it is on layer."""
    if layer_of(n) != layer: return
    k = n[0].split('_', 1)[1]
    w = width_of(n)
    fill = one(n, 'fill'); filled = fill and fill[1] in ('yes', 'solid')
    if k == 'line':
        line([T(*xy([one(n, 'start')])[0] if False else (float(one(n,'start')[1]), float(one(n,'start')[2]))), T(float(one(n,'end')[1]), float(one(n,'end')[2]))], w, col)
    elif k == 'rect':
        (sx, sy), (ex, ey) = (float(one(n,'start')[1]), float(one(n,'start')[2])), (float(one(n,'end')[1]), float(one(n,'end')[2]))
        pts = [T(sx, sy), T(ex, sy), T(ex, ey), T(sx, ey)]
        if filled: d.polygon([P(*p) for p in pts], fill=col)
        line(pts + [pts[0]], w, col) if w > 0 else None
    elif k == 'poly':
        pts = [T(*p) for p in xy(one(n, 'pts'))]
        if filled or w == 0: d.polygon([P(*p) for p in pts], fill=col)
        if w > 0: line(pts + [pts[0]], w, col)
    elif k == 'circle':
        c = T(float(one(n,'center')[1]), float(one(n,'center')[2])); e = T(float(one(n,'end')[1]), float(one(n,'end')[2]))
        r = math.hypot(e[0]-c[0], e[1]-c[1]); (cx, cy) = P(*c); R = r * PX
        if filled: d.ellipse([cx-R, cy-R, cx+R, cy+R], fill=col)
        else: d.ellipse([cx-R, cy-R, cx+R, cy+R], outline=col, width=max(1, round(w*PX)))
    elif k == 'arc':
        a = (float(one(n,'start')[1]), float(one(n,'start')[2])); m = (float(one(n,'mid')[1]), float(one(n,'mid')[2])); b = (float(one(n,'end')[1]), float(one(n,'end')[2]))
        line([T(*p) for p in arc3(a, m, b)], w, col)
    elif k == 'curve':
        p = xy(one(n, 'pts')); line([T(*q) for q in bezier(*p)], w, col)

_fonts = {}
def font(px):
    px = max(4, int(round(px)))
    if px not in _fonts: _fonts[px] = ImageFont.truetype(FONT, px) if FONT else ImageFont.load_default()
    return _fonts[px]

def text(s, x, y, rot, size, justify, col):
    s = s.replace('\\n', '\n').rstrip('\n')
    if not s.strip(): return
    f = font(size * PX * 1.37)          # KiCad's size is roughly cap height; Liberation caps are ~0.73 em
    lines = s.split('\n')
    lh = size * PX * 1.62
    widths = [f.getlength(l) for l in lines]
    tw, th = max(widths), lh * len(lines)
    pad = 4
    tile = Image.new('L', (int(tw + 2*pad), int(th + 2*pad)), 0)
    td = ImageDraw.Draw(tile)
    for i, l in enumerate(lines):
        if 'right' in justify: lx = tw - widths[i]
        elif 'left' in justify: lx = 0
        else: lx = (tw - widths[i]) / 2
        td.text((pad + lx, pad + i * lh + lh / 2), l, font=f, fill=255, anchor='lm')
    # anchor point inside the tile
    ax = pad if 'left' in justify else (pad + tw if 'right' in justify else pad + tw / 2)
    ay = pad + lh / 2 if 'top' in justify else (pad + th - lh / 2 if 'bottom' in justify else pad + th / 2)
    if len(lines) > 1 and not ('top' in justify or 'bottom' in justify): ay = pad + th / 2
    big = Image.new('L', (tile.width * 3, tile.height * 3), 0)
    ox, oy = tile.width, tile.height
    big.paste(tile, (ox, oy))
    rot_img = big.rotate(rot, resample=Image.BICUBIC, center=(ox + ax, oy + ay))
    cx, cy = P(x, y)
    colimg = Image.new('RGB', rot_img.size, col)
    img.paste(colimg, (int(round(cx - ox - ax)), int(round(cy - oy - ay))), rot_img)

TEXT_MASK = Image.new('L', (W, H), 0)
tm = ImageDraw.Draw(TEXT_MASK)
def cached_text(n):
    # KiCad stores the exact outline of TrueType text in render_cache: polygons whose
    # first pts list is the outline and the rest are holes (the inside of an 'o').
    rc = one(n, 'render_cache')
    if not rc: return False
    for poly in find(rc, 'polygon'):
        rings = find(poly, 'pts')
        for k, ring in enumerate(rings):
            pts = [P(*p) for p in xy(ring)]
            if len(pts) >= 3: tm.polygon(pts, fill=0 if k else 255)
    return True

def effects(n):
    e = one(n, 'effects') or []
    fnt = one(e, 'font') or []; sz = one(fnt, 'size'); j = one(e, 'justify')
    hidden = (one(e, 'hide') or one(n, 'hide') or [None, 'no'])
    return (float(sz[1]) if sz else 1.0), (' '.join(j[1:]) if j else ''), (len(hidden) > 1 and hidden[1] == 'yes') or hidden == ['hide']

# 1. copper under the mask: zones, tracks, pads, vias
for z in find(pcb, 'zone'):
    for fpoly in find(z, 'filled_polygon'):
        if layer_of(fpoly) == 'F.Cu':
            d.polygon([P(*p) for p in xy(one(fpoly, 'pts'))], fill=C_CU)
for s in find(pcb, 'segment'):
    if layer_of(s) == 'F.Cu':
        line([(float(one(s,'start')[1]), float(one(s,'start')[2])), (float(one(s,'end')[1]), float(one(s,'end')[2]))], float(one(s,'width')[1]), C_CU)
for a in find(pcb, 'arc'):
    if layer_of(a) == 'F.Cu':
        line(arc3(*[(float(one(a,k)[1]), float(one(a,k)[2])) for k in ('start','mid','end')]), float(one(a,'width')[1]), C_CU)
for v in find(pcb, 'via'):
    x, y = P(float(one(v,'at')[1]), float(one(v,'at')[2])); r = float(one(v,'size')[1]) * PX / 2
    d.ellipse([x-r, y-r, x+r, y+r], fill=C_CU)

def pad_poly(p, T):
    at = one(p, 'at'); px, py = float(at[1]), float(at[2]); prot = float(at[3]) if len(at) > 3 else 0
    sx, sy = float(one(p,'size')[1]), float(one(p,'size')[2])
    return px, py, prot, sx, sy

def draw_pads(fp, col, need_mask):
    T = fp_xform(fp); frot = float(one(fp,'at')[3]) if len(one(fp,'at')) > 3 else 0
    for p in find(fp, 'pad'):
        layers = one(p, 'layers')[1:]
        on_cu = 'F.Cu' in layers or '*.Cu' in layers
        on_mask = 'F.Mask' in layers or '*.Mask' in layers
        if not on_cu or (need_mask and not on_mask): continue
        px, py, prot, sx, sy = pad_poly(p, T)
        cx, cy = T(px, py)
        kind = p[3]
        r = math.radians(prot)  # pad angle in the file is absolute
        c, s = math.cos(r), math.sin(r)
        corner = lambda u, v: P(cx + u * c + v * s, cy - u * s + v * c)
        if kind in ('circle',) or (kind == 'oval' and abs(sx - sy) < 1e-6):
            X, Y = P(cx, cy); R = sx * PX / 2
            d.ellipse([X-R, Y-R, X+R, Y+R], fill=col)
        elif kind == 'oval':
            rr = min(sx, sy) / 2; long = (max(sx, sy) - min(sx, sy)) / 2
            a, b = ((-long, 0), (long, 0)) if sx > sy else ((0, -long), (0, long))
            line([(cx + a[0]*c + a[1]*s, cy - a[0]*s + a[1]*c), (cx + b[0]*c + b[1]*s, cy - b[0]*s + b[1]*c)], 2 * rr, col)
        else:
            d.polygon([corner(-sx/2, -sy/2), corner(sx/2, -sy/2), corner(sx/2, sy/2), corner(-sx/2, sy/2)], fill=col)
        dr = one(p, 'drill')
        if dr and len(dr) > 1 and dr[1] != 'oval':
            X, Y = P(cx, cy); R = float(dr[1]) * PX / 2
            d.ellipse([X-R, Y-R, X+R, Y+R], fill=C_HOLE)

fps = find(pcb, 'footprint')
for fp in fps:
    if layer_of(fp) == 'F.Cu': draw_pads(fp, C_CU, False)

# 2. silkscreen (drawn before exposed pads, which clip it on a real board)
for n in pcb:
    if isinstance(n, list) and n[0] in ('gr_line', 'gr_rect', 'gr_poly', 'gr_circle', 'gr_arc', 'gr_curve'):
        shape(n, 'F.SilkS', C_SILK)
for fp in fps:
    T = fp_xform(fp)
    for n in fp:
        if isinstance(n, list) and n[0] in ('fp_line', 'fp_rect', 'fp_poly', 'fp_circle', 'fp_arc', 'fp_curve'):
            shape(n, 'F.SilkS', C_SILK, T)
for n in find(pcb, 'gr_text'):
    if layer_of(n) != 'F.SilkS': continue
    at = one(n, 'at'); size, just, hidden = effects(n)
    if not hidden and not cached_text(n): text(n[1], float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0, size, just, C_SILK)
for fp in fps:
    T = fp_xform(fp)
    for n in fp:
        if isinstance(n, list) and n[0] in ('fp_text', 'property') and layer_of(n) == 'F.SilkS':
            size, just, hidden = effects(n)
            if hidden: continue
            at = one(n, 'at'); s = n[2] if n[0] == 'property' or n[1] in ('reference', 'value', 'user') else n[1]
            if n[0] == 'fp_text': s = n[2]
            if cached_text(n): continue
            x, y = T(float(at[1]), float(at[2]))
            text(s, x, y, float(at[3]) if len(at) > 3 else 0, size, just, C_SILK)

img.paste(Image.new('RGB', (W, H), C_SILK), (0, 0), TEXT_MASK)

# 3. exposed pads (mask openings) and plain holes
for fp in fps:
    if layer_of(fp) == 'F.Cu': draw_pads(fp, C_PAD, True)
    T = fp_xform(fp)
    for p in find(fp, 'pad'):
        if p[2] == 'np_thru_hole':
            cx, cy = T(float(one(p,'at')[1]), float(one(p,'at')[2])); X, Y = P(cx, cy); R = float(one(p,'drill')[1]) * PX / 2
            d.ellipse([X-R, Y-R, X+R, Y+R], fill=C_HOLE)

# rounded board corners
radius = one(rect, 'radius'); R = float(radius[1]) * PX if radius else 0
mask = Image.new('L', (W, H), 0); ImageDraw.Draw(mask).rounded_rectangle([0, 0, W - 1, H - 1], radius=R, fill=255)
out = Image.new('RGB', (W, H), (255, 255, 255)); out.paste(img, (0, 0), mask)
out.save(OUT, quality=88)
print(W, H, X0, Y0, X1, Y1)
