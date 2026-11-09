"""Losslessly decode the source 16-bit grayscale PNG to Unreal's little-endian R16.
Uses only Python's standard library; no Blender or external packages.
"""
from pathlib import Path
import struct
import zlib
import hashlib

root = Path(__file__).resolve().parent / 'WorldMapSource'
png = (root / 'heightmap_4033x2521_16bit.png').read_bytes()
assert png[:8] == b'\x89PNG\r\n\x1a\n'
pos, compressed = 8, bytearray()
while pos < len(png):
    length = struct.unpack_from('>I', png, pos)[0]
    kind, data = png[pos+4:pos+8], png[pos+8:pos+8+length]
    if kind == b'IHDR':
        width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', data)
        assert (width, height, depth, color, compression, filtering, interlace) == (4033, 2521, 16, 0, 0, 0, 0)
    if kind == b'IDAT':
        compressed.extend(data)
    pos += length + 12
raw = zlib.decompress(compressed)
stride = width * 2
previous = bytearray(stride)
result = bytearray()
for y in range(height):
    offset = y * (stride + 1)
    mode = raw[offset]
    row = bytearray(raw[offset+1:offset+1+stride])
    for x in range(stride):
        a = row[x-2] if x >= 2 else 0
        b = previous[x]
        c = previous[x-2] if x >= 2 else 0
        if mode == 1:
            prediction = a
        elif mode == 2:
            prediction = b
        elif mode == 3:
            prediction = (a+b)//2
        elif mode == 4:
            p = a+b-c
            pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
            prediction = a if pa <= pb and pa <= pc else b if pb <= pc else c
        else:
            assert mode == 0
            prediction = 0
        row[x] = (row[x] + prediction) & 255
    little = bytearray(stride)
    little[0::2], little[1::2] = row[1::2], row[0::2]
    result.extend(little)
    previous = row
assert len(result) == width*height*2
(root / 'heightmap.r16').write_bytes(result)
print('R16_READY', width, height, len(result), hashlib.sha256(result).hexdigest())
