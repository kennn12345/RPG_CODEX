"""Read Saved/GetUpMisses.csv (landings no get-up start was close to) and describe each: which way the body lies and
where the limbs are, in the matching frame (x = from the hips toward the head along the ground, y = to the right of
that, z = up; cm from the hips). Run: python getupmisses.py"""
import csv, math, os

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'GetUpMisses.csv')
if not os.path.exists(path):
    print('no misses recorded'); raise SystemExit
for row in csv.reader(open(path, encoding='utf-8-sig')):
    who, clip, turn, seg, hips = row[:5]
    pts = {row[i]: tuple(float(v) for v in row[i + 1:i + 4]) for i in range(5, len(row), 4)}
    chest = pts['spine_05']
    # which way the front faces, from the shoulders: seen from above, a body lying face down has its left shoulder on
    # the left of the head direction (as standing), face up on the right; a big height difference is lying on a side
    side = pts['upperarm_l'][1] - pts['upperarm_r'][1]
    tilt = math.degrees(math.atan2(pts['upperarm_l'][2] - pts['upperarm_r'][2], abs(side) + 1e-6))
    lying = 'face down' if side < 0 else 'face up'
    if abs(tilt) > 50: lying = 'on the ' + ('right' if tilt > 0 else 'left') + ' side'
    print(f'{who}: matched {clip} but {seg} turns {turn} deg - lies {lying} (shoulder tilt {tilt:.0f}), hips {hips} cm up')
    for b in ('head', 'spine_05', 'upperarm_l', 'lowerarm_l', 'hand_l', 'upperarm_r', 'lowerarm_r', 'hand_r',
              'thigh_l', 'calf_l', 'foot_l', 'thigh_r', 'calf_r', 'foot_r'):
        x, y, z = pts[b]
        print(f'   {b:11s} ahead {x:6.1f}  right {y:6.1f}  up {z:6.1f}')
