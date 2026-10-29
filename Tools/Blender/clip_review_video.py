"""Six-angle review of any clip (punch_review_video.py, for clips of any length): video (normal speed twice, pause,
quarter speed) and a key-frame sheet.
python clip_review_video.py <frames_dir> <out.mp4> <sheet.png> <title> <phases>
<frames_dir> holds the frames from render_punch_frames.py ... all (<view>_<frame>.png, 60 fps).
<phases>: comma list of start_seconds=label, e.g. "0=hit,0.3=stumble,0.66=knee down,1.05=absorb,1.8=rise". The sheet
takes a column at every phase start plus one between each. Needs PyAV and Pillow (the local install used so far)."""
import glob, os, re, sys
sys.path.insert(0, r'C:/Users/cedar/Documents/Codex/2026-10-23/referenced-chatgpt-conversation-this-is-an/work/video-deps')
import av
from PIL import Image, ImageDraw, ImageFont
src, out, sheet, title, phases = sys.argv[1:6]
PHASES = sorted((float(t), label) for t, label in (p.split('=') for p in phases.split(',')))
VIEWS = [('front', 'Front'), ('front34', 'Front 3/4'), ('side', 'Side'), ('back34', 'Back 3/4'), ('back', 'Back'),
         ('farside', 'Far side')]
FRAMES = max(int(re.search(r'_(\d+)\.png$', f).group(1)) for f in glob.glob(os.path.join(src, 'front_*.png')))
W, H = 360, 405
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 20)
small = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 16)

def view(v, frame, size):
    im = Image.open(f'{src}/{v}_{frame:03d}.png').convert('RGBA')
    bg = Image.new('RGBA', im.size, 'white')
    return Image.alpha_composite(bg, im).convert('RGB').resize(size)

def phase(t):
    return [label for start, label in PHASES if t >= start - 1e-6][-1]

def tile(frame, label):
    canvas = Image.new('RGB', (W*3, H*2+40), 'white'); d = ImageDraw.Draw(canvas)
    for i, (v, name) in enumerate(VIEWS):
        x, y = (i % 3)*W, 40+(i//3)*H
        canvas.paste(view(v, frame, (W, H)), (x, y)); d.text((x+6, y+4), name, fill='black', font=small)
    t = (frame-1)/60
    d.text((8, 8), f'{title}   {label}   t = {t:.3f} s   ({phase(t)})', fill='black', font=font)
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

# Key-frame sheet: one row per view, a column at each phase start and half-way to the next.
starts = [t for t, _ in PHASES] + [(FRAMES - 1) / 60]
times = sorted({round(x, 3) for a, b in zip(starts, starts[1:]) for x in (a, (a + b) / 2)} | {starts[-1]})
keys = [min(FRAMES, max(1, int(round(t * 60)) + 1)) for t in times]
TW, TH = 150, 169
img = Image.new('RGB', (100+TW*len(keys), 30+TH*len(VIEWS)), 'white'); d = ImageDraw.Draw(img)
for c, frame in enumerate(keys):
    t = (frame-1)/60; d.text((100+c*TW+4, 6), f'{t:.2f}s {phase(t)}', fill='black', font=small)
for r, (v, name) in enumerate(VIEWS):
    d.text((6, 30+r*TH+TH//2-8), name, fill='black', font=small)
    for c, frame in enumerate(keys):
        img.paste(view(v, frame, (TW, TH)), (100+c*TW, 30+r*TH))
img.save(sheet)
print(title, 'video frames', count, 'seconds', round(count/60, 2))
