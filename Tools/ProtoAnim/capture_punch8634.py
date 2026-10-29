"""Right and left punches from IMG_8634, fitted onto the IMG_8635 filmed stance (UEFN skeleton).

RPG_SKEL=uefn python capture_punch8634.py
The recorded arm motion is applied as a change from the recorded stance just before the punch, so the clip starts
and ends exactly in RPG_FightStance_Capture8635. The whole body is then organised around the arm as a kinetic chain
(user request: "as if these punches are real"): the hips, then the torso, then the shoulder coil back in the
pull-back and drive through in the strike, each starting a little after the one below it. The striking-side heel
lifts and the foot pivots on its ball as the hips turn, the knees bend as the weight drops and shifts forward, the
torso pivots, pitches and bends, and the shoulder rolls the elbow over as the fist turns palm-down onto the opponent's
chin. A held press waits at LOAD; impact (full extension) is at IMPACT. RPGPrototypeAnimationComponent uses the same
two times.

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
# Timing (user request: faster and snappier). The strike takes 0.13 s from the loaded pose to full extension.
LOAD, IMPACT = .16, .29
HOLD = .05                         # Clip seconds the extended arm stays out (the video holds it ~0.5 s).
RETURN_SPEED = 2.2                 # The retraction plays this much faster than recorded.
SETTLE = .2                        # Seconds to ease into the exact stance after the recorded recovery.

# Kinetic chain: per segment, how far into the strike its drive starts, when it peaks (seconds from impact), and how
# long after the load it holds before unwinding the pull-back (fraction of the strike).
CHAIN = {'hips': dict(start=0., peak=-.02, unwind=0.),
         'torso': dict(start=.15, peak=0., unwind=.15),
         'shoulder': dict(start=.35, peak=.02, unwind=.3)}
UNWIND = .6                        # Fraction of the strike over which each segment's pull-back unwinds.
DRIVE_FALL = .25                   # Seconds the follow-through takes to settle after the hold.

# Pull-back (more rear back; elbow comes back tight). Turns are degrees, + brings the striking side back.
LOAD_PELVIS_TURN = 10.
LOAD_TURN = 22.                    # Torso, over spine_01-03.
LOAD_HIPS = [0., -4., -3.]         # Weight sits back and down (mesh space: +Y forward, -X the character's right).
LOAD_FIST = [1., -12., 2.]         # Striking fist and elbow draw straight back, tight to the body (right hand).
LOAD_GUARD = [0., 2., 0.]          # The other hand holds out a little.

# Drive / follow-through.
DRIVE_PELVIS_TURN = 14.            # Hips turn through (striking hip forward), carrying the rear foot's pivot.
DRIVE_TURN = 12.                   # Torso pivot through the target, on top of the hips (with the recorded turn the chest
                                   # ends ~58 degrees through; more put the lead shoulder past its range).
DRIVE_HIPS = [3., 5., -4.]         # Weight shifts toward the lead side, forward and down: the knees bend into it.
DRIVE_LEAN = 10.                   # Torso pitches down into the punch (the head keeps 40% of it).
DRIVE_SIDE_BEND = 6.               # ... and bends toward the lead side.
DRIVE_CLAVICLE = 10.               # The striking shoulder rolls forward and drives.
FOOT_PIVOT = 35.                   # Striking-side foot turns on its ball as the hips turn (heel swings out) ...
HEEL_LIFT = 22.                    # ... with the heel lifted.

# Arm.
ELBOW_STRUCK = [-1., 0., .2]       # Elbow direction at impact: rolled out to the side, a little high (right arm).
CHIN_Z = 146.                      # Wrist height at impact: an opponent of the same height's chin.
OVERHAND_ARC = 3.                  # Extra fist height mid-strike: a slight arc over the top.
REACH_LIMIT = .97                  # Fraction of the rig's full arm length the strike reaches for.
LEFT_AIM = [2., 0., 0.]            # User request: the left punch lands 2 cm further left.
SMOOTH_RADIUS, SMOOTH_SIGMA = 2, 1.2   # Frames; temporal smoothing after the fit (tight, to keep the snap).

# Source seconds in IMG_8634 (solve frame 0 = 0.0 s). The 51.9 s punch faces the camera with the right side, so the
# striking arm is never hidden; its guard hand drifts least of the five in that view. Stance before the punch, arrival
# at full extension, start of the retraction (the video holds the arm out ~0.5 s), back in guard.
PUNCH = dict(base=51.33, peak=51.80, retract=52.27, back=52.77)

SOURCE = json.loads((HERE/'captures/IMG8634_uefn_retarget.json').read_text())
STANCES = json.loads((HERE/'capture_stance8635_uefn.json').read_text())
STANCE = STANCES['RPG_FightStance_Capture8635']['poses'][0]
FP_STANCE = STANCES['RPG_FP_FightStance_Capture8635']['poses'][0]
REST = p.to_cs(STANCE); FP_REST = p.to_cs(FP_STANCE)
# Legs keep the stance's knee direction (over the toes); the recorded thigh twist would bow a knee sideways.
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


def slerp(a, b, u):
    """Constant-speed blend between two directions (a straight-line blend rushes through near-opposite ones)."""
    angle = math.acos(max(-1., min(1., p.dot(a, b))))
    if angle < 1.e-4: return a
    return p.norm(p.add(p.mul(a, math.sin((1.-u)*angle)/math.sin(angle)), p.mul(b, math.sin(u*angle)/math.sin(angle))))


def recover_time():
    return HOLD+(PUNCH['back']-PUNCH['retract'])/RETURN_SPEED


def weights(t):
    """Per-segment pull-back (load) and follow-through (drive) amounts at clip time t, plus the arm's strike."""
    span = IMPACT-LOAD; hold_end = IMPACT+HOLD; recover = recover_time()
    w = {}
    for seg, c in CHAIN.items():
        if t <= LOAD: load = ease(t/LOAD)
        else: load = 1.-ease((t-LOAD-c['unwind']*span)/(UNWIND*span))
        start = LOAD+c['start']*span; peak = max(start+.02, IMPACT+c['peak'])
        if t <= start: drive = 0.
        elif t <= peak: drive = ease((t-start)/(peak-start))
        elif t <= max(peak, hold_end): drive = 1.
        else: drive = 1.-ease((t-max(peak, hold_end))/DRIVE_FALL)
        w[seg] = (load, drive)
    if t <= LOAD: w['strike'] = 0.
    elif t <= IMPACT: w['strike'] = ease((t-LOAD)/span)
    else: w['strike'] = 1.-ease((t-IMPACT)/recover)
    w['arc'] = math.sin(math.pi*w['strike']) if LOAD < t <= IMPACT else 0.
    return w


def foot_target(side, w):
    """Planted foot, or for the striking side, the foot pivoted on its ball (heel out and up) as the hips drive."""
    q, t = REST['foot_'+side]
    if side != S: return q, t, KNEE_POLE[side]
    drive = w['hips'][1]
    ball = REST['ball_'+side][1]
    toe = p.norm([ball[0]-t[0], ball[1]-t[1], 0.])
    # Heel swings out as the hips turn through: the foot turns the same way as the pelvis drive.
    yaw = p.qaxis([0., 0., 1.], -SIGN*FOOT_PIVOT*drive)
    lift = p.qaxis(p.norm(p.cross([0., 0., 1.], toe)), HEEL_LIFT*drive)
    rot = p.qmul(yaw, lift)
    return p.qnorm(p.qmul(rot, q)), p.add(ball, p.qrot(rot, p.sub(t, ball))), p.qrot(yaw, KNEE_POLE[side])


def plant_feet(local, w):
    for s in 'lr':
        q, t, pole = foot_target(s, w)
        repair_chain(local, 'thigh_'+s, 'calf_'+s, 'foot_'+s, t, pole, q)


def elbow_pole(turn):
    """Striking elbow direction: tucked down as in the stance through the pull-back and early strike, rolling over to
    out-to-the-side with the shoulder in the release (`turn` 0..1). Steered explicitly, because near full extension the
    elbow is barely defined and, with the torso turning through, it otherwise snaps in a few frames."""
    guard = p.norm(p.sub(REST['lowerarm_'+S][1], REST['upperarm_'+S][1]))
    return slerp(guard, p.norm(side_x(ELBOW_STRUCK)), turn)


def turn_fist(local, side, amount):
    """Pronate the striking fist toward palm-down (knuckles forward, index on the inside) by `amount`, about the
    forearm, so it lands flat on the target."""
    if amount <= 0.: return
    cs = p.to_cs(local); hand = 'hand_'+side
    axis = p.norm(p.sub(cs[hand][1], cs['lowerarm_'+side][1]))
    across = p.sub(cs['pinky_01_'+side][1], cs['index_01_'+side][1])
    across = p.norm(p.sub(across, p.mul(axis, p.dot(across, axis))))
    want = side_x([-1., 0., 0.])   # index -> pinky points to the striking side's outside
    want = p.sub(want, p.mul(axis, p.dot(want, axis)))
    if p.length(want) < 1.e-3: return
    want = p.norm(want)
    angle = math.degrees(math.atan2(p.dot(axis, p.cross(across, want)), p.dot(across, want)))
    q = p.qmul(p.qaxis(axis, angle*amount), cs[hand][0])
    local[hand] = (p.qnorm(p.qmul(p.qconj(cs[p.PARENT[hand]][0]), q)), local[hand][1])


def recorded(seconds, w):
    """Recorded pose at `seconds`, expressed as a change from BASE and applied to the filmed stance, with the body
    chain's pull-back and drive from `w` (see weights())."""
    frame = sample(seconds); source_cs = p.to_cs(frame)
    local = copy.deepcopy(STANCE)
    for bone in p.BONES:
        if bone == 'root' or any(k in bone for k in SKIP): continue
        delta = p.qmul(p.qconj(BASE[bone][0]), frame[bone][0])
        local[bone] = (p.qnorm(p.qmul(STANCE[bone][0], delta)), local[bone][1])
    (hl, hd), (tl, td), (sl, sd) = w['hips'], w['torso'], w['shoulder']
    hips = p.qrot(TURN, p.sub(source_cs['pelvis'][1], BASE_CS['pelvis'][1]))
    hips[2] = min(hips[2], 0.)   # never rise above the stance
    hips = p.add(p.add(hips, p.mul(LOAD_HIPS, hl)), p.mul(side_x(DRIVE_HIPS), hd))
    local['pelvis'] = (local['pelvis'][0], p.add(STANCE['pelvis'][1], hips))
    # Positive (times SIGN) turns bring the striking side back (pull-back); negative ones drive it through.
    pelvis_turn = SIGN*(LOAD_PELVIS_TURN*hl-DRIVE_PELVIS_TURN*hd)
    torso_turn = SIGN*(LOAD_TURN*tl-DRIVE_TURN*td)
    turn_world(local, 'pelvis', pelvis_turn)
    for bone, share in (('spine_01', .25), ('spine_02', .35), ('spine_03', .4)):
        turn_world(local, bone, torso_turn*share)
        # Pitch down (top of the chest toward +Y) and bend toward the lead side.
        turn_world(local, bone, -DRIVE_LEAN*share*td, (1., 0., 0.))
        turn_world(local, bone, SIGN*DRIVE_SIDE_BEND*share*td, (0., 1., 0.))
    # Keep the head on the target: undo the accumulated yaw at the neck, and most of the pitch and bend.
    turn_world(local, 'neck_01', -(pelvis_turn+torso_turn))
    turn_world(local, 'neck_01', .6*DRIVE_LEAN*td, (1., 0., 0.))
    turn_world(local, 'neck_01', -.6*SIGN*DRIVE_SIDE_BEND*td, (0., 1., 0.))
    turn_world(local, 'clavicle_'+S, -SIGN*DRIVE_CLAVICLE*sd)
    plant_feet(local, w)
    strike = w['strike']
    for s in 'lr':
        cs = p.to_cs(local); hand = 'hand_'+s
        target = p.add(REST[hand][1], p.qrot(TURN, p.sub(source_cs[hand][1], BASE_CS[hand][1])))
        if s == S:
            target = p.add(target, p.mul(side_x(LOAD_FIST), sl))
        else:
            target = p.add(target, p.mul(LOAD_GUARD, hl))
            # The guard fist travels with the head (covering the chin) as the body pitches and turns.
            target = p.add(target, p.sub(cs['neck_02'][1], REST['neck_02'][1]))
        if s == S and strike > 0.:
            # Toward full extension the fist follows the recorded line of the punch from the shoulder, at chin height,
            # just short of full arm length (the shoulder drive supplies the rest smoothly).
            shoulder = cs['upperarm_'+s][1]
            reach = p.qrot(TURN, p.sub(source_cs[hand][1], source_cs['upperarm_'+s][1]))
            arm = p.length(p.sub(REST['lowerarm_'+s][1], REST['upperarm_'+s][1]))+p.length(p.sub(REST[hand][1], REST['lowerarm_'+s][1]))
            length = min(p.length(reach), REACH_LIMIT*arm)
            up = max(-length*.9, min(length*.9, CHIN_Z-shoulder[2]))
            flat = p.norm([reach[0], reach[1], 0.])
            extended = p.add(shoulder, p.add(p.mul(flat, math.sqrt(length*length-up*up)), [0., 0., up]))
            if s == 'l': extended = p.add(extended, LEFT_AIM)
            target = p.lerp(target, extended, strike)
            target[2] += OVERHAND_ARC*w['arc']
            fit_punch_shoulder(local, s, target); cs = p.to_cs(local)
        target = elbow_clearance(cs, s, target)
        pole = elbow_pole(sd) if s == S else p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1])
        repair_chain(local, 'upperarm_'+s, 'lowerarm_'+s, hand, target, pole)
        straight_wrist(local, s, .8*strike if s == S else 0.)
        if s == S: turn_fist(local, s, sd)
    local['root'] = ([0., 0., 0., 1.], [0., 0., 0.])
    return follow_ik(local)


def source_time(t):
    """Clip time -> source time: the stance holds through the pull-back, then the recorded outward motion is fitted
    so full extension lands on IMPACT, a short hold, and a quicker retraction."""
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
        weight = 1.-.6*drive if s == O else min(1., ease((abs(t-IMPACT)-.04)/.12) if t <= IMPACT else ease((t-IMPACT-.06)/(length-IMPACT-.06)))
        target = p.add(cs['hand_'+s][1], p.mul(p.sub(FP_REST['hand_'+s][1], REST['hand_'+s][1]), weight))
        # Start from the first-person stance's shoulder (it brings the lead shoulder forward to reach the camera
        # guard); otherwise the same fist target lands differently and the clip jumps against RPG_FP_FightStance.
        view['clavicle_'+s] = (p._nlerp(pose['clavicle_'+s][0], FP_STANCE['clavicle_'+s][0], weight), view['clavicle_'+s][1])
        if s == O: target = p.add(target, [4.*drive*(1. if s == 'l' else -1.), 0., 0.])   # clear of the striking arm
        if weight > 0.: fit_punch_shoulder(view, s, target)
        # Keep an arm in the camera guard bent (the lead guard otherwise locks straight); the striking arm relaxes to
        # the punch's own reach as it leaves the guard.
        target = cap_reach(view, s, target, FP_REACH_LIMIT if s == O else FP_REACH_LIMIT+(REACH_LIMIT-FP_REACH_LIMIT)*(1.-weight))
        vcs = p.to_cs(view); target = elbow_clearance(vcs, s, target)
        rotation = p._nlerp(cs['hand_'+s][0], p.qmul(FP_REST['hand_'+s][0], p.qmul(p.qconj(REST['hand_'+s][0]), cs['hand_'+s][0])), weight)
        guard_pole = p.norm(p.sub(FP_REST['lowerarm_'+s][1], FP_REST['upperarm_'+s][1]))
        # The guard arm keeps last frame's elbow direction, so it evolves continuously while the torso turns through;
        # it eases back to the stance's elbow over the last 0.3 s so the clip joins RPG_FP_FightStance.
        home = ease((t-(length-.3))/.3)
        pole = p.lerp(p.norm(previous[s]), guard_pole, home) if s == O else p.lerp(p.norm(p.sub(vcs['lowerarm_'+s][1], vcs['upperarm_'+s][1])), guard_pole, weight)
        repair_chain(view, 'upperarm_'+s, 'lowerarm_'+s, 'hand_'+s, target, pole, rotation)
        solved = p.to_cs(view); previous[s] = p.sub(solved['lowerarm_'+s][1], solved['upperarm_'+s][1])
    return follow_ik(view)


def smooth(rows, ws):
    """Gaussian smoothing of every bone over neighbouring frames (end frames fixed), then the feet re-planted and the
    arms re-solved onto the averaged fist path (averaged joint rotations wobble the fist where the elbow turns over)."""
    kernel = [math.exp(-.5*(k/SMOOTH_SIGMA)**2) for k in range(-SMOOTH_RADIUS, SMOOTH_RADIUS+1)]
    world = [p.to_cs(r) for r in rows]
    out = [rows[0]]
    for i in range(1, len(rows)-1):
        pose = None; total = 0.; hands = {'l': [0., 0., 0.], 'r': [0., 0., 0.]}
        for k, w in zip(range(-SMOOTH_RADIUS, SMOOTH_RADIUS+1), kernel):
            j = min(len(rows)-1, max(0, i+k))
            total += w
            pose = copy.deepcopy(rows[j]) if pose is None else blend(pose, rows[j], w/total)
            for s in 'lr': hands[s] = p.add(hands[s], p.mul(world[j]['hand_'+s][1], w))
        plant_feet(pose, ws[i])
        for s in 'lr':
            cs = p.to_cs(pose)
            pole = elbow_pole(ws[i]['shoulder'][1]) if s == S else p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1])
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
    recover = recover_time()
    length = IMPACT+recover+SETTLE
    rest_w = weights(length)
    end_pose = recorded(PUNCH['back'], weights(IMPACT+recover))
    rows, ws = [], []
    for i in range(round(length*FPS)+1):
        t = i/FPS; w = weights(t)
        if t <= IMPACT+recover: pose = recorded(source_time(t), w)
        else:
            pose = blend(end_pose, STANCE, ease((t-IMPACT-recover)/SETTLE)); w = rest_w
        rows.append(pose); ws.append(w)
    rows[0] = copy.deepcopy(STANCE); rows[-1] = copy.deepcopy(STANCE)
    rows = smooth(rows, ws)
    previous = {s: p.sub(FP_REST['lowerarm_'+s][1], FP_REST['upperarm_'+s][1]) for s in 'lr'}
    # The guard arm makes room on the torso's timing (it turns through before the shoulder).
    views = [first_person(pose, i/FPS, length, ws[i]['torso'][1], previous) for i, pose in enumerate(rows)]
    rows[0] = copy.deepcopy(STANCE); rows[-1] = copy.deepcopy(STANCE)
    views[0] = copy.deepcopy(FP_STANCE); views[-1] = copy.deepcopy(FP_STANCE)
    tp, fp = names(side)
    source = ('IMG_8634.mov right punch %.2f-%.2f s (peak %.2f s)' % (PUNCH['base'], PUNCH['back'], PUNCH['peak'])
              + (', mirrored to the left hand' if side == 'l' else '') + ' on the IMG_8635 stance, whole-body chain')
    out = {}
    for name, poses in [(tp, rows), (fp, views)]:
        out[name] = dict(name=name, fps=FPS, frames=len(poses), bones=p.BONES, poses=poses, impact=IMPACT, load=LOAD,
                         keys=[0., LOAD, IMPACT, IMPACT+recover, length], hand=side, source=source, source_seconds=PUNCH)
    return out


def check(clips):
    for name, clip in clips.items():
        poses = clip['poses']; s = clip['hand']; lead = 'l' if s == 'r' else 'r'
        stance = FP_STANCE if '_FP_' in name else STANCE
        assert poses[0] == stance and poses[-1] == stance, (name, 'stance seams')
        lengths = None; heel = 0.
        for i, local in enumerate(poses):
            assert local['root'] == [[0., 0., 0., 1.], [0., 0., 0.]] or local['root'] == ([0., 0., 0., 1.], [0., 0., 0.]), (name, i, 'root')
            for b, (q, t) in local.items():
                assert all(math.isfinite(v) for v in list(q)+list(t)), (name, i, b)
                assert abs(sum(v*v for v in q)-1.) < 1.e-4, (name, i, b, 'quaternion')
            cs = p.to_cs(local)
            for b in ['foot_'+lead, 'ball_'+lead]:
                assert p.length(p.sub(cs[b][1], REST[b][1])) < .1, (name, i, b, 'lead foot slide')
            assert p.length(p.sub(cs['ball_'+s][1], REST['ball_'+s][1])) < .3, (name, i, 'pivot ball slide')
            heel = max(heel, cs['foot_'+s][1][2]-REST['foot_'+s][1][2])
            arm = [p.length(p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1])), p.length(p.sub(cs['hand_'+s][1], cs['lowerarm_'+s][1]))]
            lengths = lengths or arm
            assert max(abs(a-b) for a, b in zip(arm, lengths)) < .05, (name, i, 'arm stretched')
        hit = p.to_cs(poses[round(IMPACT*FPS)]); rest = p.to_cs(poses[0])
        print(name, clip['frames'], 'frames', round((clip['frames']-1)/FPS, 3), 's;', s, 'fist forward at impact',
              round(hit['hand_'+s][1][1]-rest['hand_'+s][1][1], 1), 'cm, height', round(hit['hand_'+s][1][2], 1),
              'cm; pull-back', round(p.to_cs(poses[round(LOAD*FPS)])['hand_'+s][1][1]-rest['hand_'+s][1][1], 1),
              'cm; heel lift', round(heel, 1), 'cm; max fist jolt', round(jolt(poses, s), 2), 'cm/frame^2')
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
