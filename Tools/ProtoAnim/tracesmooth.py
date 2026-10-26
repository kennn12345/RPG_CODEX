"""Hitches in what the game really played: reads Saved/GetUpTrace.csv (RPG.Npc.Collision with RPG_GETUP_TRACE=1: world
bone positions every frame) and reports, for the stretch where each body is getting up, frames where a bone jumps
(moves far more than the frames either side: a pop), stalls between faster stretches, or the frame time itself spikes.
Run: python tracesmooth.py [path]
"""
import csv, math, os, sys
from collections import defaultdict

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'GetUpTrace.csv')
rows = defaultdict(lambda: defaultdict(list))   # who -> bone -> [(t, (x, y, z), state)]
for r in csv.DictReader(open(path)):
    rows[r['who']][r['bone']].append((float(r['t']), (float(r['x']), float(r['y']), float(r['z'])), r['state']))

for who, bones in rows.items():
    pel = bones['pelvis']
    # getting up: from the frame the body stops being down to 4 s after
    up_at = next((t for (t, _, s), (_, _, s0) in zip(pel[1:], pel) if s == 'up' and s0 == 'down'), None)
    if up_at is None:
        print(who, ': never got up in the trace'); continue
    print(f'{who}: getting up from {up_at:.2f}s')
    ts = [t for t, _, _ in pel]
    dts = [b - a for a, b in zip(ts, ts[1:])]
    med_dt = sorted(dts)[len(dts) // 2]
    for i, d in enumerate(dts):
        if up_at - 0.5 <= ts[i] <= up_at + 4 and d > 2.5 * med_dt:
            print(f'   {ts[i] - up_at:5.2f}s  frame time {d * 1000:.0f} ms (typical {med_dt * 1000:.0f})')
    for bone, seq in bones.items():
        sp = []
        for (t0, p0, _), (t1, p1, _) in zip(seq, seq[1:]):
            sp.append((t0, math.dist(p0, p1) / max(t1 - t0, 1e-4)))
        for i in range(1, len(sp) - 1):
            t, v = sp[i]
            if not (up_at - 0.2 <= t <= up_at + 4): continue
            nb = 0.5 * (sp[i - 1][1] + sp[i + 1][1])
            if v > 150 and v > 3 * nb:
                print(f'   {t - up_at:5.2f}s  {bone:10s} pop    {v:5.0f} cm/s against {nb:4.0f} either side')
            if v < 0.2 * min(sp[i - 1][1], sp[i + 1][1]) and min(sp[i - 1][1], sp[i + 1][1]) > 60:
                print(f'   {t - up_at:5.2f}s  {bone:10s} stall  {v:5.0f} cm/s between {sp[i - 1][1]:.0f} and {sp[i + 1][1]:.0f}')
