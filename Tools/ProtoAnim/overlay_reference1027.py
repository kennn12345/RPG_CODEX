"""Draw the authored skeleton over the filmed reference frames through the fitted camera (pelvis translation aligned
only), with the hand-marked joint centres, and report per-joint image distances. For judging posture by eye: the
landmarks alone cannot see depth (forward lean, shoulder roll), the overlay on the filmed body can.

Run (Codex python with numpy/PIL), RPG_SKEL=uefn:
  python overlay_reference1027.py [clip-file] [out.png]
  default clip file reference1027_uefn.json; uses RPG_Reference1027_PairReview (the fitted poses on the filmed
  timing: frame f at (f-72)/60 s) and RPG_CombatStance_Reference1027 for the guard frame 128.
Env RPG_REFERENCE_FRAMES: folder with ref_0072.png ... (default: Codex's extraction).
"""
import json, math, os, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import protoanim as p
import getupcheck as g

HERE = Path(__file__).resolve().parent
clips = json.loads(Path(sys.argv[1] if len(sys.argv) > 1 else HERE / 'reference1027_uefn.json').read_text())
out = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE.parents[1] / 'Saved/ReferenceCombat1027/overlay.png'
frames_dir = Path(os.environ.get('RPG_REFERENCE_FRAMES', 'C:/Users/cedar/Documents/Codex/2026-10-26/ok-x20/work/PunchReference1027'))
marks = json.loads((HERE / 'reference1027_landmarks.json').read_text())['frames']
yaw, pitch, distance, focal, cx, cy = json.loads((HERE / 'captures/reference1027_fitted_poses.json').read_text())['camera']
yaw, pitch = math.radians(yaw), math.radians(pitch)
forward = np.array([math.sin(yaw) * math.cos(pitch), math.cos(yaw) * math.cos(pitch), -math.sin(pitch)])
right = np.array([-forward[1], forward[0], 0.]); right /= np.linalg.norm(right)
up = np.cross(forward, right)

def project(point):
    v = np.asarray(point) - [0., 0., 85.]
    depth = distance + v @ forward
    return np.array([cx + focal * (v @ right) / depth, cy - focal * (v @ up) / depth])

SKIP = ('twist', 'index', 'middle', 'ring', 'pinky', 'thumb', 'ik_', 'interaction', 'center_of_mass', 'corrective',
        'metacarpal', 'weapon', 'attach', 'root')
DRAW = [b for b in p.BONES if p.PARENT[b] and not any(k in b for k in SKIP) and not any(k in p.PARENT[b] for k in SKIP)]

def pose_for(frame):
    if frame == 128:
        c = clips['RPG_CombatStance_Reference1027']; return c['poses'][0]
    c = clips['RPG_Reference1027_PairReview']
    return c['poses'][min(len(c['poses']) - 1, round((frame - 72)))]   # 60 fps clip, filmed 60 Hz

def measure(frame, cs):
    observed = marks[str(frame)]
    projected = {b: project(g.head_top(cs) if b == 'head_centre' else cs[b][1]) for b in observed}
    offset = np.array(observed['pelvis']) - projected['pelvis']
    return offset, {b: float(np.linalg.norm(projected[b] + offset - observed[b])) for b in observed if b != 'pelvis'}

def main():
    frames = sorted(map(int, marks))
    W, H = 340, 517
    sheet = Image.new('RGB', (W * 6, H * 2 + 40), '#15181d'); draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 16)
    report = {}
    for k, frame in enumerate(frames):
        cs = p.to_cs(pose_for(frame))
        offset, errors = measure(frame, cs)
        report[frame] = errors
        im = Image.open(frames_dir / f'ref_{frame:04d}.png').convert('RGB')
        im = Image.blend(im, Image.new('RGB', im.size, 'black'), .35)
        d = ImageDraw.Draw(im)
        for b in DRAW:
            a, c = project(cs[p.PARENT[b]][1]) + offset, project(cs[b][1]) + offset
            col = '#ff9a2a' if b.endswith('_l') else '#ffe14a' if b.endswith('_r') else '#ffffff'
            d.line([tuple(a), tuple(c)], fill=col, width=6)
        hc = project(g.head_top(cs)) + offset; d.ellipse((hc[0] - 34, hc[1] - 34, hc[0] + 34, hc[1] + 34), outline='white', width=4)
        for b, v in marks[str(frame)].items():
            d.ellipse((v[0] - 9, v[1] - 9, v[0] + 9, v[1] + 9), outline='#00ff88', width=4)
        x, y = k % 6 * W, 40 + k // 6 * H
        sheet.paste(im.resize((W, H)), (x, y))
        draw.text((x + 6, y + 4), f'f{frame}  mean {np.mean(list(errors.values())):.0f}px', font=font, fill='yellow')
    total = np.mean([v for e in report.values() for v in e.values()])
    draw.text((8, 10), f'Authored skeleton (left orange, right yellow) over the filmed player; green = marked joints. '
                       f'Mean marked-joint distance {total:.1f}px (pelvis aligned only). Diagnostic, not a score.', font=font, fill='white')
    out.parent.mkdir(parents=True, exist_ok=True); sheet.save(out, quality=92)
    print(f'mean {total:.1f}px  ' + '  '.join(f'f{f}:{np.mean(list(e.values())):.0f}' for f, e in report.items()))
    return report

if __name__ == '__main__':
    main()
