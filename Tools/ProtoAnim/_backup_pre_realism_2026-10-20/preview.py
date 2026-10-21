"""Validate clips.json and render stick-figure contact sheets (PNG) for each clip."""
import json, os, zlib, struct, math
from protoanim import to_cs, BONES, PARENT, qrot, sub, length, HERE

SKIP = ('twist', 'index', 'middle', 'ring', 'pinky', 'thumb', 'ik_', 'interaction', 'center_of_mass', 'corrective', 'metacarpal')
DRAW = [b for b in BONES if PARENT[b] and not any(k in b for k in SKIP) and not any(k in PARENT[b] for k in SKIP)]

def png(path, w, h, px):
    raw = b''.join(b'\x00' + bytes(px[y*w*3:(y+1)*w*3]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

class Canvas:
    def __init__(s, w, h): s.w, s.h = w, h; s.px = bytearray([250] * (w*h*3))
    def dot(s, x, y, c, r=1):
        for dy in range(-r, r+1):
            for dx in range(-r, r+1):
                X, Y = int(x)+dx, int(y)+dy
                if 0 <= X < s.w and 0 <= Y < s.h: s.px[(Y*s.w+X)*3:(Y*s.w+X)*3+3] = bytes(c)
    def line(s, a, b, c, r=1):
        n = int(max(abs(b[0]-a[0]), abs(b[1]-a[1]))) + 1
        for i in range(n+1): s.dot(a[0]+(b[0]-a[0])*i/n, a[1]+(b[1]-a[1])*i/n, c, r)

def color(b):
    if b.endswith('_r'): return (200, 60, 50)     # right side red
    if b.endswith('_l'): return (40, 90, 200)     # left side blue
    return (40, 40, 40)

def render(clip, path, times):
    cell, pad = 230, 6
    views = [('side', lambda p: (p[1], p[2])),            # forward to the right
             ('front', lambda p: (-p[0], p[2])),          # viewer faces the character
             ('top', lambda p: (p[1], p[0]))]             # forward right, left side up
    cv = Canvas(cell * len(times), cell * 3)
    for ci, t in enumerate(times):
        f = min(int(round(t * clip['fps'])), clip['frames'] - 1)
        cs = to_cs({b: tuple(v) for b, v in clip['poses'][f].items()})
        for vi, (_, proj) in enumerate(views):
            ox, oy = ci * cell, vi * cell
            for x in range(cell): cv.dot(ox + x, oy, (200, 200, 200), 0)
            for y in range(cell): cv.dot(ox, oy + y, (200, 200, 200), 0)
            sc = 1.15
            def to2(p):
                u, v = proj(p)
                if vi == 2: return ox + cell/2 + u*sc, oy + cell/2 - v*sc
                return ox + cell/2 + u*sc, oy + cell - 12 - v*sc
            if vi < 2: cv.line(to2([-100, -100, 0]) if vi == 0 else to2([100, 0, 0]), to2([0, 100, 0]) if vi == 0 else to2([-100, 0, 0]), (170, 170, 170), 0)
            for b in DRAW:
                cv.line(to2(cs[PARENT[b]][1]), to2(cs[b][1]), color(b), 1)
            cv.dot(*to2(cs['head'][1]), (0, 0, 0), 4)
            # facing arrow from head
            hq = cs['head'][0]
            cv.line(to2(cs['head'][1]), to2([cs['head'][1][i] + d * 18 for i, d in enumerate([0, 1, 0])]), (0, 150, 0), 0)
    png(path, cv.w, cv.h, cv.px)

def validate(name, clip):
    worst_up, min_head, max_slip, root_bad = 1.0, 1e9, {'l': 0, 'r': 0}, 0
    first = to_cs({b: tuple(v) for b, v in clip['poses'][0].items()})
    for i, pose in enumerate(clip['poses']):
        if pose['root'][0] != [0.0, 0.0, 0.0, 1.0] or pose['root'][1] != [0.0, 0.0, 0.0]: root_bad += 1
        cs = to_cs({b: tuple(v) for b, v in pose.items()})
        up = sub(cs['neck_01'][1], cs['pelvis'][1]); worst_up = min(worst_up, up[2] / length(up))
        min_head = min(min_head, cs['head'][1][2])
        for s in 'lr':
            max_slip[s] = max(max_slip[s], length(sub(cs['ball_' + s][1], first['ball_' + s][1])))
    print(f"{name}: root keys non-identity={root_bad}  min torso-up cos={worst_up:.3f} ({math.degrees(math.acos(worst_up)):.1f} deg from vertical)"
          f"  min head z={min_head:.1f}  max toe travel L={max_slip['l']:.1f} R={max_slip['r']:.1f}")

if __name__ == '__main__':
    clips = json.load(open(os.path.join(HERE, 'clips.json')))
    for name, clip in clips.items():
        validate(name, clip)
        times = clip['keys'][:]
        render(clip, os.path.join(HERE, 'previews', name + '.png'), times)
