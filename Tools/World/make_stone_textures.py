"""Generates the seamless R03 rock textures (no Blender scene, just numpy): Saved/StoneR03/Stone_R03_BaseColor.png + _Normal.png.
No straight strata: blotchy colour, layered breakup and branching cracks, used by the rock material at two scales.
Import with: build_vista_assets.py stone_r03. Unreal's Python has no numpy, so run it with Blender's interpreter:
  "C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" Tools/World/make_stone_textures.py [out_dir]"""
import os, struct, sys, zlib
import numpy as np

N = 1024
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'StoneR03')
rng = np.random.default_rng(1803)
fy, fx = np.meshgrid(np.fft.fftfreq(N) * N, np.fft.fftfreq(N) * N, indexing='ij')
FREQ = np.hypot(fx, fy)


def noise(lo, hi, beta=1.8):
    """seamless band-limited noise (FFT filtered white noise), normalised to about -1..1; lo/hi in cycles per tile."""
    amp = np.where((FREQ >= lo) & (FREQ <= hi), 1.0 / np.maximum(FREQ, 1.0) ** beta, 0.0)
    v = np.fft.ifft2(np.fft.fft2(rng.standard_normal((N, N))) * amp).real
    return v / (np.abs(v).max() + 1e-9)


def voronoi(count, warp_x, warp_y):
    """wrap-around Voronoi on warped coordinates: F2 - F1 (distance to the nearest cell edge) and the cell index."""
    pts = rng.random((count, 2))
    yy, xx = np.meshgrid(np.arange(N) / N, np.arange(N) / N, indexing='ij')
    u, v = (xx + warp_x) % 1.0, (yy + warp_y) % 1.0
    f1, f2, cell = np.full((N, N), 9.0), np.full((N, N), 9.0), np.zeros((N, N), int)
    for i, (px, py) in enumerate(pts):
        dx = np.abs(u - px); dy = np.abs(v - py)
        d = np.hypot(np.minimum(dx, 1 - dx), np.minimum(dy, 1 - dy))
        closer = d < f1
        f2 = np.where(closer, f1, np.minimum(f2, d)); f1 = np.where(closer, d, f1); cell = np.where(closer, i, cell)
    return f2 - f1, cell


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def write_png(path, rgb):
    raw = b''.join(b'\0' + row.tobytes() for row in rgb)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', N, N, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


# ---- forms: broad lumps, medium chips, fine grain
broad, medium, fine, grain = noise(1, 6, 1.6), noise(5, 28, 1.3), noise(24, 120, 1.0), noise(100, 500, 0.5)
wx, wy = noise(2, 10) * .045 + noise(10, 40) * .012, noise(2, 10) * .045 + noise(10, 40) * .012

# ---- cracks: big and small networks. A mask keeps only some of each, so they branch and stop instead of closing into tiles
edge_big, cell_big = voronoi(16, wx, wy)
edge_small, cell_small = voronoi(70, wx * .7, wy * .7)
keep_big = smooth(-.12, .22, noise(1, 5, 1.2) / .6)
keep_small = smooth(.02, .4, noise(2, 9, 1.2) / .6)
width = .55 + .45 * (noise(8, 40, 1.0) / .7)            # cracks swell and pinch
crack_big = (1 - smooth(0, .0052 * np.maximum(width, .15), edge_big)) * keep_big
crack_small = (1 - smooth(0, .0026 * np.maximum(width, .15), edge_small)) * keep_small
bevel = (1 - smooth(0, .03, edge_big)) * keep_big       # worn shoulders either side of the big cracks

# ---- gently tilted plates per big cell, faded out at unkept edges, so faces read as chipped rock rather than flat plaster
plate = rng.random(16)[cell_big] - .5
plate = plate * keep_big + plate * (1 - keep_big) * .25

height = .50 * broad + .26 * medium + .10 * fine + .025 * grain + .10 * plate
height += -.05 * bevel - .28 * crack_big - .11 * crack_small
dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * .5
dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * .5
STRENGTH = 42.0
nrm = np.dstack((-dx * STRENGTH, -dy * STRENGTH, np.ones((N, N))))    # -gradient in UV space: the material's UVs are world axes, so it adds these to the world normal
nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
nrm_img = np.clip((nrm * .5 + .5) * 255 + .5, 0, 255).astype(np.uint8)

# ---- colour: cool grey and warm grey blotches, dark in the cavities, slightly lighter on worn high spots, mineral flecks
cavity = height - (np.fft.ifft2(np.fft.fft2(height) * np.exp(-(FREQ / 18.0) ** 2)).real)
value = .50 + .10 * broad + .075 * medium + .05 * fine + .018 * grain + .05 * plate + 1.0 * np.clip(cavity, -.14, .14) * .55
value -= .20 * crack_big + .10 * crack_small
hue = smooth(-.8, .8, noise(2, 12, 1.4) / .55)
cool, warm = np.array([.495, .50, .49]), np.array([.53, .51, .465])
col = (cool * (1 - hue[..., None]) + warm * hue[..., None]) * (value[..., None] / .5)
fleck = smooth(.42, .6, noise(150, 450, .2) / .7)[..., None]
col = col * (1 - fleck * .22)
pale = smooth(.5, .68, noise(120, 400, .2) / .7)[..., None]
col = col + pale * .05
bottom = np.array([.19, .19, .22])                       # cool crack bottoms
col = col * (1 - (crack_big[..., None] * .75)) + bottom * crack_big[..., None] * .75
col_img = np.clip(col * 255 + .5, 0, 255).astype(np.uint8)

os.makedirs(OUT, exist_ok=True)
write_png(os.path.join(OUT, 'Stone_R03_BaseColor.png'), col_img)
write_png(os.path.join(OUT, 'Stone_R03_Normal.png'), nrm_img)
lin = np.where(col_img / 255 <= .04045, col_img / 255 / 12.92, ((col_img / 255 + .055) / 1.055) ** 2.4)
row_std = col_img[..., 1].mean(axis=1).std(); col_std = col_img[..., 1].mean(axis=0).std()
print(f'avg sRGB {col_img.reshape(-1, 3).mean(0).round(1)}  avg linear {lin.reshape(-1, 3).mean(0).round(3)}')
print(f'row-mean std {row_std:.2f} vs column-mean std {col_std:.2f} (similar values = no horizontal bands)')
