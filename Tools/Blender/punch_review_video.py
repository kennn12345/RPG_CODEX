"""Six-angle review of one punch: video (normal speed twice, pause, quarter speed) and a key-frame sheet.
python punch_review_video.py <frames_dir> <R|L> <out.mp4> <sheet.png>
<frames_dir>/<R|L> holds the frames from render_punch_frames.py ... all. Needs PyAV and Pillow (the path below is the
local PyAV install used so far)."""
import sys
sys.path.insert(0, r'C:/Users/cedar/Documents/Codex/2026-10-23/referenced-chatgpt-conversation-this-is-an/work/video-deps')
import av
from PIL import Image, ImageDraw, ImageFont
src, side, out, sheet = sys.argv[1:5]
VIEWS = [('front', 'Front'), ('front34', 'Front 3/4'), ('side', 'Punching side'),
         ('back34', 'Back 3/4'), ('back', 'Back'), ('farside', 'Far side')]
FRAMES = 48
W, H = 360, 405
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 20)
small = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 16)
name = 'Right punch' if side == 'R' else 'Left punch'


def view(v, frame, size):
    im = Image.open(f'{src}/{side}/{v}_{frame:03d}.png').convert('RGBA')
    bg = Image.new('RGBA', im.size, 'white')
    return Image.alpha_composite(bg, im).convert('RGB').resize(size)


def phase(t):
    return ('wind-up' if t <= .16 else 'strike' if t < .285 else 'contact' if t < .3 else
            'follow-through' if t < .40 else 'retract' if t < .58 else 'settle')


def tile(frame, label):
    canvas = Image.new('RGB', (W*3, H*2+40), 'white'); d = ImageDraw.Draw(canvas)
    for i, (v, title) in enumerate(VIEWS):
        x, y = (i % 3)*W, 40+(i//3)*H
        canvas.paste(view(v, frame, (W, H)), (x, y)); d.text((x+6, y+4), title, fill='black', font=small)
    t = (frame-1)/60
    d.text((8, 8), f'{name}   {label}   t = {t:.3f} s   ({phase(t)})', fill='black', font=font)
    return canvas


container = av.open(out, 'w', options={'movflags': '+faststart'})
stream = container.add_stream('libx264', rate=60); stream.width = W*3; stream.height = H*2+40
stream.pix_fmt = 'yuv420p'; stream.options = {'crf': '18', 'preset': 'medium'}
sequence = [(f, 'normal speed', 1) for _ in range(2) for f in range(1, FRAMES+1)]
sequence += [(FRAMES, 'pause', 30)]
sequence += [(f, 'quarter speed', 4) for f in range(1, FRAMES+1)]
count = 0
for frame, label, repeat in sequence:
    picture = av.VideoFrame.from_image(tile(frame, label))
    for _ in range(repeat):
        for packet in stream.encode(picture): container.mux(packet)
        count += 1
for packet in stream.encode(): container.mux(packet)
container.close()
with av.open(out) as check: decoded = sum(1 for _ in check.decode(video=0))
assert decoded == count, (decoded, count)

# Key-frame sheet: one row per view, columns at the phases of the punch.
keys = [1, 10, 14, 16, 18, 20, 22, 25, 30, 40]
TW, TH = 168, 189
img = Image.new('RGB', (110+TW*len(keys), 30+TH*len(VIEWS)), 'white'); d = ImageDraw.Draw(img)
for c, frame in enumerate(keys):
    t = (frame-1)/60; d.text((110+c*TW+4, 6), f'{t:.3f}s {phase(t)}', fill='black', font=small)
for r, (v, title) in enumerate(VIEWS):
    d.text((6, 30+r*TH+TH//2-8), title, fill='black', font=small)
    for c, frame in enumerate(keys):
        img.paste(view(v, frame, (TW, TH)), (110+c*TW, 30+r*TH))
img.save(sheet)
print(name, 'video frames', count, 'seconds', round(count/60, 2))
