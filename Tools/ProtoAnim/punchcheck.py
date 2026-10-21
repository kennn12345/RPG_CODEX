"""Form check for a punch in clips.json: how a trained punch should look, as numbers per frame.
  lean   - chest forward of the hips (degrees; a cross stays fairly upright, ~10 deg or less)
  bend   - sideways spine bend seen from the front (degrees; should stay small)
  guard  - non-punching fist's distance from the chin (cm; should stay glued, ~15 or less)
  elbowZ - punching elbow height relative to its shoulder (cm; positive = elbow up and out: 'chicken wing')
Run: python punchcheck.py RPG_Proto_Punch_R   (also prints the worst value of each over the clip)
"""
import json, math, os, sys
from protoanim import to_cs, sub, HERE

name = sys.argv[1] if len(sys.argv) > 1 else 'RPG_Proto_Punch_R'
side = 'r' if name.endswith('_R') or '_R_' in name else 'l'
guard = 'l' if side == 'r' else 'r'
clip = json.load(open(os.path.join(HERE, 'clips.json')))[name]
rows = []
for i, p in enumerate(clip['poses']):
    cs = to_cs({b: tuple(v) for b, v in p.items()})
    hips, chest = cs['pelvis'][1], cs['spine_05'][1]
    up = sub(chest, hips)
    lean = math.degrees(math.atan2(up[1], up[2]))           # +Y forward
    bend = math.degrees(math.atan2(abs(up[0]), up[2]))       # X sideways
    chin = cs['head'][1]
    g = cs['hand_' + guard][1]
    gd = math.dist(g, (chin[0], chin[1] + 8, chin[2] - 8))   # a fist on the guard sits in front of and below the head
    elbow_z = cs['lowerarm_' + side][1][2] - cs['upperarm_' + side][1][2]
    rows.append((i / clip['fps'], lean, bend, gd, elbow_z))
print('%s  (impact %.3fs)' % (name, clip['impact']))
print('  t      lean   bend  guard  elbowZ')
for t, lean, bend, gd, ez in rows[::3]:
    print('  %.3f %6.1f %6.1f %6.1f %6.1f' % (t, lean, bend, gd, ez))
print('WORST: lean %.1f deg, bend %.1f deg, guard %.1f cm from chin, elbow %.1f cm above shoulder' % (
    max(r[1] for r in rows), max(r[2] for r in rows), max(r[3] for r in rows), max(r[4] for r in rows)))
