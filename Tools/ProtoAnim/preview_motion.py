"""Motion previews: what happens BETWEEN the keys.

  python preview_motion.py [name-filter]   ->  previews/<clip>_strip.png, previews/<clip>_curves.png

strip:  the clip every STEP seconds (side view on top, three-quarter view below), faint trail of the striking limb and
        the pelvis through the whole clip, the impact frame boxed.
curves: speeds through the clip, each normalised to its own peak, to read the kinetic chain: a punch should peak
        pelvis -> chest -> shoulder -> elbow -> fist in that order, each rising and falling once; a kick support-side
        hip -> kicking knee -> foot. Grey verticals are keys, the red one the impact; the bottom band is balance (how
        far the centre of mass is from the middle of the planted feet, red when outside them).
"""
import json, math, os, sys
from protoanim import to_cs, sub, add, mul, length, norm, qmul, qconj, HERE
from preview import Canvas, png, DRAW, color
from motioncheck import MASS, FLOOR_BALL, FLOOR_ANK, PLANT_TOL, hull_dist

STEP = 0.05
WINDOW = None   # (t0, t1): --from / --to on the command line

def proj_side(p): return (p[1], p[2])
def proj_34(p):   # from front-right, a little above
    a = math.radians(35)
    x = p[1] * math.cos(a) - p[0] * math.sin(a); depth = p[1] * math.sin(a) + p[0] * math.cos(a)
    return (x, p[2] + 0.18 * depth)

def com(cs):
    c = [0, 0, 0]
    for a, b, m in MASS:
        q = cs[a][1] if b is None else mul(add(cs[a][1], cs[b][1]), 0.5); c = add(c, mul(q, m))
    return mul(c, 1 / sum(m for *_, m in MASS))

def strip(name, clip, frames):
    fps = clip['fps']; n = len(frames)
    a = int(WINDOW[0] * fps) if WINDOW else 0
    b = min(int(WINDOW[1] * fps), n - 1) if WINDOW else n - 1
    idx = list(range(a, b + 1, max(1, int(round(STEP * fps)))))
    if idx[-1] != b: idx.append(b)
    cell_w, cell_h = 150, 230
    cv = Canvas(cell_w * len(idx), cell_h * 2)
    side = 'l' if name.endswith('_L') or '_L_' in name else 'r'
    limb = ('ball_' if 'Kick' in name else 'hand_') + side
    impact = clip.get('impact')
    for row, proj in enumerate((proj_side, proj_34)):
        for ci, f in enumerate(idx):
            ox, oy = ci * cell_w, row * cell_h
            for x in range(cell_w): cv.dot(ox + x, oy, (210, 210, 210), 0)
            for y in range(cell_h): cv.dot(ox, oy + y, (210, 210, 210), 0)
            def to2(p):
                u, v = proj(p); return ox + cell_w / 2 + u * 0.95 - 15, oy + cell_h - 14 - v * 0.95
            cv.line(to2([0, -60, 0]), to2([0, 110, 0]), (190, 190, 190), 0)
            # trails through the whole clip (faint), up to this frame (darker)
            for k in range(1, n):
                for bone, col in ((limb, (230, 150, 40)), ('pelvis', (120, 120, 120))):
                    c = col if k <= f else tuple(min(255, int(v * 0.35 + 255 * 0.65)) for v in col)
                    cv.line(to2(frames[k - 1][bone][1]), to2(frames[k][bone][1]), c, 0)
            cs = frames[f]
            for b in DRAW: cv.line(to2(cs[cs and b and _parent(b)][1]), to2(cs[b][1]), color(b), 1)
            cv.dot(*to2(cs['head'][1]), (0, 0, 0), 3)
            g = com(cs); cv.dot(*to2([g[0], g[1], 0]), (20, 160, 20), 2)
            if impact is not None and abs(f / fps - impact) < 0.5 * STEP + 1e-6:
                for x in range(cell_w): cv.dot(ox + x, oy + 2, (220, 30, 30), 1)
    png(os.path.join(HERE, 'previews', name + '_strip.png'), cv.w, cv.h, cv.px)

from protoanim import PARENT
def _parent(b): return PARENT[b]

def yaw_rate(frames, a, b, fps):
    out = [0.0]
    for i in range(1, len(frames)):
        def yaw(cs): v = sub(cs[a][1], cs[b][1]); return math.atan2(v[1], v[0])
        d = (yaw(frames[i]) - yaw(frames[i - 1]) + math.pi) % (2 * math.pi) - math.pi
        out.append(abs(math.degrees(d)) * fps)
    return out

def lin(frames, bone, fps):
    return [0.0] + [length(sub(frames[i][bone][1], frames[i - 1][bone][1])) * fps for i in range(1, len(frames))]

def curves(name, clip, frames):
    fps = clip['fps']; n = len(frames)
    side = 'l' if name.endswith('_L') or '_L_' in name else 'r'
    kick = 'Kick' in name
    series = [('pelvis yaw', yaw_rate(frames, 'thigh_l', 'thigh_r', fps), (90, 90, 90)),
              ('chest yaw', yaw_rate(frames, 'upperarm_l', 'upperarm_r', fps), (40, 140, 40))]
    if kick:
        series += [('knee', lin(frames, 'calf_' + side, fps), (60, 90, 220)), ('foot', lin(frames, 'ball_' + side, fps), (230, 120, 20))]
    else:
        series += [('shoulder', lin(frames, 'upperarm_' + side, fps), (150, 60, 200)), ('elbow', lin(frames, 'lowerarm_' + side, fps), (60, 90, 220)),
                   ('fist', lin(frames, 'hand_' + side, fps), (230, 120, 20))]
    series.append(('head', lin(frames, 'head', fps), (0, 0, 0)))
    W, H, band = max(600, n * 6), 300, 40
    cv = Canvas(W, H + band + 10)
    X = lambda i: 10 + (W - 20) * i / max(n - 1, 1)
    for k in clip.get('keys', []):
        for y in range(H): cv.dot(X(k * fps), y, (215, 215, 215), 0)
    if clip.get('impact') is not None:
        for y in range(H + band): cv.dot(X(clip['impact'] * fps), y, (230, 60, 60), 0)
    for li, (label, s, col) in enumerate(series):
        top = max(s) or 1.0
        pts = [(X(i), H - 10 - (H - 30) * v / top) for i, v in enumerate(s)]
        for a, b in zip(pts, pts[1:]): cv.line(a, b, col, 1)
        for x in range(14): cv.dot(12 + x + li * 40, 8, col, 1)   # legend swatches, in order
    # balance band
    for i, cs in enumerate(frames):
        g = com(cs); pts = []
        for s in 'lr':
            if cs['ball_' + s][1][2] < FLOOR_BALL[s] + PLANT_TOL:
                pts.append(tuple(cs['ball_' + s][1][:2]))
                if cs['foot_' + s][1][2] < FLOOR_ANK[s] + 4: pts.append(tuple(cs['foot_' + s][1][:2]))
        d = max(hull_dist(tuple(g[:2]), pts) - 4.0, 0) if pts else 99
        c = (30, 160, 30) if d <= 0 else (220, 40, 40)
        h = min(band, 4 + d * 2)
        cv.line((X(i), H + band), (X(i), H + band - h), c, 0)
    png(os.path.join(HERE, 'previews', name + '_curves.png'), cv.w, cv.h, cv.px)

if __name__ == '__main__':
    args = sys.argv[1:]
    if '--from' in args:
        i = args.index('--from'); j = args.index('--to'); WINDOW = (float(args[i + 1]), float(args[j + 1]))
        args = [a for k, a in enumerate(args) if k not in (i, i + 1, j, j + 1)]
    clips = json.load(open(os.path.join(HERE, 'clips.json')))
    for name, clip in clips.items():
        if args and not any(a in name for a in args): continue
        frames = [to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]
        strip(name, clip, frames); curves(name, clip, frames)
        print('previews/' + name + '_strip.png / _curves.png')
