"""Stance / kick geometry report: the numbers that decide whether a stance reads compact or wide.

  python stancecheck.py [clips.json path]

stance  ankle and ball spacing (sideways = X, front/back = Y), each foot's turnout (heel-to-ball heading vs straight
        ahead, + = toes turned out), each knee's offset outside its ankle and its angle against the toe line
kicks   hip (pelvis) and head sideways travel from stance, the kicking knee's sideways travel through the chamber, and
        how far the knee sits outside its own hip at the chamber; the support ball's slide while planted
"""
import json, math, os, sys
from protoanim import to_cs, sub, length, norm, HERE

def foot_turnout(cs, s):
    a, b = cs['foot_' + s][1], cs['ball_' + s][1]
    h = math.degrees(math.atan2(b[0] - a[0], b[1] - a[1]))   # + = toes toward character-left
    return h if s == 'l' else -h                               # + = turned out, for either foot

def knee_vs_toe(cs, s):
    hip, kn, ank, ball = cs['thigh_' + s][1], cs['calf_' + s][1], cs['foot_' + s][1], cs['ball_' + s][1]
    line = norm(sub(ank, hip)); v = sub(kn, hip); d = sum(x * y for x, y in zip(v, line))
    off = [v[i] - line[i] * d for i in range(3)]
    if math.hypot(off[0], off[1]) < 1e-3: return 0.0
    a1 = math.degrees(math.atan2(off[0], off[1])); a2 = math.degrees(math.atan2(ball[0] - ank[0], ball[1] - ank[1]))
    dd = (a1 - a2 + 180) % 360 - 180
    return dd if s == 'l' else -dd                                # + = knee outside its toe line

def stance(cs):
    al, ar, bl, br = cs['foot_l'][1], cs['foot_r'][1], cs['ball_l'][1], cs['ball_r'][1]
    return {'ankle_side': round(al[0] - ar[0], 1), 'ankle_fwd': round(al[1] - ar[1], 1), 'ankle_dist': round(length(sub(al, ar)), 1),
            'ball_side': round(bl[0] - br[0], 1), 'ball_fwd': round(bl[1] - br[1], 1), 'ball_dist': round(length(sub(bl, br)), 1),
            'turnout_l': round(foot_turnout(cs, 'l'), 1), 'turnout_r': round(foot_turnout(cs, 'r'), 1),
            'knee_out_l': round(cs['calf_l'][1][0] - al[0], 1), 'knee_out_r': round(ar[0] - cs['calf_r'][1][0], 1),
            'knee_toe_l': round(knee_vs_toe(cs, 'l'), 1), 'knee_toe_r': round(knee_vs_toe(cs, 'r'), 1),
            'hip_width': round(cs['thigh_l'][1][0] - cs['thigh_r'][1][0], 1)}

def kick(clip, side, chamber_t):
    fr = [to_cs({b: tuple(v) for b, v in p.items()}) for p in clip['poses']]
    sup = 'l' if side == 'r' else 'r'
    p0, h0, k0 = fr[0]['pelvis'][1], fr[0]['head'][1], fr[0]['calf_' + side][1]
    hipx = [f['pelvis'][1][0] - p0[0] for f in fr]; headx = [f['head'][1][0] - h0[0] for f in fr]
    kx = [f['calf_' + side][1][0] for f in fr]
    c = fr[min(int(round(chamber_t * clip['fps'])), len(fr) - 1)]
    ball0 = fr[0]['ball_' + sup][1]; slide = max(math.hypot(f['ball_' + sup][1][0] - ball0[0], f['ball_' + sup][1][1] - ball0[1]) for f in fr)
    return {'hip_side_max': round(max(hipx, key=abs), 1), 'head_side_max': round(max(headx, key=abs), 1),
            'knee_side_travel': round(max(kx) - min(kx), 1),
            'knee_vs_own_hip_at_chamber': round(c['calf_' + side][1][0] - c['thigh_' + side][1][0], 1),
            'knee_at_chamber': [round(x) for x in c['calf_' + side][1]], 'hip_at_chamber': [round(x) for x in c['thigh_' + side][1]],
            'support_ball_drift': round(slide, 1)}

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'clips.json')
    clips = json.load(open(path))
    cs = to_cs({b: tuple(v) for b, v in clips['RPG_Proto_CombatStance']['poses'][0].items()})
    print('stance:', stance(cs))
    for nm, side, ch in (('RPG_Proto_FrontKick_R', 'r', 0.28), ('RPG_Proto_FrontKick_L', 'l', 0.2333)):
        print(nm, kick(clips[nm], side, ch))
