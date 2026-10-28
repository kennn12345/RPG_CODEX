"""Right punch from IMG_8634, fitted onto the IMG_8635 filmed stance (UEFN skeleton).

RPG_SKEL=uefn python capture_punch8634.py
The recorded motion is applied as a change from the recorded stance just before the punch, so the clip starts and
ends exactly in RPG_FightStance_Capture8635. Before it, an authored pull-back loads the punch (torso turned to bring
the right shoulder back, rear fist drawn back, weight settled back); a held press waits there at LOAD. Impact (full
extension) is at IMPACT; RPGPrototypeAnimationComponent uses the same two times.
"""
import copy
import json
import math
from pathlib import Path
import protoanim as p
import getupcheck as g
from capture_getups import repair_chain, follow_ik
from capture_punches8630 import straight_wrist, elbow_clearance, fit_punch_shoulder, ease, blend

HERE = Path(__file__).resolve().parent
FPS = 60
TP = 'RPG_Punch_R_Capture8634'
FP = 'RPG_FP_Punch_R_Capture8634'
LOAD, IMPACT = .20, .42
SETTLE = .25                       # Seconds to ease into the exact stance after the recorded recovery.
# The pull-back reached at LOAD (user request: the punch should load up rather than fire straight from the stance).
LOAD_TURN = 12.                    # Degrees the torso turns the right shoulder back, spread over spine_01-03.
LOAD_PELVIS_TURN = 4.
LOAD_HIPS = [0., -2., -1.5]        # Weight settles back and down (mesh space: +Y forward, -X the character's right).
LOAD_FIST = {'r': [-1.5, -8., -3.], 'l': [0., 2., 0.]}   # Rear fist draws back and drops a little; lead hand holds out.
UNWIND = .7                        # Fraction of the strike over which the pull-back unwinds.
SMOOTH_RADIUS, SMOOTH_SIGMA = 3, 1.5   # Frames; temporal smoothing applied after the fit.
# Source seconds in IMG_8634 (solve frame 0 = 0.0 s). The 51.9 s punch faces the camera with the right side, so the
# striking arm is never hidden; its guard hand drifts least of the five in that view. Stance before the punch, arrival
# at full extension, start of the retraction (the video holds the arm out ~0.5 s), back in guard.
PUNCH = dict(base=51.33, peak=51.80, retract=52.27, back=52.77)
HOLD = .1                          # Clip seconds the extended arm stays out (the recorded hold, compressed).
RETURN_SPEED = 1.25                # The retraction plays a little faster than recorded.
# The solve has the fist climbing 22 cm from guard to impact (its guard sits at shoulder height); the video shows about
# 9 cm, chin to mouth level. Halving the strike's height above the shoulder lands it at mouth height as filmed.
EXTENSION_UP_SCALE = .5

SOURCE = json.loads((HERE/'captures/IMG8634_uefn_retarget.json').read_text())
STANCES = json.loads((HERE/'capture_stance8635_uefn.json').read_text())
STANCE = STANCES['RPG_FightStance_Capture8635']['poses'][0]
FP_STANCE = STANCES['RPG_FP_FightStance_Capture8635']['poses'][0]
REST = p.to_cs(STANCE); FP_REST = p.to_cs(FP_STANCE)
# Planted legs keep the stance's knee direction (over the toes); the recorded thigh twist would bow a knee sideways.
KNEE_POLE = {s: p.sub(REST['calf_'+s][1], REST['thigh_'+s][1]) for s in 'lr'}
SKIP = ('index', 'middle', 'ring', 'pinky', 'thumb', 'metacarpal', 'ik_')


def sample(seconds):
    f = seconds*SOURCE['fps']; a = max(0, min(int(f), SOURCE['frames']-1)); b = min(a+1, SOURCE['frames']-1)
    return blend(SOURCE['poses'][a], SOURCE['poses'][b], f-a)


def facing_turn(cs):
    f = g.frame(cs, 'spine_05')[1]
    return p.qaxis([0, 0, 1], 90.-math.degrees(math.atan2(f[1], f[0])))


BASE = None; BASE_CS = None; TURN = None


def turn_world(local, bone, degrees):
    """Turn a bone about the vertical in world space, children following."""
    cs = p.to_cs(local); parent = p.PARENT[bone]
    q = p.qmul(p.qaxis([0., 0., 1.], degrees), cs[bone][0])
    local[bone] = (p.qnorm(p.qmul(p.qconj(cs[parent][0]), q)) if parent else p.qnorm(q), local[bone][1])


def recorded(seconds, strike_weight, load=0.):
    """Recorded pose at `seconds`, expressed as a change from BASE and applied to the filmed stance, plus `load` of
    the pull-back."""
    frame = sample(seconds); source_cs = p.to_cs(frame)
    local = copy.deepcopy(STANCE)
    for bone in p.BONES:
        if bone == 'root' or any(k in bone for k in SKIP): continue
        delta = p.qmul(p.qconj(BASE[bone][0]), frame[bone][0])
        local[bone] = (p.qnorm(p.qmul(STANCE[bone][0], delta)), local[bone][1])
    hips = p.qrot(TURN, p.sub(source_cs['pelvis'][1], BASE_CS['pelvis'][1]))
    # Never rise above the stance (its legs are nearly straight), and sink slightly into the drive so the planted
    # legs can reach the forward hip shift.
    hips[2] = min(hips[2], 0.)-1.5*strike_weight
    hips = p.add(hips, p.mul(LOAD_HIPS, load))
    local['pelvis'] = (local['pelvis'][0], p.add(STANCE['pelvis'][1], hips))
    if load > 0.:
        turn_world(local, 'pelvis', LOAD_PELVIS_TURN*load)
        for bone, share in (('spine_01', .25), ('spine_02', .35), ('spine_03', .4)):
            turn_world(local, bone, LOAD_TURN*share*load)
    for s in 'lr':
        # Feet stay planted on the stance footprint.
        cs = p.to_cs(local); foot = 'foot_'+s
        repair_chain(local, 'thigh_'+s, 'calf_'+s, foot, REST[foot][1], KNEE_POLE[s], REST[foot][0])
    for s in 'lr':
        cs = p.to_cs(local); hand = 'hand_'+s
        target = p.add(REST[hand][1], p.qrot(TURN, p.sub(source_cs[hand][1], BASE_CS[hand][1])))
        target = p.add(target, p.mul(LOAD_FIST[s], load))
        if s == 'r' and strike_weight > 0.:
            # The two solves place the guard fist differently (cheek in IMG_8635, shoulder height here), so a change
            # from the guard would land the strike too high. Toward full extension, use the recorded fist position
            # relative to the shoulder instead: the real reach, height and line of the punch.
            reach = p.qrot(TURN, p.sub(source_cs[hand][1], source_cs['upperarm_r'][1]))
            reach[2] *= EXTENSION_UP_SCALE
            extended = p.add(cs['upperarm_r'][1], reach)
            target = p.lerp(target, extended, strike_weight)
            fit_punch_shoulder(local, s, target); cs = p.to_cs(local)
        target = elbow_clearance(cs, s, target)
        repair_chain(local, 'upperarm_'+s, 'lowerarm_'+s, hand, target, p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1]))
        straight_wrist(local, s, .8*strike_weight if s == 'r' else 0.)
    local['root'] = ([0., 0., 0., 1.], [0., 0., 0.])
    return follow_ik(local)


def source_time(t):
    """Clip time -> source time: the stance holds through the pull-back, then the recorded outward motion is fitted
    so full extension lands on IMPACT."""
    if t <= LOAD: return PUNCH['base']
    if t <= IMPACT: return PUNCH['base']+(PUNCH['peak']-PUNCH['base'])*(t-LOAD)/(IMPACT-LOAD)
    if t <= IMPACT+HOLD: return PUNCH['peak']+(PUNCH['retract']-PUNCH['peak'])*(t-IMPACT)/HOLD
    return min(PUNCH['back'], PUNCH['retract']+(t-IMPACT-HOLD)*RETURN_SPEED)


def first_person(pose, t, length):
    """Fit the arms to the first-person camera guard, except the striking hand around the hit."""
    view = copy.deepcopy(pose); cs = p.to_cs(pose)
    for s in 'lr':
        weight = 1. if s == 'l' else min(1., ease((abs(t-IMPACT)-.09)/.12) if t <= IMPACT else ease((t-IMPACT-.09)/(length-IMPACT-.09)))
        target = p.add(cs['hand_'+s][1], p.mul(p.sub(FP_REST['hand_'+s][1], REST['hand_'+s][1]), weight))
        if weight > 0.: fit_punch_shoulder(view, s, target)
        vcs = p.to_cs(view); target = elbow_clearance(vcs, s, target)
        rotation = p._nlerp(cs['hand_'+s][0], p.qmul(FP_REST['hand_'+s][0], p.qmul(p.qconj(REST['hand_'+s][0]), cs['hand_'+s][0])), weight)
        repair_chain(view, 'upperarm_'+s, 'lowerarm_'+s, 'hand_'+s, target, p.sub(vcs['lowerarm_'+s][1], vcs['upperarm_'+s][1]), rotation)
    return follow_ik(view)


def smooth(rows):
    """Gaussian smoothing of every bone over neighbouring frames (end frames fixed), then the feet re-planted, so the
    seams between stance, pull-back, strike, hold and recovery carry no velocity pops."""
    weights = [math.exp(-.5*(k/SMOOTH_SIGMA)**2) for k in range(-SMOOTH_RADIUS, SMOOTH_RADIUS+1)]
    out = [rows[0]]
    for i in range(1, len(rows)-1):
        pose = None; total = 0.
        for k, w in zip(range(-SMOOTH_RADIUS, SMOOTH_RADIUS+1), weights):
            other = rows[min(len(rows)-1, max(0, i+k))]
            total += w
            pose = copy.deepcopy(other) if pose is None else blend(pose, other, w/total)
        for s in 'lr':
            cs = p.to_cs(pose); foot = 'foot_'+s
            repair_chain(pose, 'thigh_'+s, 'calf_'+s, foot, REST[foot][1], KNEE_POLE[s], REST[foot][0])
        pose['root'] = ([0., 0., 0., 1.], [0., 0., 0.])
        out.append(follow_ik(pose))
    out.append(rows[-1])
    return out


def jolt(rows):
    """Largest frame-to-frame change in fist velocity (cm/frame^2): a pop shows as a spike."""
    hands = [p.to_cs(r)['hand_r'][1] for r in rows]
    return max(p.length(p.sub(p.sub(hands[i+1], hands[i]), p.sub(hands[i], hands[i-1]))) for i in range(1, len(hands)-1))


def build():
    global BASE, BASE_CS, TURN
    BASE = sample(PUNCH['base']); BASE_CS = p.to_cs(BASE); TURN = facing_turn(BASE_CS)
    recover = HOLD+(PUNCH['back']-PUNCH['retract'])/RETURN_SPEED
    length = IMPACT+recover+SETTLE
    end_pose = recorded(PUNCH['back'], 0.)
    rows = []
    for i in range(round(length*FPS)+1):
        t = i/FPS
        if t <= IMPACT+recover:
            strike = 0. if t <= LOAD else ease((t-LOAD)/(IMPACT-LOAD)) if t <= IMPACT else 1.-ease((t-IMPACT)/recover)
            load = ease(t/LOAD) if t <= LOAD else 1.-ease((t-LOAD)/((IMPACT-LOAD)*UNWIND))
            pose = recorded(source_time(t), strike, load)
        else:
            pose = blend(end_pose, STANCE, ease((t-IMPACT-recover)/SETTLE))
        rows.append(pose)
    rows[0] = copy.deepcopy(STANCE); rows[-1] = copy.deepcopy(STANCE)
    rows = smooth(rows)
    views = [first_person(pose, i/FPS, length) for i, pose in enumerate(rows)]
    rows[0] = copy.deepcopy(STANCE); rows[-1] = copy.deepcopy(STANCE)
    views[0] = copy.deepcopy(FP_STANCE); views[-1] = copy.deepcopy(FP_STANCE)
    out = {}
    for name, poses in [(TP, rows), (FP, views)]:
        out[name] = dict(name=name, fps=FPS, frames=len(poses), bones=p.BONES, poses=poses, impact=IMPACT, load=LOAD,
                         keys=[0., LOAD, IMPACT, IMPACT+recover, length], hand='r',
                         source='IMG_8634.mov right punch %.2f-%.2f s (peak %.2f s) on the IMG_8635 stance'
                                % (PUNCH['base'], PUNCH['back'], PUNCH['peak']), source_seconds=PUNCH)
    return out


def check(clips):
    for name, clip in clips.items():
        poses = clip['poses']
        stance = FP_STANCE if name == FP else STANCE
        assert poses[0] == stance and poses[-1] == stance, (name, 'stance seams')
        lengths = None
        for i, local in enumerate(poses):
            assert local['root'] == [[0., 0., 0., 1.], [0., 0., 0.]] or local['root'] == ([0., 0., 0., 1.], [0., 0., 0.]), (name, i, 'root')
            for b, (q, t) in local.items():
                assert all(math.isfinite(v) for v in list(q)+list(t)), (name, i, b)
                assert abs(sum(v*v for v in q)-1.) < 1.e-4, (name, i, b, 'quaternion')
            cs = p.to_cs(local)
            for s in 'lr':
                for b in ['foot_'+s, 'ball_'+s]:
                    assert p.length(p.sub(cs[b][1], REST[b][1])) < .1, (name, i, b, 'foot slide')
            arm = [p.length(p.sub(cs['lowerarm_r'][1], cs['upperarm_r'][1])), p.length(p.sub(cs['hand_r'][1], cs['lowerarm_r'][1]))]
            lengths = lengths or arm
            assert max(abs(a-b) for a, b in zip(arm, lengths)) < .05, (name, i, 'arm stretched')
        hit = p.to_cs(poses[round(IMPACT*FPS)]); rest = p.to_cs(poses[0])
        print(name, clip['frames'], 'frames', round((clip['frames']-1)/FPS, 3), 's; right fist forward at impact',
              round(hit['hand_r'][1][1]-rest['hand_r'][1][1], 1), 'cm; pull-back',
              round(p.to_cs(poses[round(LOAD*FPS)])['hand_r'][1][1]-rest['hand_r'][1][1], 1), 'cm; max fist jolt',
              round(jolt(poses), 2), 'cm/frame^2')
        g.check_clip(name, clip)


if __name__ == '__main__':
    assert p.SP.NAME == 'uefn', 'Use RPG_SKEL=uefn'
    assert None not in PUNCH.values(), 'Set PUNCH source seconds first'
    old = p.SOFT_IK, p.SOFT_IK_ARM; p.SOFT_IK = p.SOFT_IK_ARM = 1.e-7
    try: clips = build()
    finally: p.SOFT_IK, p.SOFT_IK_ARM = old
    check(clips)
    (HERE/'capture_punch8634_uefn.json').write_text(json.dumps(clips))
    from preview import render
    render(clips[TP], str(HERE/'previews'/f'{TP}.png'), clips[TP]['keys'])
