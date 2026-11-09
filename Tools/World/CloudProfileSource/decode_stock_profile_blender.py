"""Decode-only helper: turn the exported stock T_Profile_08.exr into raw float data for the Unreal builder.

blender --background --factory-startup --python decode_stock_profile_blender.py -- <in.exr> <out.f32>
Output: 256x256 RGBA float32 little-endian, rows top-to-bottom (Unreal UV v=0 first).
"""
import bpy, sys, struct
src, dst = sys.argv[sys.argv.index('--') + 1:][:2]
img = bpy.data.images.load(src)
w, h = img.size
px = list(img.pixels)  # Blender rows are bottom-to-top
with open(dst, 'wb') as f:
    f.write(struct.pack('<II', w, h))
    for y in range(h - 1, -1, -1):
        row = px[y * w * 4:(y + 1) * w * 4]
        f.write(struct.pack('<%df' % len(row), *row))
print('DECODED', w, h, dst)
