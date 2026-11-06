"""Detail texture for the hero leaf spray (user 2026-11-05: brown stems, dark curved veins, per-leaf tones).
  blender -b --factory-startup -P Tools/World/make_leaf_detail.py
Reads the leaf mask, writes Hero_LeafSpray_2x2_Detail.png beside it, same size and layout:
  R = tone of each leaf (0.5 = the palette as it is), G = stems, B = veins.
Only brightness is stored: the material scales the palette with it, so a new leaf colour keeps the same tones + veins.
Stems = thin, long parts of the mask. Leaves = distance-field peaks + watershed, split-off tips merged back. Seeds are fixed."""
import bpy, os, heapq, numpy as np
TEXTURES = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R02/textures'
MASK, OUT = f'{TEXTURES}/Hero_LeafSpray_2x2_Mask.png', f'{TEXTURES}/Hero_LeafSpray_2x2_Detail.png'
STEM_RADIUS = 5     # parts of the mask thinner than about 2x this are stem candidates
SEED_DEPTH = 14     # a leaf centre sits at least this far inside the mask (lower: leaf tips become leaves)
TIP_AREA = 900      # smaller pieces are split-off tips
TONE_SD = .24       # spread of the per-leaf tones

im = bpy.data.images.load(MASK); W, H = im.size
px = np.empty(W * H * 4, np.float32); im.pixels.foreach_get(px); g = px.reshape(H, W, 4)[:, :, 0]
m = g > .5
def grow4(a):
    b = a.copy(); b[1:, :] |= a[:-1, :]; b[:-1, :] |= a[1:, :]; b[:, 1:] |= a[:, :-1]; b[:, :-1] |= a[:, 1:]; return b
def grow8(a):
    b = grow4(a); b[1:, 1:] |= a[:-1, :-1]; b[1:, :-1] |= a[:-1, 1:]; b[:-1, 1:] |= a[1:, :-1]; b[:-1, :-1] |= a[1:, 1:]; return b

# ---- stems: mask minus an opening, keeping long thin pieces (not leaf-edge teeth or tips)
o = m.copy()
for _ in range(STEM_RADIUS): o = ~grow4(~o)
for _ in range(STEM_RADIUS + 1): o = grow4(o)
res = m & ~o
seen = np.zeros_like(res); keep = np.zeros_like(res)
for y0, x0 in zip(*np.nonzero(res)):
    if seen[y0, x0]: continue
    st = [(y0, x0)]; seen[y0, x0] = True; pts = []
    while st:
        y, x = st.pop(); pts.append((y, x))
        for j in (-1, 0, 1):
            for i in (-1, 0, 1):
                v, u = y + j, x + i
                if 0 <= v < H and 0 <= u < W and res[v, u] and not seen[v, u]:
                    seen[v, u] = True; st.append((v, u))
    p = np.array(pts); ext = (p.max(0) - p.min(0)).max() + 1
    if ext >= 11 and len(pts) / ext <= 5.0: keep[p[:, 0], p[:, 1]] = True
stems = grow4(keep) & m     # 1 px wider: no green fringe along a stem

# ---- leaves: distance peaks, then a flood from each, deepest pixels first
leaf = m & ~stems
dist = np.zeros((H, W), np.float32); e = leaf.copy()
for i in range(60):
    dist += e; e = ~(grow4(~e) if i % 2 else grow8(~e))
    if not e.any(): break
ys, xs = np.nonzero(dist >= SEED_DEPTH); seeds = []
for k in np.argsort(-dist[ys, xs]):
    y, x, d = ys[k], xs[k], dist[ys[k], xs[k]]
    if all((y - a) ** 2 + (x - b) ** 2 > (1.7 * max(d, c)) ** 2 for a, b, c in seeds): seeds.append((y, x, d))
lab = np.zeros((H, W), np.int32); hp = []
for i, (y, x, d) in enumerate(seeds, 1):
    lab[y, x] = i; heapq.heappush(hp, (-float(d), int(y), int(x)))
while hp:
    _, y, x = heapq.heappop(hp)
    for j, i in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        v, u = y + j, x + i
        if 0 <= v < H and 0 <= u < W and leaf[v, u] and not lab[v, u]:
            lab[v, u] = lab[y, x]; heapq.heappush(hp, (-float(dist[v, u]), v, u))
area = np.bincount(lab.ravel(), minlength=len(seeds) + 1)
for i in np.argsort(area):
    if i == 0 or not 0 < area[i] < TIP_AREA: continue
    a = lab == i; nb = lab[grow4(a) & ~a]; nb = nb[nb > 0]
    if len(nb):
        j = np.bincount(nb).argmax(); lab[a] = j; area[j] += area[i]; area[i] = 0

# ---- tones: one value per leaf
tones = np.clip(np.random.RandomState(11).normal(.5, TONE_SD, len(seeds) + 1), 0, 1); tones[0] = .5
tone = tones[lab].astype(np.float32)

# ---- veins: a bowed midrib, and side veins that leave it shallow and curve up towards the tip
sy, sx = np.nonzero(stems)
vein = np.zeros((H, W), np.float32); made = 0
for i in range(1, len(seeds) + 1):
    ys, xs = np.nonzero(lab == i)
    if len(ys) < 400: continue
    P = np.stack([xs, ys], 1).astype(np.float32); c = P.mean(0); Q = P - c
    val, vec = np.linalg.eigh(np.cov(Q.T)); ax = vec[:, 1]; sd = vec[:, 0]
    t = Q @ ax; lo, hi = t.min(), t.max()
    A, B = c + ax * lo, c + ax * hi       # the base is the end nearer a stem
    if ((sx - B[0]) ** 2 + (sy - B[1]) ** 2).min() < ((sx - A[0]) ** 2 + (sy - A[1]) ** 2).min():
        ax = -ax; t = -t; lo, hi = t.min(), t.max()
    p = Q @ sd; L = hi - lo; u = (t - lo) / L
    wd = max(abs(p.min()), p.max()); edge = np.clip((dist[ys, xs] - 1.5) / 2.5, 0, 1)
    bow = .07 * L * (1 if (int(seeds[i - 1][0]) * 7 + int(seeds[i - 1][1])) % 2 else -1)
    mid = np.abs(p - bow * (1 - (2 * u - 1) ** 2))
    v = np.clip((1.7 - u) - mid + .5, 0, 1) * (u < .94)
    r = mid / wd
    rise = wd * (.5 * r + .95 * r * r); run = np.sqrt(1 + (.5 + 1.9 * r) ** 2)
    for k in range(5):
        d = np.abs(t - (lo + (.12 + .17 * k) * L) - rise) / run
        v = np.maximum(v, np.clip((1 - .35 * r) - d + .5, 0, 1) * .75 * (r < .9))
    vein[ys, xs] = np.maximum(vein[ys, xs], v * edge); made += 1

rgba = np.dstack([tone, stems.astype(np.float32), vein, np.ones((H, W), np.float32)])
o = bpy.data.images.new('detail', W, H, alpha=True); o.colorspace_settings.name = 'Non-Color'
o.pixels.foreach_set(rgba.ravel()); o.update()
o.filepath_raw = OUT; o.file_format = 'PNG'; o.save()
print(f'LEAF_DETAIL leaves={made} stem_pixels={int(stems.sum())} vein_pixels={int((vein > .3).sum())} -> {OUT}')
