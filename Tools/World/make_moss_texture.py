"""Seamless carpet-like moss for the rock trial: a low-detail pile with a gentle raised feel, no clumps or fine tufts.
Writes Saved/StoneTest/Moss_Carpet_Value.png (linear, averages 0.5, so multiplying by value * 2 keeps the moss colour)
+ Moss_Carpet_Normal.png. Unreal's Python has no numpy, so run it with Blender's interpreter:
  "C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" -I Tools/World/make_moss_texture.py"""
import os, struct, zlib
import numpy as np

N = 1024
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'StoneTest')
rng = np.random.default_rng(3119)
fy, fx = np.meshgrid(np.fft.fftfreq(N) * N, np.fft.fftfreq(N) * N, indexing='ij')
FREQ = np.hypot(fx, fy)


def noise(lo, hi, beta=1.0):
    amp = np.where((FREQ >= lo) & (FREQ <= hi), 1.0 / np.maximum(FREQ, 1.0) ** beta, 0.0)
    v = np.fft.ifft2(np.fft.fft2(rng.standard_normal((N, N))) * amp).real
    return v / (np.abs(v).max() + 1e-9)


def write_png(path, rgb):
    img = np.clip(rgb * 255 + .5, 0, 255).astype(np.uint8)
    raw = b''.join(b'\0' + row.tobytes() for row in img)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', N, N, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


# for an 80 cm tile: soft tone patches about 15 cm, pile bumps in a wide size range (a narrow range forms maze-like ripples), fine fuzz about 1 cm
patches, pile, fuzz = noise(3, 8, 1.2), noise(8, 70, 1.1), noise(60, 160, .4)

height = .20 * patches + .60 * pile + .20 * fuzz
dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * .5
dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * .5
STRENGTH = 16.0
nrm = np.dstack((-dx * STRENGTH, -dy * STRENGTH, np.ones((N, N))))
nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)

value = .08 * patches + .10 * pile + .09 * fuzz
value = value - value.mean() + .5

os.makedirs(OUT, exist_ok=True)
write_png(os.path.join(OUT, 'Moss_Carpet_Value.png'), np.dstack((value, value, value)))
write_png(os.path.join(OUT, 'Moss_Carpet_Normal.png'), nrm * .5 + .5)

# preview only, not used in Unreal: the texture in the moss green, with simple low sun lighting at the material's relief (0.5)
def to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4
green = np.array([to_linear(v) for v in (80, 114, 24)])
lit = np.dstack((nrm[..., 0] * .5, nrm[..., 1] * .5, nrm[..., 2]))
lit /= np.linalg.norm(lit, axis=2, keepdims=True)
sun = np.array([-.55, -.45, .7]); sun /= np.linalg.norm(sun)
shade = .35 + .9 * np.clip(lit @ sun, 0, 1)
shot = np.clip(green * (value * 2.0)[..., None] * shade[..., None], 0, 1)
shot = np.where(shot <= .0031308, shot * 12.92, 1.055 * shot ** (1 / 2.4) - .055)
write_png(os.path.join(OUT, 'Moss_Carpet_Preview.png'), shot)

tilt = np.degrees(np.arccos(nrm[..., 2]))
print(f'value mean {np.clip(value, 0, 1).mean():.3f}  std {value.std():.3f}  range {value.min():.2f}..{value.max():.2f}')
print(f'normal tilt: average {tilt.mean():.1f} deg, 99th percentile {np.percentile(tilt, 99):.1f} deg')
