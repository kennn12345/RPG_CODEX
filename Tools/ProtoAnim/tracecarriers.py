"""Per-frame movement (cm) of a few bones and of what carries the body (capsule, mesh component, root bone) from
Saved/GetUpTrace.csv, over a window after each get-up starts: tells a whole-body shift (the capsule or mesh moving) from
the animation itself. Run: python tracecarriers.py [from_s to_s]"""
import csv, math, os, sys

t0, t1 = (float(sys.argv[1]), float(sys.argv[2])) if len(sys.argv) > 2 else (0.08, 0.40)
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'GetUpTrace.csv')
rows = list(csv.DictReader(open(path)))
for who in ('player', 'npc'):
    pel = [(float(r['t']), r['state']) for r in rows if r['who'] == who and r['bone'] == 'pelvis']
    up = next((t for (t, s), (_, s0) in zip(pel[1:], pel) if s == 'up' and s0 == 'down'), None)
    if up is None: continue
    print(who, 'gets up at', round(up, 2))
    names = ['head', 'foot_l', '@actor', '@mesh', '@root']
    series = {b: [(float(r['t']), (float(r['x']), float(r['y']), float(r['z']))) for r in rows if r['who'] == who and r['bone'] == b]
              for b in names}
    for i in range(1, len(series['head'])):
        t = series['head'][i][0]
        if t0 <= t - up <= t1:
            print(f'{t - up:6.3f} ' + ' '.join(f'{b}:{math.dist(series[b][i][1], series[b][i - 1][1]):5.2f}' for b in names))
