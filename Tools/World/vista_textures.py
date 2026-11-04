"""Blender (numpy): painted R02 textures for the world assets -> outputs/World_R02/textures. Tileable, stylized to the
Dreamscape: Tower reference (World_R01/reference): big soft painted value patches + a few crisp details, never noisy.
  blender -b --python vista_textures.py [-- name ...]
Each *_Height.png is a grey height map (relief: cracks / strata / bark grooves); Unreal builds the normal from it
(build_vista_assets.py) and the Blender previews use it as Bump - so both show the same surface relief."""
import bpy, numpy as np, math, os, sys
OUT = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R02/textures'
TREES2 = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Trees_R02/textures'
os.makedirs(OUT, exist_ok=True)
ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []

def save(name, img, folder=OUT):
    img = np.clip(img, 0, 1)
    if img.ndim == 2: img = np.dstack([img, img, img])
    if img.shape[2] == 3: img = np.dstack([img, np.ones(img.shape[:2])])
    h, w = img.shape[:2]
    im = bpy.data.images.new(name, w, h, alpha=True)
    im.pixels = np.flipud(img).astype(np.float32).ravel()
    im.filepath_raw = os.path.join(folder, name + '.png'); im.file_format = 'PNG'; im.save()
    print('TEX', name, w, h)

def fnoise(rng, h, w, sy, sx):
    f = np.fft.fft2(rng.standard_normal((h, w)))
    ky = np.fft.fftfreq(h)[:, None]; kx = np.fft.fftfreq(w)[None, :]
    n = np.real(np.fft.ifft2(f * np.exp(-2 * math.pi ** 2 * ((ky * sy) ** 2 + (kx * sx) ** 2))))
    return (n - n.min()) / (n.max() - n.min())

def blur(a, s):
    h, w = a.shape; ky = np.fft.fftfreq(h)[:, None]; kx = np.fft.fftfreq(w)[None, :]
    return np.real(np.fft.ifft2(np.fft.fft2(a) * np.exp(-2 * math.pi ** 2 * ((ky * s) ** 2 + (kx * s) ** 2))))

def voronoi(rng, h, w, count, sy=1.0):
    pts = rng.random((count, 2)) * (h, w)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    f1 = np.full((h, w), 1e9, np.float32); f2 = f1.copy(); cid = np.zeros((h, w), np.int32)
    for i, (py, px) in enumerate(pts):
        dy = np.abs(yy - py); dy = np.minimum(dy, h - dy) * sy
        dx = np.abs(xx - px); dx = np.minimum(dx, w - dx)
        d = np.sqrt(dx * dx + dy * dy); c = d < f1
        f2 = np.where(c, f1, np.minimum(f2, d)); cid = np.where(c, i, cid); f1 = np.where(c, d, f1)
    return f1, f2, cid

def ramp(t, stops):
    t = np.clip(t, 0, 1); out = np.zeros(t.shape + (3,), np.float32)
    for c in range(3): out[..., c] = np.interp(t, [s[0] for s in stops], [s[1][c] / 255 for s in stops])
    return out

def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)

def want(n): return not ARGS or n in ARGS

# ---------------------------------------------------------------- stone (rocks + cliffs + terrain slopes)
if want('stone'):
    rng = np.random.default_rng(5); S = 1024
    broad = fnoise(rng, S, S, 90, 90)                       # big painted patches
    mid = fnoise(rng, S, S, 28, 28)
    strata = fnoise(rng, S, S, 6, 120)                      # long horizontal strokes / layers
    grain = fnoise(rng, S, S, 2.2, 2.2)
    # cracks: voronoi borders of big plates, kept only where a mask says so (sparse long cracks, not a tiled grid)
    f1, f2, cid = voronoi(rng, S, S, 12, 2.2)                 # few big plates, stretched -> long sweeping cracks
    border = f2 - f1
    keep = smooth(.5, .6, fnoise(rng, S, S, 90, 90))
    wob = (fnoise(rng, S, S, 8, 8) - .5) * 5
    crack = np.clip(1 - (border + wob) / 3.2, 0, 1) ** 1.5 * keep
    hair = np.clip(1 - np.abs(fnoise(rng, S, S, 18, 18) - .5) * 70, 0, 1) * smooth(.55, .7, fnoise(rng, S, S, 50, 50)) * .6
    plate = rng.random(12)[cid]
    chips = smooth(.82, .86, fnoise(rng, S, S, 3, 3)) * smooth(.5, .7, mid)          # small chipped spots
    v = .52 + .28 * (broad - .5) + .14 * (mid - .5) + .10 * (plate - .5) + .12 * (strata - .5) + .07 * (grain - .5)
    v -= .48 * crack + .05 * hair + .14 * chips
    v += .07 * np.clip(blur(crack, 3) - crack, 0, 1) * 3                               # light lip beside each crack
    col = ramp(v, [(0, (58, 50, 52)), (.35, (124, 110, 96)), (.6, (176, 160, 134)), (1, (222, 210, 186))])
    # cool tint in the crack bottoms, warm on the plates (reference: warm beige lit stone, blue-grey shadows)
    col[..., 2] += .05 * crack; col[..., 0] += .025 * (broad - .5)
    save('Stone_R02_BaseColor', col)
    hgt = .55 + .18 * (broad - .5) + .12 * (mid - .5) + .06 * (strata - .5) + .03 * (grain - .5) - .45 * crack - .06 * hair - .15 * chips
    save('Stone_R02_Height', hgt)

# ---------------------------------------------------------------- bark relief (height for the R02 bark albedo)
if want('bark'):
    rng = np.random.default_rng(11); S = 1024
    strokes = fnoise(rng, S, S, 90, 4); groove_n = fnoise(rng, S, S, 140, 2.5); fine = fnoise(rng, S, S, 10, 2)
    # matches tree_textures.py R02 bark (same seed order is NOT required - relief only needs to read as long grooves)
    groove = smooth(.6, .78, groove_n)
    save('Bark_R02_Height', .55 + .25 * (strokes - .5) + .08 * (fine - .5) - .5 * groove)

# ---------------------------------------------------------------- grass blade (UV: u across 0..1, v root 0 .. tip 1)
if want('blade'):
    rng = np.random.default_rng(3); H, W = 512, 128
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32); u = (xx + .5) / W; vv = 1 - (yy + .5) / H
    streak = fnoise(rng, H, W, 40, 1.5)
    rib = np.exp(-((u - .5) / .06) ** 2)
    edge = smooth(.3, .5, np.abs(u - .5))
    v = .78 + .14 * rib - .14 * edge + .12 * (streak - .5) + .08 * vv
    save('GrassBlade_R02_BaseColor', np.dstack([v, v, v * .9]))

# ---------------------------------------------------------------- poppy / daisy petal (UV radial: u around, v base 0 .. rim 1)
if want('petal'):
    rng = np.random.default_rng(9); S = 256
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32); u = (xx + .5) / S; vv = 1 - (yy + .5) / S
    veins = .5 + .5 * np.cos((u * 22 + fnoise(rng, S, S, 12, 4) * 3) * math.pi)
    v = .72 + .18 * vv + .08 * (veins - .5) * (1 - vv * .5) + .06 * (fnoise(rng, S, S, 6, 6) - .5)
    base = smooth(.0, .25, vv)
    save('Petal_R02_BaseColor', np.dstack([v * (.6 + .4 * base), v * (.6 + .4 * base), v * (.6 + .4 * base)]))

# ---------------------------------------------------------------- ground: painted grass ground + dirt with pebbles
if want('ground'):
    rng = np.random.default_rng(21); S = 1024
    b = fnoise(rng, S, S, 60, 60); m = fnoise(rng, S, S, 12, 12)
    # grass ground: short painted blade strokes in many directions (reads as dense turf under the grass meshes)
    st = np.zeros((S, S), np.float32)
    for k in range(5):
        a = rng.uniform(0, math.pi); n_ = fnoise(rng, S, S, 1.5 + 3 * abs(math.sin(a)), 1.5 + 3 * abs(math.cos(a)))
        st += n_
    st = (st - st.min()) / (st.max() - st.min())
    v = .55 + .2 * (b - .5) + .1 * (m - .5) + .18 * (st - .5)
    save('GroundGrass_R02_BaseColor', ramp(v, [(0, (40, 72, 8)), (.45, (92, 132, 14)), (.75, (150, 166, 20)), (1, (196, 196, 40))]))
    # dirt: ochre soil, darker damp blotches, pebbles (small rounded light stones with a dark rim)
    f1, f2, cid = voronoi(rng, S, S, 380, 1.0)
    rnd_size = (rng.random(380) * 6 + 4)[cid]; sel = (rng.random(380) < .35)[cid]
    peb = sel * smooth(rnd_size, rnd_size - 2.5, f1)
    rim = sel * smooth(rnd_size + 2, rnd_size, f1) * (1 - peb)
    v = .55 + .18 * (b - .5) + .14 * (m - .5) + .06 * (fnoise(rng, S, S, 2, 2) - .5)
    dirt = ramp(v, [(0, (96, 64, 30)), (.5, (176, 132, 62)), (1, (226, 186, 106))])
    pebc = ramp(.5 + .3 * (rng.random(380)[cid] - .5), [(0, (120, 108, 92)), (1, (210, 196, 170))])
    dirt = dirt * (1 - .45 * rim[..., None]) * (1 - peb[..., None]) + pebc * peb[..., None]
    save('GroundDirt_R02_BaseColor', dirt)
    save('GroundDirt_R02_Height', .5 + .2 * (b - .5) + .5 * peb - .25 * rim)
print('VISTA_TEXTURES_DONE')

# ---------------------------------------------------------------- hero-tree leaves: dappled clusters of small round leaves
# (user screenshots User_01..07: every leaf readable at a clump edge -> scalloped "dappled" outline). 2x2 atlas, each cell
# a loose rounded cluster of ~80 small oval leaves; back leaves darker, a thin dark gap between overlapping leaves, near-grey
# values (hue from vertex colour), colour dilated behind transparency, 3% edge fade (no straight tile cuts).
if want('dapple'):
    C = 512
    def dapple(seed):
        r = np.random.default_rng(seed)
        yy, xx = np.mgrid[0:C, 0:C].astype(np.float32); X = (xx + .5) / C; Y = 1 - (yy + .5) / C
        val = np.zeros((C, C), np.float32); alpha = np.zeros((C, C), np.float32)
        n = r.integers(125, 150); leaves = []
        for i in range(n):
            a = r.uniform(0, 2 * math.pi); rr = .40 * math.sqrt(r.random())         # leaf centres fill a rounded blob
            cx, cy = .5 + math.cos(a) * rr, .5 + math.sin(a) * rr * .85
            ang = a + r.uniform(-.7, .7)                                           # leaves point roughly outward
            L = r.uniform(.07, .1); Wd = L * r.uniform(.58, .72)
            leaves.append((cy, cx, ang, L, Wd, r.uniform(-.06, .06)))
        leaves.sort(key=lambda q: -q[0])                                           # paint lower (back) leaves first
        for k, (cy, cx, ang, L, Wd, tone) in enumerate(leaves):
            ca, sa = math.cos(ang), math.sin(ang)
            u = ((X - cx) * ca + (Y - cy) * sa) / L + .5                           # 0 base .. 1 tip
            v = (-(X - cx) * sa + (Y - cy) * ca) / L
            uu = np.clip(u, 0, 1)
            prof = (Wd / L) * .5 * np.power(np.maximum(np.sin(uu * math.pi), 0), .8) * (1 - .25 * uu)   # rounded oval (sin(pi) < 0 in float32 -> NaN without the max)
            inside = (prof - np.abs(v)) * L * C
            a_ = np.clip(inside + .5, 0, 1) * (u > 0) * (u < 1)
            edge = 1 - smooth(0, 3.5, inside)                                      # thin dark rim = gap to the leaf behind
            depth = k / max(1, len(leaves) - 1)
            lv = .58 + .22 * depth + .12 * uu + .06 * (v > 0) - .32 * edge + tone
            val = val * (1 - a_) + lv * a_; alpha = np.maximum(alpha, a_)
        mean = float((val * alpha).sum() / max(alpha.sum(), 1.0))
        val = np.where(alpha > .02, val, mean)
        alpha = alpha * np.clip(np.minimum(np.minimum(X, 1 - X), np.minimum(Y, 1 - Y)) / .03, 0, 1)
        return np.dstack([val * .97, val, val * .9, alpha])
    atlas = np.zeros((2 * C, 2 * C, 4), np.float32)
    for i in range(4):
        r_, c_ = divmod(i, 2); atlas[r_ * C:(r_ + 1) * C, c_ * C:(c_ + 1) * C] = dapple(101 + i * 13)
    save('Leaf_Dapple_Atlas_BaseColor', atlas)

# ---------------------------------------------------------------- normal maps from the height maps (tangent space, Unreal/DirectX: green = down)
def normal_from(name, strength):
    im = bpy.data.images.load(os.path.join(OUT, name + '_Height.png')); w, h = im.size
    hgt = np.flipud(np.array(im.pixels[:], np.float32).reshape(h, w, 4)[..., 0])      # row 0 = top
    dx = (np.roll(hgt, -1, 1) - np.roll(hgt, 1, 1)) * .5 * strength
    dy = (np.roll(hgt, -1, 0) - np.roll(hgt, 1, 0)) * .5 * strength
    n = np.dstack([-dx, dy, np.ones_like(hgt)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
    save(name + '_Normal', n * .5 + .5)
for nm, st in (('Stone_R02', 14.0), ('Bark_R02', 10.0), ('GroundDirt_R02', 8.0)):
    if os.path.exists(os.path.join(OUT, nm + '_Height.png')): normal_from(nm, st)
print('VISTA_NORMALS_DONE')
