"""Right and left punches from IMG_8634, fitted onto the IMG_8635 filmed stance (UEFN skeleton).

RPG_SKEL=uefn python capture_punch8634.py
The recorded motion is applied as a change from the recorded stance just before the punch, so the clip starts and
ends exactly in RPG_FightStance_Capture8635. Before it, an authored pull-back loads the punch (torso turned to bring
the striking shoulder back, striking fist drawn back, weight settled back); a held press waits there at LOAD. Impact
(full extension) is at IMPACT; RPGPrototypeAnimationComponent uses the same two times.

The video only has right punches. The left punch mirrors the recorded right punch (flight_symmetry.mirror_pose) and
fits it onto the same, unmirrored stance; every authored turn and sideways offset flips with the striking side.
"""
import copy
import json
import math
from pathlib import Path
import protoanim as p
import getupcheck as g
from capture_getups import repair_chain, follow_ik
from capture_punches8630 import straight_wrist, elbow_clearance, fit_punch_shoulder, ease, blend
from capture_stance8635 import cap_reach, FP_REACH_LIMIT
from flight_symmetry import mirror_pose

HERE = Path(__file__).resolve().parent
FPS = 60
LOAD, IMPACT = .20, .42
SETTLE = .25                       # Seconds to ease into the exact stance after the recorded recovery.
# The pull-back reached at LOAD (user request: the punch should load up rather than fire straight from the stance).
LOAD_TURN = 20.                    # Degrees the torso turns the striking shoulder back, spread over spine_01-03.
LOAD_PELVIS_TURN = 7.
LOAD_HIPS = [0., -3., -2.]         # Weight settles back and down (mesh space: +Y forward, -X the character's right).
LOAD_FIST = [-2., -11., -3.]       # Striking fist draws back and drops a little (right hand; X flips for the left).
LOAD_GUARD = [0., 2., 0.]          # The other hand holds out a little.
UNWIND = .7                        # Fraction of the strike over which the pull-back unwinds.
# Follow-through (user request): the torso keeps rotating through the target and the striking shoulder drives
# forward, carrying the punch's momentum. Builds through the strike, peaks just after impact, holds while the arm is
# out, then unwinds with the retraction. The head counter-turns so the eyes (and first-person camera) stay on target.
DRIVE_TURN = 14.                   # Torso rotation through the target (striking shoulder forward), over spine_01-03.
DRIVE_PELVIS_TURN = 6.
DRIVE_CLAVICLE = 8.                # Extra striking-shoulder protraction.
DRIVE_LEAN = 4.                    # Forward lean of the chest.
DRIVE_HIPS = [0., 2.5, 0.]         # Weight carried forward.
DRIVE_RISE, DRIVE_PEAK, DRIVE_FALL = .4, .05, .35   # Starts 40% into the strike, peaks 0.05 s after impact, unwinds 0.35 s after the hold.
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
REACH_LIMIT = .97                  # Fraction of the rig's full arm length the strike reaches for.

SOURCE = json.loads((HERE/'captures/IMG8634_uefn_retarget.json').read_text())
STANCES = json.loads((HERE/'capture_stance8635_uefn.json').read_text())
STANCE = STANCES['RPG_FightStance_Capture8635']['poses'][0]
FP_STANCE = STANCES['RPG_FP_FightStance_Capture8635']['poses'][0]
REST = p.to_cs(STANCE); FP_REST = p.to_cs(FP_STANCE)
# Planted legs keep the stance's knee direction (over the toes); the recorded thigh twist would bow a knee sideways.
KNEE_POLE = {s: p.sub(REST['calf_'+s][1], REST['thigh_'+s][1]) for s in 'lr'}
SKIP = ('index', 'middle', 'ring', 'pinky', 'thumb', 'metacarpal', 'ik_')

# Striking side, set by use_side(): S strikes, O guards; SIGN flips turns (+: S shoulder back) and sideways offsets.
S, O, SIGN = 'r', 'l', 1.
_MIRRORED = {}
BASE = None; BASE_CS = None; TURN = None


def use_side(side):
    global S, O, SIGN
    S, O, SIGN = side, ('l' if side == 'r' else 'r'), (1. if side == 'r' else -1.)


def side_x(v):
    """A right-punch offset for the current striking side (mesh X flips for the left)."""
    return [v[0]*SIGN, v[1], v[2]]


def source_pose(i):
    if S == 'r': return SOURCE['poses'][i]
    if i not in _MIRRORED: _MIRRORED[i] = mirror_pose(SOURCE['poses'][i])
    return _MIRRORED[i]


def sample(seconds):
    f = seconds*SOURCE['fps']; a = max(0, min(int(f), SOURCE['frames']-1)); b = min(a+1, SOURCE['frames']-1)
    return blend(source_pose(a), source_pose(b), f-a)


def facing_turn(cs):
    f = g.frame(cs, 'spine_05')[1]
    return p.qaxis([0, 0, 1], 90.-math.degrees(math.atan2(f[1], f[0])))


def turn_world(local, bone, degrees, axis=(0., 0., 1.)):
    """Turn a bone about a world axis (default the vertical), children following."""
    cs = p.to_cs(local); parent = p.PARENT[bone]
    q = p.qmul(p.qaxis(list(axis), degrees), cs[bone][0])
    local[bone] = (p.qnorm(p.qmul(p.qconj(cs[parent][0]), q)) if parent else p.qnorm(q), local[bone][1])


def elbow_pole(strike):
    """Striking elbow direction: as in the stance, rolling out to the side as the arm extends (the punch's natural
    elbow turn). Steered explicitly, because near full extension the elbow is barely defined and, with the torso
    turning through, it otherwise snaps between the two in a few frames."""
    return p.norm(p.lerp(p.norm(p.sub(REST['lowerarm_'+S][1], REST['upperarm_'+S][1])), side_x([-1., 0., -.35]), strike))


def drive_weight(t, recover):
    """Follow-through amount at clip time t."""
    start = LOAD+DRIVE_RISE*(IMPACT-LOAD); peak = IMPACT+DRIVE_PEAK; hold_end = max(peak, IMPACT+HOLD)
    if t <= start: return 0.
    if t <= peak: return ease((t-start)/(peak-start))
    if t <= hold_end: return 1.
    return 1.-ease((t-hold_end)/DRIVE_FALL)


def recorded(seconds, strike_weight, load=0., drive=0.):
    """Recorded pose at `seconds`, expressed as a change from BASE and applied to the filmed stance, plus `load` of
    the pull-back and `drive` of the follow-through."""
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
    hips = p.add(p.add(hips, p.mul(LOAD_HIPS, load)), p.mul(DRIVE_HIPS, drive))
    local['pelvis'] = (local['pelvis'][0], p.add(STANCE['pelvis'][1], hips))
    # Positive (times SIGN) turns bring the striking shoulder back (pull-back); negative ones drive it through.
    pelvis_turn = SIGN*(LOAD_PELVIS_TURN*load-DRIVE_PELVIS_TURN*drive)
    torso_turn = SIGN*(LOAD_TURN*load-DRIVE_TURN*drive)
    if pelvis_turn or torso_turn:
        turn_world(local, 'pelvis', pelvis_turn)
        for bone, share in (('spine_01', .25), ('spine_02', .35), ('spine_03', .4)):
            turn_world(local, bone, torso_turn*share)
        # Keep the head on the target: undo the accumulated yaw at the neck.
        turn_world(local, 'neck_01', -(pelvis_turn+torso_turn))
    if drive > 0.:
        turn_world(local, 'spine_03', -DRIVE_LEAN*drive, (1., 0., 0.))   # top of the chest toward +Y
        turn_world(local, 'neck_01', DRIVE_LEAN*drive, (1., 0., 0.))
        turn_world(local, 'clavicle_'+S, -SIGN*DRIVE_CLAVICLE*drive)
    for s in 'lr':
        # Feet stay planted on the stance footprint.
        cs = p.to_cs(local); foot = 'foot_'+s
        repair_chain(local, 'thigh_'+s, 'calf_'+s, foot, REST[foot][1], KNEE_POLE[s], REST[foot][0])
    for s in 'lr':
        cs = p.to_cs(local); hand = 'hand_'+s
        target = p.add(REST[hand][1], p.qrot(TURN, p.sub(source_cs[hand][1], BASE_CS[hand][1])))
        target = p.add(target, p.mul(side_x(LOAD_FIST) if s == S else LOAD_GUARD, load))
        if s == S and strike_weight > 0.:
            # The two solves place the guard fist differently (cheek in IMG_8635, shoulder height here), so a change
            # from the guard would land the strike too high. Toward full extension, use the recorded fist position
            # relative to the shoulder instead: the real reach, height and line of the punch.
            reach = p.qrot(TURN, p.sub(source_cs[hand][1], source_cs['upperarm_'+s][1]))
            reach[2] *= EXTENSION_UP_SCALE
            # The recorded arm is longer than the rig's: cap the reach just short of full length, so the shoulder
            # solver never has to snap in (the follow-through's shoulder drive supplies the extra distance smoothly).
            arm = p.length(p.sub(REST['lowerarm_'+s][1], REST['upperarm_'+s][1]))+p.length(p.sub(REST[hand][1], REST['lowerarm_'+s][1]))
            reach = p.mul(reach, min(1., REACH_LIMIT*arm/p.length(reach)))
            extended = p.add(cs['upperarm_'+s][1], reach)
            target = p.lerp(target, extended, strike_weight)
            fit_punch_shoulder(local, s, target); cs = p.to_cs(local)
        target = elbow_clearance(cs, s, target)
        pole = elbow_pole(strike_weight) if s == S else p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1])
        repair_chain(local, 'upperarm_'+s, 'lowerarm_'+s, hand, target, pole)
        straight_wrist(local, s, .8*strike_weight if s == S else 0.)
    local['root'] = ([0., 0., 0., 1.], [0., 0., 0.])
    return follow_ik(local)


def source_time(t):
    """Clip time -> source time: the stance holds through the pull-back, then the recorded outward motion is fitted
    so full extension lands on IMPACT."""
    if t <= LOAD: return PUNCH['base']
    if t <= IMPACT: return PUNCH['base']+(PUNCH['peak']-PUNCH['base'])*(t-LOAD)/(IMPACT-LOAD)
    if t <= IMPACT+HOLD: return PUNCH['peak']+(PUNCH['retract']-PUNCH['peak'])*(t-IMPACT)/HOLD
    return min(PUNCH['back'], PUNCH['retract']+(t-IMPACT-HOLD)*RETURN_SPEED)


def first_person(pose, t, length, drive, previous):
    """Fit the arms to the first-person camera guard, except the striking hand around the hit. During the
    follow-through the guard hand partly keeps its body position: with the torso turned through, the full camera-guard
    offset would pull that shoulder past its range."""
    view = copy.deepcopy(pose); cs = p.to_cs(pose)
    for s in 'lr':
        weight = 1.-.6*drive if s == O else min(1., ease((abs(t-IMPACT)-.06)/.2) if t <= IMPACT else ease((t-IMPACT-.09)/(length-IMPACT-.09)))
        target = p.add(cs['hand_'+s][1], p.mul(p.sub(FP_REST['hand_'+s][1], REST['hand_'+s][1]), weight))
        # Start from the first-person stance's shoulder (it brings the lead shoulder forward to reach the camera
        # guard); otherwise the same fist target lands differently and the clip jumps against RPG_FP_FightStance.
        view['clavicle_'+s] = (p._nlerp(pose['clavicle_'+s][0], FP_STANCE['clavicle_'+s][0], weight), view['clavicle_'+s][1])
        if s == O: target = p.add(target, [4.*drive*(1. if s == 'l' else -1.), 0., 0.])   # clear of the striking arm
        if weight > 0.: fit_punch_shoulder(view, s, target)
        # As in the first-person stance, keep an arm in the camera guard bent (the lead guard otherwise locks straight);
        # the striking arm relaxes to the punch's own reach as it leaves the guard.
        target = cap_reach(view, s, target, FP_REACH_LIMIT if s == O else FP_REACH_LIMIT+(REACH_LIMIT-FP_REACH_LIMIT)*(1.-weight))
        vcs = p.to_cs(view); target = elbow_clearance(vcs, s, target)
        rotation = p._nlerp(cs['hand_'+s][0], p.qmul(FP_REST['hand_'+s][0], p.qmul(p.qconj(REST['hand_'+s][0]), cs['hand_'+s][0])), weight)
        # In the camera guard, use the first-person stance's own elbow direction, so the clip leaves and rejoins
        # RPG_FP_FightStance without an elbow jump.
        guard_pole = p.norm(p.sub(FP_REST['lowerarm_'+s][1], FP_REST['upperarm_'+s][1]))
        # The guard arm keeps last frame's elbow direction, so it evolves continuously while the torso turns through;
        # a fixed or chest-rotated direction lines up with the arm at some frames and flips the elbow.
        home = ease((t-(length-.3))/.3)   # ease back to the stance's elbow so the last frame joins RPG_FP_FightStance
        pole = p.lerp(p.norm(previous[s]), guard_pole, home) if s == O else p.lerp(p.norm(p.sub(vcs['lowerarm_'+s][1], vcs['upperarm_'+s][1])), guard_pole, weight)
        repair_chain(view, 'upperarm_'+s, 'lowerarm_'+s, 'hand_'+s, target, pole, rotation)
        solved = p.to_cs(view); previous[s] = p.sub(solved['lowerarm_'+s][1], solved['upperarm_'+s][1])
    return follow_ik(view)


def smooth(rows, strikes):
    """Gaussian smoothing of every bone over neighbouring frames (end frames fixed), then the feet re-planted, so the
    seams between stance, pull-back, strike, hold and recovery carry no velocity pops."""
    weights = [math.exp(-.5*(k/SMOOTH_SIGMA)**2) for k in range(-SMOOTH_RADIUS, SMOOTH_RADIUS+1)]
    world = [p.to_cs(r) for r in rows]
    out = [rows[0]]
    for i in range(1, len(rows)-1):
        pose = None; total = 0.; hands = {'l': [0., 0., 0.], 'r': [0., 0., 0.]}
        for k, w in zip(range(-SMOOTH_RADIUS, SMOOTH_RADIUS+1), weights):
            j = min(len(rows)-1, max(0, i+k))
            total += w
            pose = copy.deepcopy(rows[j]) if pose is None else blend(pose, rows[j], w/total)
            for s in 'lr': hands[s] = p.add(hands[s], p.mul(world[j]['hand_'+s][1], w))
        for s in 'lr':
            cs = p.to_cs(pose); foot = 'foot_'+s
            repair_chain(pose, 'thigh_'+s, 'calf_'+s, foot, REST[foot][1], KNEE_POLE[s], REST[foot][0])
        for s in 'lr':
            # Averaged joint rotations would wobble the fist where the elbow twist changes quickly near full
            # extension: re-solve the arm onto the averaged fist path instead.
            cs = p.to_cs(pose)
            pole = elbow_pole(strikes[i]) if s == S else p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1])
            repair_chain(pose, 'upperarm_'+s, 'lowerarm_'+s, 'hand_'+s, p.mul(hands[s], 1./total), pole, cs['hand_'+s][0])
        pose['root'] = ([0., 0., 0., 1.], [0., 0., 0.])
        out.append(follow_ik(pose))
    out.append(rows[-1])
    return out


def jolt(rows, side):
    """Largest frame-to-frame change in fist velocity (cm/frame^2): a pop shows as a spike."""
    hands = [p.to_cs(r)['hand_'+side][1] for r in rows]
    return max(p.length(p.sub(p.sub(hands[i+1], hands[i]), p.sub(hands[i], hands[i-1]))) for i in range(1, len(hands)-1))


def names(side):
    return 'RPG_Punch_%s_Capture8634' % side.upper(), 'RPG_FP_Punch_%s_Capture8634' % side.upper()


def build(side):
    global BASE, BASE_CS, TURN
    use_side(side)
    BASE = sample(PUNCH['base']); BASE_CS = p.to_cs(BASE); TURN = facing_turn(BASE_CS)
    recover = HOLD+(PUNCH['back']-PUNCH['retract'])/RETURN_SPEED
    length = IMPACT+recover+SETTLE
    end_pose = recorded(PUNCH['back'], 0.)
    rows, strikes = [], []
    for i in range(round(length*FPS)+1):
        t = i/FPS
        if t <= IMPACT+recover:
            strike = 0. if t <= LOAD else ease((t-LOAD)/(IMPACT-LOAD)) if t <= IMPACT else 1.-ease((t-IMPACT)/recover)
            load = ease(t/LOAD) if t <= LOAD else 1.-ease((t-LOAD)/((IMPACT-LOAD)*UNWIND))
            pose = recorded(source_time(t), strike, load, drive_weight(t, recover))
        else:
            strike = 0.
            pose = blend(end_pose, STANCE, ease((t-IMPACT-recover)/SETTLE))
        rows.append(pose); strikes.append(strike)
    rows[0] = copy.deepcopy(STANCE); rows[-1] = copy.deepcopy(STANCE)
    rows = smooth(rows, strikes)
    previous = {s: p.sub(FP_REST['lowerarm_'+s][1], FP_REST['upperarm_'+s][1]) for s in 'lr'}
    views = [first_person(pose, i/FPS, length, drive_weight(i/FPS, recover), previous) for i, pose in enumerate(rows)]
    rows[0] = copy.deepcopy(STANCE); rows[-1] = copy.deepcopy(STANCE)
    views[0] = copy.deepcopy(FP_STANCE); views[-1] = copy.deepcopy(FP_STANCE)
    tp, fp = names(side)
    source = ('IMG_8634.mov right punch %.2f-%.2f s (peak %.2f s)' % (PUNCH['base'], PUNCH['back'], PUNCH['peak'])
              + (', mirrored to the left hand' if side == 'l' else '') + ' on the IMG_8635 stance')
    out = {}
    for name, poses in [(tp, rows), (fp, views)]:
        out[name] = dict(name=name, fps=FPS, frames=len(poses), bones=p.BONES, poses=poses, impact=IMPACT, load=LOAD,
                         keys=[0., LOAD, IMPACT, IMPACT+recover, length], hand=side, source=source, source_seconds=PUNCH)
    return out


def check(clips):
    for name, clip in clips.items():
        poses = clip['poses']; s = clip['hand']
        stance = FP_STANCE if '_FP_' in name else STANCE
        assert poses[0] == stance and poses[-1] == stance, (name, 'stance seams')
        lengths = None
        for i, local in enumerate(poses):
            assert local['root'] == [[0., 0., 0., 1.], [0., 0., 0.]] or local['root'] == ([0., 0., 0., 1.], [0., 0., 0.]), (name, i, 'root')
            for b, (q, t) in local.items():
                assert all(math.isfinite(v) for v in list(q)+list(t)), (name, i, b)
                assert abs(sum(v*v for v in q)-1.) < 1.e-4, (name, i, b, 'quaternion')
            cs = p.to_cs(local)
            for side in 'lr':
                for b in ['foot_'+side, 'ball_'+side]:
                    assert p.length(p.sub(cs[b][1], REST[b][1])) < .1, (name, i, b, 'foot slide')
            arm = [p.length(p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1])), p.length(p.sub(cs['hand_'+s][1], cs['lowerarm_'+s][1]))]
            lengths = lengths or arm
            assert max(abs(a-b) for a, b in zip(arm, lengths)) < .05, (name, i, 'arm stretched')
        hit = p.to_cs(poses[round(IMPACT*FPS)]); rest = p.to_cs(poses[0])
        print(name, clip['frames'], 'frames', round((clip['frames']-1)/FPS, 3), 's;', s, 'fist forward at impact',
              round(hit['hand_'+s][1][1]-rest['hand_'+s][1][1], 1), 'cm; pull-back',
              round(p.to_cs(poses[round(LOAD*FPS)])['hand_'+s][1][1]-rest['hand_'+s][1][1], 1), 'cm; max fist jolt',
              round(jolt(poses, s), 2), 'cm/frame^2')
        g.check_clip(name, clip)


if __name__ == '__main__':
    assert p.SP.NAME == 'uefn', 'Use RPG_SKEL=uefn'
    old = p.SOFT_IK, p.SOFT_IK_ARM; p.SOFT_IK = p.SOFT_IK_ARM = 1.e-7
    clips = {}
    try:
        for side in 'rl': clips.update(build(side))
    finally: p.SOFT_IK, p.SOFT_IK_ARM = old
    check(clips)
    (HERE/'capture_punch8634_uefn.json').write_text(json.dumps(clips))
    from preview import render
    for side in 'rl':
        tp = names(side)[0]
        render(clips[tp], str(HERE/'previews'/f'{tp}.png'), clips[tp]['keys'])
