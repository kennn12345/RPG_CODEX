"""Seamless fine detail for the rock trial: grain, small chips and pits, with no cracks and no large shapes that would show
a repeat. Writes Saved/StoneTest/Stone_Detail_Value.png (linear, averages 0.5, so multiplying by value * 2 keeps the
stone's colour) + Stone_Detail_Normal.png. Unreal's Python has no numpy, so run it with Blender's interpreter:
  "C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" -I Tools/World/make_stone_detail.py"""
import os, struct, zlib
import numpy as np

N = 1024
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'StoneTest')
rng = np.random.default_rng(2207)
fy, fx = np.meshgrid(np.fft.fftfreq(N) * N, np.fft.fftfreq(N) * N, indexing='ij')
FREQ = np.hypot(fx, fy)


def noise(lo, hi, beta=1.0):
    amp = np.where((FREQ >= lo) & (FREQ <= hi), 1.0 / np.maximum(FREQ, 1.0) ** beta, 0.0)
    v = np.fft.ifft2(np.fft.fft2(rng.standard_normal((N, N))) * amp).real
    return v / (np.abs(v).max() + 1e-9)


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def cell_edges(count, warp_x, warp_y):
    """wrap-around Voronoi: distance to the nearest cell edge, and the cell index."""
    pts = rng.random((count, 2))
    yy, xx = np.meshgrid(np.arange(N) / N, np.arange(N) / N, indexing='ij')
    u, v = (xx + warp_x) % 1.0, (yy + warp_y) % 1.0
    f1, f2, cell = np.full((N, N), 9.0), np.full((N, N), 9.0), np.zeros((N, N), int)
    for i, (px, py) in enumerate(pts):
        dx, dy = np.abs(u - px), np.abs(v - py)
        d = np.hypot(np.minimum(dx, 1 - dx), np.minimum(dy, 1 - dy))
        closer = d < f1
        f2 = np.where(closer, f1, np.minimum(f2, d)); f1 = np.where(closer, d, f1); cell = np.where(closer, i, cell)
    return f2 - f1, cell


def write_png(path, rgb):
    img = np.clip(rgb * 255 + .5, 0, 255).astype(np.uint8)
    raw = b''.join(b'\0' + row.tobytes() for row in img)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', N, N, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


lumps, chips, fine, grain = noise(3, 14, 1.2), noise(12, 60, 1.0), noise(50, 180, .8), noise(160, 500, .3)
wx, wy = noise(4, 20) * .018, noise(4, 20) * .018

# chipped faces: small tilted facets that only appear in patches
edge, cell = cell_edges(170, wx, wy)
facet = (rng.random(170)[cell] - .5) * smooth(-.05, .35, noise(4, 18, 1.1) / .6)

# pits: small dents scattered in clusters
pits = smooth(.5, .72, noise(90, 260, .4) / .7) * smooth(-.1, .35, noise(4, 16, 1.1) / .6)

height = .26 * lumps + .20 * chips + .10 * fine + .04 * grain + .10 * facet - .06 * pits   # hairline cracks left out at the user's request
dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * .5
dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * .5
STRENGTH = 26.0
nrm = np.dstack((-dx * STRENGTH, -dy * STRENGTH, np.ones((N, N))))
nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)

value = .10 * lumps + .13 * chips + .10 * fine + .05 * grain + .05 * facet - .16 * pits
value = value - value.mean() + .5

os.makedirs(OUT, exist_ok=True)
write_png(os.path.join(OUT, 'Stone_Detail_Value.png'), np.dstack((value, value, value)))
write_png(os.path.join(OUT, 'Stone_Detail_Normal.png'), nrm * .5 + .5)
print(f'value mean {np.clip(value, 0, 1).mean():.3f}  std {value.std():.3f}  range {value.min():.2f}..{value.max():.2f}')
print(f'normal tilt: average {np.degrees(np.arccos(nrm[..., 2])).mean():.1f} deg, 99th percentile {np.percentile(np.degrees(np.arccos(nrm[..., 2])), 99):.1f} deg')
