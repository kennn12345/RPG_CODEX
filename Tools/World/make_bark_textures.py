"""Bark variations for the Fab trees, one model at a time (user 2026-11-05: 3 barks per tree model, see Docs/ASSET_RESTYLE_PROGRESS.md).
  blender -b --factory-startup -P Tools/World/make_bark_textures.py -- Lux_A
Look chosen by the user for Lux A: deep brown, vertical ridges, hand-painted; the 3 variations share colour and character with
only a very slight tone shift, but each has its own ridge pattern and a knot or two.
Writes Bark_<tree>_<1-3>_{BaseColor,Normal,Height}.png to World_R02/textures/Bark. All tile in both directions. The pack trunks'
UVs repeat about every 2.5 m with U along the trunk, so the grain runs along U (image x)."""
import bpy, os, sys, math, numpy as np
OUT = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R02/textures/Bark'
N = 1024
RECIPES = {
    'Lux_A': {'palette': ((46, 32, 24), (102, 74, 54), (154, 120, 88)), 'across': 1.33,   # across: merge_fab_bark.py prints it; darker paint reads as black under a thick canopy
              # gain = brightness, warm = red up / blue down; knots = (u, v, size) in 0..1 of the tile
              'barks': ({'seed': 101, 'gain': 1.00, 'warm': 1.00, 'knots': ((.32, .62, 1.0),)},
                        {'seed': 211, 'gain': .96, 'warm': 1.02, 'knots': ((.70, .24, .85), (.18, .78, .7))},
                        {'seed': 307, 'gain': 1.04, 'warm': .98, 'knots': ((.55, .50, 1.15),)})},
}
Y, X = np.mgrid[0:N, 0:N].astype(np.float32)
def noise(along, across, seed):
    """tiling noise; along = feature length in px along the grain (image x), across = across it"""
    r = np.random.RandomState(seed).randn(N, N)
    fy, fx = np.fft.fftfreq(N)[:, None], np.fft.fftfreq(N)[None, :]
    n = np.fft.ifft2(np.fft.fft2(r) * np.exp(-2 * math.pi ** 2 * ((fx * along) ** 2 + (fy * across) ** 2))).real
    lo, hi = np.percentile(n, (1, 99)); return np.clip((n - lo) / (hi - lo), 0, 1).astype(np.float32)
def wrapped(a, c): d = a - c; return d - N * np.round(d / N)
def bilinear(a, x, y):
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int); fx, fy = x - x0, y - y0
    x1, y1 = (x0 + 1) % N, (y0 + 1) % N; x0, y0 = x0 % N, y0 % N
    return (a[y0, x0] * (1 - fx) + a[y0, x1] * fx) * (1 - fy) + (a[y1, x0] * (1 - fx) + a[y1, x1] * fx) * fy
def bark(recipe, b):
    s = b['seed']
    # grain coordinates: slow wander, then pushed apart around each knot so the ridges flow around it
    A = recipe['across']                                                  # image y covers the trunk's girth, not 2.5 m: see merge_fab_bark.py
    sy = Y + (noise(320, 70 * A, s + 3) - .5) * 34 * A
    knots = []
    for (ku, kv, size) in b['knots']:
        dx, dy = wrapped(X, ku * N), wrapped(Y, kv * N); R = 30.0 * size
        e = np.sqrt((dx / (2.1 * R)) ** 2 + (dy / (R * A)) ** 2)
        sy = sy - dy * .85 * np.exp(-(e / 1.7) ** 2); knots.append((e, dx, dy, R))
    a = bilinear(noise(240, 8.5 * A, s), X, sy); b2 = bilinear(noise(90, 3.2 * A, s + 1), X, sy)
    h = np.clip(.8 * np.abs(2 * a - 1) ** .8 + .2 * b2, 0, 1)            # ridges about 5 cm wide with grooves between them
    h = np.clip(h + .12 * (bilinear(noise(60, 6 * A, s + 2), X, sy) - .5), 0, 1)
    tone = np.ones((N, N), np.float32)
    for e, dx, dy, R in knots:                                            # the knot: a dark hollow with a raised, uneven lip
        wob = (noise(40, 40, s + 9) - .5) * .5
        w = 1 - np.clip((e + wob - .55) / .3, 0, 1); w = w * w * (3 - 2 * w)
        h = h * (1 - w) + (.12 + .3 * np.clip(e * 1.6, 0, 1)) * w
        rim = np.exp(-((e + wob - .85) / .22) ** 2); h = np.clip(h + .34 * rim, 0, 1)
        tone = tone * (1 - .3 * np.exp(-(e / .4) ** 2))
    # hand-painted: broad strokes, then soft bands of colour instead of a smooth ramp
    t = np.clip(h + .12 * (bilinear(noise(110, 9 * A, s + 5), X, sy) - .5), 0, 1)
    t = .35 * t + .65 * (np.round(t * 5) / 5)
    d, m, l = [np.array(c, np.float32)[None, None] for c in recipe['palette']]
    t3 = t[..., None]; rgb = np.where(t3 < .5, d + (m - d) * t3 * 2, m + (l - m) * (t3 - .5) * 2)
    rgb = rgb * (.97 + .06 * noise(360, 160, s + 8))[..., None] * tone[..., None] * b['gain']   # low: broad patches show UV seams
    rgb = rgb * np.array((b['warm'], 1, 2 - b['warm']), np.float32)[None, None]
    return np.clip(rgb / 255, 0, 1), h
def save(name, rgb):
    a = rgb if rgb.ndim == 3 else np.dstack([rgb, rgb, rgb])
    o = bpy.data.images.new(name, N, N, alpha=True); o.colorspace_settings.name = 'Non-Color'
    o.pixels.foreach_set(np.dstack([a, np.ones((N, N))]).astype(np.float32).ravel()); o.update()
    o.filepath_raw = f'{OUT}/{name}.png'; o.file_format = 'PNG'; o.save()
def normal(h, strength=5.0):
    """Unreal convention (green = -dv in this image's bottom-up rows)"""
    du = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength; dv = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    n = np.dstack([-du, dv, np.ones_like(h)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n * .5 + .5
os.makedirs(OUT, exist_ok=True)
args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else list(RECIPES)
for tree in args:
    for i, b in enumerate(RECIPES[tree]['barks'], 1):
        rgb, h = bark(RECIPES[tree], b)
        save(f'Bark_{tree}_{i}_BaseColor', rgb); save(f'Bark_{tree}_{i}_Normal', normal(h)); save(f'Bark_{tree}_{i}_Height', h)
        print(f'BARK {tree} {i} mean sRGB {[int(v * 255) for v in rgb.reshape(-1, 3).mean(0)]}')
print('BARK_DONE')
