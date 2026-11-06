"""Removes the horizontal bands from the current stone texture, leaving its cracks, colour and scale alone.
Bands are near-horizontal energy in the image's frequency space, so a wedge around that axis is filtered out, then the
original pixels are put back on the cracks. Writes Saved/StoneTest/Stone_NoBands_BaseColor.png + _Normal.png.
Unreal's Python has no numpy, so run it with Blender's interpreter:
  "C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" -I Tools/World/destripe_stone_texture.py"""
import os, struct, zlib
import numpy as np

SRC = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/World_R02/textures'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'StoneTest')


def read_png(path):
    data = open(path, 'rb').read()
    pos, idat, w, h, depth, ctype = 8, [], 0, 0, 0, 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT':
            idat.append(body)
        pos += n + 12
    if depth != 8 or ctype not in (2, 6):
        raise ValueError(f'{path}: unsupported PNG depth {depth} colour type {ctype}')
    ch = 3 if ctype == 2 else 4
    raw = np.frombuffer(zlib.decompress(b''.join(idat)), np.uint8).reshape(h, w * ch + 1)
    img = np.zeros((h, w * ch), np.int32)
    for y in range(h):
        f, line = raw[y, 0], raw[y, 1:].astype(np.int32)
        up = img[y - 1] if y else np.zeros(w * ch, np.int32)
        if f == 0:
            img[y] = line
        elif f == 2:
            img[y] = (line + up) & 255
        else:
            cur = img[y]
            for x in range(w * ch):
                a = cur[x - ch] if x >= ch else 0
                b = up[x]
                c = up[x - ch] if x >= ch else 0
                if f == 1:
                    pred = a
                elif f == 3:
                    pred = (a + b) // 2
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[x] = (line[x] + pred) & 255
    return img.reshape(h, w, ch)[..., :3].astype(np.float64) / 255.0


def write_png(path, rgb):
    img = np.clip(rgb * 255 + .5, 0, 255).astype(np.uint8)
    h, w = img.shape[:2]
    raw = b''.join(b'\0' + row.tobytes() for row in img)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def blur(a, sigma):
    h, w = a.shape
    fy, fx = np.meshgrid(np.fft.fftfreq(h), np.fft.fftfreq(w), indexing='ij')
    return np.fft.ifft2(np.fft.fft2(a) * np.exp(-2 * (np.pi * sigma) ** 2 * (fx ** 2 + fy ** 2))).real


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def band_filter(chan):
    """removes energy with little horizontal and real vertical frequency: rows that are lighter or darker all the way across."""
    h, w = chan.shape
    fy, fx = np.meshgrid(np.fft.fftfreq(h) * h, np.fft.fftfreq(w) * w, indexing='ij')
    limit = np.maximum(3.0, .22 * np.abs(fy))
    keep = 1.0 - np.exp(-(fx / limit) ** 2) * smooth(1.5, 4.0, np.abs(fy))
    return np.fft.ifft2(np.fft.fft2(chan) * keep).real


alb = read_png(f'{SRC}/Stone_R02_BaseColor.png')
nrm = read_png(f'{SRC}/Stone_R02_Normal.png')

lum = alb @ np.array([.3, .59, .11])
# cracks removed too (user, 2026-11-06: "they dont look good"): dark lines and flecks are filled from the stone around them
crack = np.clip(blur(smooth(.012, .045, blur(lum, 10) - lum), 2.0) * 2.2, 0, 1)
keep = 1 - crack
clean_alb = np.dstack([band_filter(alb[..., i]) for i in range(3)])
filled = np.dstack([blur(clean_alb[..., i] * keep, 8) / np.maximum(blur(keep, 8), .02) for i in range(3)])
out_alb = clean_alb * keep[..., None] + filled * crack[..., None]
out_alb = np.dstack([blur(out_alb[..., i], 14) for i in range(3)])    # soft tone only: plate seams would read as faint cracks, and the detail layer adds the surface

# the base relief map held nothing but cracks, veins and bands, so it goes flat; the detail layer carries the relief
out_nrm = np.dstack((np.full(lum.shape, .5), np.full(lum.shape, .5), np.ones(lum.shape)))

os.makedirs(OUT, exist_ok=True)
write_png(os.path.join(OUT, 'Stone_NoBands_BaseColor.png'), out_alb)
write_png(os.path.join(OUT, 'Stone_NoBands_Normal.png'), out_nrm)


def bands(img):
    g = blur(img[..., 1], 2)
    return g.mean(axis=1).std()


print(f'row-to-row banding, base colour: {bands(alb) * 255:.2f} -> {bands(out_alb) * 255:.2f} (of 255)')
print(f'row-to-row banding, normal: {bands(nrm) * 255:.2f} -> {bands(out_nrm) * 255:.2f}')
print(f'avg colour, sRGB: {(alb.reshape(-1, 3).mean(0) * 255).round(1)} -> {(out_alb.reshape(-1, 3).mean(0) * 255).round(1)}')
