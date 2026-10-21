"""Solve the first-person combat stance hands for a symmetric, wider screen layout.

  python fpsym.py  ->  prints FP_STANCE_FIST (combat stance) values to paste into fpanim.py, the screen positions before /
                       after (x, y in -1..1; - = left / down), and where the block (guard) puts the fists. The block's
                       layout is set directly by fpanim.FP_GUARD_FIST (fpanim solves its offset itself).
"""
import contextlib, io, math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
with contextlib.redirect_stdout(io.StringIO()):
    import fpanim, poses
from protoanim import sample_track, build_pose, lerp, sub

TARGET = {'stance': 0.85, 'stance_y': None}
TANV = 9 / 16

def screen(keys, spec_name, **fp):
    _, _, springs, pins = poses.SPECS[spec_name]
    P = sample_track(keys, [0.0], poses.GROUPS, springs, pins)[0]
    local = fpanim.fp_frame(P, 0.0, **fp)
    from protoanim import to_cs
    cs = to_cs(local); cam = fpanim.camera(cs); out = {}
    for s in 'lr':
        d = sub(fpanim.fist(cs, s), cam); out[s] = (-d[0] / d[1], d[2] / d[1] / TANV, d[1])
    return out

def solve_stance(xt, yt):
    for _ in range(12):
        fpanim.OFFSET = fpanim._offsets()
        sc = screen(poses.COMBAT_STANCE, 'RPG_Proto_CombatStance')
        for s, sx in (('l', -xt), ('r', xt)):
            x, y, depth = sc[s]; f = fpanim.FP_STANCE_FIST[s]
            f[0] += -(sx - x) * depth * 0.9        # screen x = -X / depth
            f[2] += (yt - y) * depth * TANV * 0.9  # screen y = Z / depth / TANV
    fpanim.OFFSET = fpanim._offsets()
    return screen(poses.COMBAT_STANCE, 'RPG_Proto_CombatStance')

if __name__ == '__main__':
    fmt = lambda sc: '  '.join(f"{s}({sc[s][0]:+.3f},{sc[s][1]:+.3f}) depth {sc[s][2]:.1f}" for s in 'lr')
    print('stance before:', fmt(screen(poses.COMBAT_STANCE, 'RPG_Proto_CombatStance')))
    b = screen(poses.COMBAT_STANCE, 'RPG_Proto_CombatStance'); sy = (b['l'][1] + b['r'][1]) / 2
    st = solve_stance(TARGET['stance'], sy)
    print('stance after: ', fmt(st)); print('  FP_STANCE_FIST =', {s: [round(v, 1) for v in fpanim.FP_STANCE_FIST[s]] for s in 'lr'})
    guard = dict(shift=fpanim.GUARD_SHIFT, poles=fpanim.FP_GUARD_POLE, clav=fpanim.FP_GUARD_CLAV)
    print('block (guard):', fmt(screen(poses.UNARMED_GUARD, 'RPG_Proto_UnarmedGuard', **guard)), ' <- fpanim.FP_GUARD_FIST')
