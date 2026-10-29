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
         'torso': dict(start=.1, peak=0., unwind=.1),
         'shoulder': dict(start=.22, peak=.02, unwind=.2)}   # earlier: the arm now drives out straight from the wind-up
UNWIND = .6                        # Fraction of the strike over which each segment's pull-back unwinds.
DRIVE_FALL = .25                   # Seconds the follow-through takes to settle after the hold.

# Pull-back (more rear back; elbow comes back tight). Turns are degrees, + brings the striking side back.
LOAD_PELVIS_TURN = 10.
LOAD_TURN = 22.                    # Torso, over spine_01-03.
LOAD_HIPS = [0., -4., -3.]         # Weight sits back and down (mesh space: +Y forward, -X the character's right).
# Pull-back arm (user request: the elbow rears back past the line of the back by ~4 cm, down toward the lat, then rolls
# into the overhand from there). Measured as seen from the side, along the line to the opponent: the shoulder blade
# sits ~8 cm behind the shoulder joint, so the elbow goes 12 cm behind the joint. (Measured in the chest's own frame
# instead, it compounded with the torso's turn back and folded the arm into the chest.) With the elbow that far back,
# a forearm folded up beside the shoulder would over-bend the elbow, so the forearm angles forward at a natural bend
# and the fist cocks at chest level, in line with the forearm.
LOAD_CLAVICLE = 8.                 # The striking shoulder blade retracts with the rearing elbow (chest opens a little).
# Where the back line is, seen from the side: whichever is further back of the striking shoulder blade (~8 cm behind
# its joint) and the mid back (~11 cm behind spine_04, measured on SKM_UEFN_Mannequin). On the lead side the shoulder
# starts further forward in the stance, so a fixed distance behind the joint left the left elbow in front of the back.
SHOULDER_BLADE_DEPTH = 8.
BACKLINE_DEPTH = 11.
ELBOW_PAST_BACK = 10.              # Wound-up elbow past the back line. First asked for ~4 cm; the third sketch draws it
                                   # further back and lower (a V: elbow back and down, fist pulled in by the shoulder).
                                   # Smoothing with the frames either side takes ~2.5 cm off the target.
LOAD_ELBOW_OUT = 11.               # ... and out to the striking side (user's sketches: flared out, seen from the front);
                                   # the rest of the upper arm's length goes down.
LOAD_ELBOW_BEND = 138.             # Elbow bend in the wound-up arm (degrees; ~145 is the natural limit): folded, so the fist
                                   # sits in close by the shoulder.
# Strike path (user's sketch on the six-angle sheet): from the wind-up the elbow comes forward and under, the forearm
# angled up with the fist leading; then the upper arm comes level with the forearm upright (the top of the overhand);
# then the arm extends straight onto the chin. Keys relative to the striking shoulder, in (forward, out, up) cm.
# Fists are interpolated on a smooth curve through the keys, elbows by slerp between the keyed directions.
# Second sketch (both hands): at 0.250 s the arm is nearly straight, reaching up and forward at ~25-30 degrees above
# the target, then comes down onto the chin at contact. (Keys sit a little early so that after smoothing the arm
# reaches the sketched positions at 0.217 / 0.250 s.)
# Third sketch: from the wind-up the arm drives straight out forward and up on a diagonal (already mostly extended at
# 0.217 s, no "forward and under" phase), carries on up and forward at 0.250 s, then comes down onto the chin.
STRIKE_KEYS = [(.207, dict(elbow=[18., 7., 2.], fist=[35., 2., 14.])),       # driving out forward and up
               (.243, dict(elbow=[22., 5., 11.], fist=[41., 1., 22.]))]      # nearly straight, up and forward
GUARD_ELBOW = [.25, 0., -1.]       # Guard elbow under its fist (forearm upright), a little out (guard side).
LOAD_GUARD = [0., 2., 0.]          # The other hand holds out a little.

# Drive / follow-through.
DRIVE_PELVIS_TURN = 14.            # Hips turn through (striking hip forward), carrying the rear foot's pivot.
DRIVE_TURN = 12.                   # Torso pivot through the target, on top of the hips (with the recorded turn the chest
                                   # ends ~58 degrees through; more put the lead shoulder past its range).
DRIVE_HIPS = [3., 5., -4.]         # Weight shifts toward the lead side, forward and down: the knees bend into it.
DRIVE_LEAN = 10.                   # Torso pitches down into the punch (the head keeps 40% of it).
DRIVE_SIDE_BEND = 6.               # ... and bends toward the lead side.
DRIVE_CLAVICLE = 10.               # The striking shoulder rolls forward and drives.
# Striking-side foot turns on its ball as the hips turn (heel swings out), with the heel lifted. The left punch's
# foot is the lead foot: user request, only a very small pivot there, so the lead knee barely turns.
FOOT_PIVOT = {'r': 35., 'l': 4.}
HEEL_LIFT = {'r': 22., 'l': 3.}

# Follow-through (user request): the punch angles down through the target. Contact is unchanged (straight ahead at
# the chin); just after it the fist carries on forward and down while the striking shoulder drives further forward
# and the torso pivots down over that shoulder to extend it, then everything retracts.
FOLLOW_PEAK = .09                  # Seconds after impact the follow-through peaks ...
FOLLOW_FALL = .14                  # ... and how long it takes to fade back into the retraction.
FOLLOW_FIST = [-1., 6., -8.]       # Fist travel past the target: forward and down, slightly inside (right hand).
FOLLOW_PITCH = 7.                  # Extra forward pitch of the torso, over spine_01-03.
FOLLOW_ROLL = 8.                   # Torso rolls down toward the striking side (that shoulder dips forward and down).
FOLLOW_CLAVICLE = 8.               # Extra striking-shoulder drive.
FOLLOW_HIPS = [0., 2., -1.5]       # Hips carry a little further forward and down.

# Arm.
ELBOW_STRUCK = [-1., 0., .3]       # Elbow direction at impact: rolled up and out over the top (right arm).
CHIN_Z = 146.                      # Wrist height at impact: an opponent of the same height's chin.
OVERHAND_ARC = 5.                  # Extra fist height mid-strike: the fist comes in from above, angling down onto the chin.
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


def short_nlerp(a, b, u):
    """Quaternion blend the short way round (a flipped-sign target otherwise spins the long way)."""
    if sum(x*y for x, y in zip(a, b)) < 0.: b = [-v for v in b]
    return p.qnorm([x+(y-x)*u for x, y in zip(a, b)])


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
    # Starts from rest but arrives with speed (2u^2 - u^3 has zero slope at the start, full slope at the end), so the
    # punch carries through contact into the follow-through instead of stopping dead on the target.
    elif t <= IMPACT: u = (t-LOAD)/span; w['strike'] = 2.*u*u-u*u*u
    else: w['strike'] = 1.-ease((t-IMPACT)/recover)
    w['arc'] = math.sin(math.pi*w['strike']) if LOAD < t <= IMPACT else 0.
    # Fists straighten into line with the forearms over the pull-back and stay there (the stance's wrist angles bent
    # them up to 50 degrees off the forearm once the arms moved); the settle blends back to the stance.
    w['wrist'] = ease(t/LOAD) if t <= LOAD else 1.-ease((t-IMPACT-recover)/SETTLE)
    w['t'] = t
    # Follow-through: zero at contact, peaks just after, fades into the retraction.
    if t <= IMPACT: w['follow'] = 0.
    elif t <= IMPACT+FOLLOW_PEAK: w['follow'] = math.sin(.5*math.pi*(t-IMPACT)/FOLLOW_PEAK)   # leaves contact moving
    else: w['follow'] = 1.-ease((t-IMPACT-FOLLOW_PEAK)/FOLLOW_FALL)
    return w


def foot_target(side, w):
    """Planted foot, or for the striking side, the foot pivoted on its ball (heel out and up) as the hips drive."""
    q, t = REST['foot_'+side]
    if side != S: return q, t, KNEE_POLE[side]
    drive = w['hips'][1]
    ball = REST['ball_'+side][1]
    toe = p.norm([ball[0]-t[0], ball[1]-t[1], 0.])
    # Heel swings out as the hips turn through: the foot turns the same way as the pelvis drive.
    yaw = p.qaxis([0., 0., 1.], -SIGN*FOOT_PIVOT[side]*drive)
    lift = p.qaxis(p.norm(p.cross([0., 0., 1.], toe)), HEEL_LIFT[side]*drive)
    rot = p.qmul(yaw, lift)
    return p.qnorm(p.qmul(rot, q)), p.add(ball, p.qrot(rot, p.sub(t, ball))), p.qrot(yaw, KNEE_POLE[side])


def plant_feet(local, w):
    for s in 'lr':
        q, t, pole = foot_target(s, w)
        repair_chain(local, 'thigh_'+s, 'calf_'+s, 'foot_'+s, t, pole, q)


def wound_arm(cs):
    """The wound-up striking arm in the current pose: elbow ELBOW_BEHIND the shoulder joint (toward the back, along
    the line to the opponent) and down, forearm angled forward at LOAD_ELBOW_BEND. Returns (elbow, fist)."""
    fwd = [0., 1., 0.]; out = side_x([-1., 0., 0.])
    shoulder = cs['upperarm_'+S][1]
    upper = p.length(p.sub(REST['lowerarm_'+S][1], REST['upperarm_'+S][1]))
    fore = p.length(p.sub(REST['hand_'+S][1], REST['lowerarm_'+S][1]))
    back_line = min(shoulder[1]-SHOULDER_BLADE_DEPTH, cs['spine_04'][1][1]-BACKLINE_DEPTH)
    behind = min(shoulder[1]-(back_line-ELBOW_PAST_BACK), .75*upper)   # keep the upper arm angled down, not straight back
    down = math.sqrt(max(0., upper*upper-behind**2-LOAD_ELBOW_OUT**2))
    elbow = p.add(shoulder, p.add(p.add(p.mul(fwd, -behind), p.mul(out, LOAD_ELBOW_OUT)), [0., 0., -down]))
    # Forearm: from the folded-back direction (toward the shoulder), opened forward by (180 - bend) degrees.
    back_up = p.norm(p.sub(shoulder, elbow))
    ahead = p.norm(p.sub(fwd, p.mul(back_up, p.dot(fwd, back_up))))
    opening = math.radians(180.-LOAD_ELBOW_BEND)
    forearm = p.norm(p.add(p.mul(back_up, math.cos(opening)), p.mul(ahead, math.sin(opening))))
    return elbow, p.add(elbow, p.mul(forearm, fore))


def axes():
    """World (forward, out, up) for the striking side: forward is toward the opponent (mesh +Y)."""
    return [0., 1., 0.], side_x([-1., 0., 0.]), [0., 0., 1.]


def from_axes(v):
    f, o, u = axes()
    return p.add(p.add(p.mul(f, v[0]), p.mul(o, v[1])), p.mul(u, v[2]))


def strike_path(t, shoulder, wound_elbow, wound_fist, extended):
    """Fist target and elbow direction between the wind-up (LOAD) and contact (IMPACT), through STRIKE_KEYS.
    Fist: cubic Hermite through the keys (relative to the shoulder), leaving the wind-up from rest and arriving at
    the chin with speed. Elbow: slerp between the keyed directions."""
    upper = p.length(p.sub(REST['lowerarm_'+S][1], REST['upperarm_'+S][1]))
    def bend_hint(elbow, fist):
        # Only the part of the elbow off the shoulder-fist line says which way to bend; near a straight arm the
        # elbow's own direction runs along that line and the solver flips.
        line = p.norm(fist)
        off = p.sub(elbow, p.mul(line, p.dot(elbow, line)))
        return p.norm(off) if p.length(off) > 1. else p.norm(side_x(ELBOW_STRUCK))
    keys = [(LOAD, p.sub(wound_fist, shoulder), bend_hint(p.sub(wound_elbow, shoulder), p.sub(wound_fist, shoulder)))]
    for kt, k in STRIKE_KEYS:
        keys.append((kt, from_axes(k['fist']), bend_hint(from_axes(k['elbow']), from_axes(k['fist']))))
    keys.append((IMPACT, p.sub(extended, shoulder), p.norm(side_x(ELBOW_STRUCK))))
    i = max(j for j in range(len(keys)-1) if keys[j][0] <= t)
    (t0, p0, e0), (t1, p1, e1) = keys[i], keys[i+1]
    dt = t1-t0; u = (t-t0)/dt

    def tangent(j):
        if j == 0: return [0., 0., 0.]
        if j == len(keys)-1: return p.mul(p.sub(keys[j][1], keys[j-1][1]), 1./(keys[j][0]-keys[j-1][0]))
        return p.mul(p.sub(keys[j+1][1], keys[j-1][1]), 1./(keys[j+1][0]-keys[j-1][0]))
    m0, m1 = p.mul(tangent(i), dt), p.mul(tangent(i+1), dt)
    h00, h10, h01, h11 = 2*u**3-3*u*u+1, u**3-2*u*u+u, -2*u**3+3*u*u, u**3-u*u
    rel = p.add(p.add(p.mul(p0, h00), p.mul(m0, h10)), p.add(p.mul(p1, h01), p.mul(m1, h11)))
    # Constant-speed roll between keys: an eased roll doubled the peak twist where the elbow turns from down to out.
    return p.add(shoulder, rel), slerp(e0, e1, u)


def elbow_pole(turn, load=0., wound=None):
    """Striking elbow direction: rearing back behind the back line in the pull-back (`load`, toward the wound-up
    elbow `wound`), then rolling up, out and over with the shoulder in the release (`turn`). Steered explicitly,
    because near full extension the elbow is barely defined and, with the torso turning through, it otherwise snaps."""
    guard = p.norm(p.sub(REST['lowerarm_'+S][1], REST['upperarm_'+S][1]))
    reared = slerp(guard, p.norm(wound), load) if wound is not None else guard
    return slerp(reared, p.norm(side_x(ELBOW_STRUCK)), turn)


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
    fo = w['follow']
    hips = p.qrot(TURN, p.sub(source_cs['pelvis'][1], BASE_CS['pelvis'][1]))
    hips[2] = min(hips[2], 0.)   # never rise above the stance
    hips = p.add(p.add(hips, p.mul(LOAD_HIPS, hl)), p.mul(side_x(DRIVE_HIPS), hd))
    hips = p.add(hips, p.mul(FOLLOW_HIPS, fo))
    local['pelvis'] = (local['pelvis'][0], p.add(STANCE['pelvis'][1], hips))
    # Positive (times SIGN) turns bring the striking side back (pull-back); negative ones drive it through.
    pelvis_turn = SIGN*(LOAD_PELVIS_TURN*hl-DRIVE_PELVIS_TURN*hd)
    torso_turn = SIGN*(LOAD_TURN*tl-DRIVE_TURN*td)
    turn_world(local, 'pelvis', pelvis_turn)
    for bone, share in (('spine_01', .25), ('spine_02', .35), ('spine_03', .4)):
        turn_world(local, bone, torso_turn*share)
        # Pitch down (top of the chest toward +Y) and bend toward the lead side; in the follow-through, pitch further
        # and roll down over the striking shoulder.
        pitch = DRIVE_LEAN*td+FOLLOW_PITCH*fo
        bend = SIGN*(DRIVE_SIDE_BEND*td-FOLLOW_ROLL*fo)
        turn_world(local, bone, -pitch*share, (1., 0., 0.))
        turn_world(local, bone, bend*share, (0., 1., 0.))
    # Keep the head on the target: undo the accumulated yaw at the neck, and most of the pitch and bend.
    turn_world(local, 'neck_01', -(pelvis_turn+torso_turn))
    turn_world(local, 'neck_01', .6*(DRIVE_LEAN*td+FOLLOW_PITCH*fo), (1., 0., 0.))
    turn_world(local, 'neck_01', -.6*SIGN*(DRIVE_SIDE_BEND*td-FOLLOW_ROLL*fo), (0., 1., 0.))
    turn_world(local, 'clavicle_'+S, SIGN*(LOAD_CLAVICLE*sl-DRIVE_CLAVICLE*sd-FOLLOW_CLAVICLE*fo))
    plant_feet(local, w)
    strike = w['strike']
    for s in 'lr':
        cs = p.to_cs(local); hand = 'hand_'+s
        target = p.add(REST[hand][1], p.qrot(TURN, p.sub(source_cs[hand][1], BASE_CS[hand][1])))
        if s == S:
            wound_elbow, wound_fist = wound_arm(cs)
            target = p.lerp(target, wound_fist, sl)
        else:
            target = p.add(target, p.mul(LOAD_GUARD, hl))
            # The guard fist travels with the head (covering the chin) as the body pitches and turns.
            target = p.add(target, p.sub(cs['neck_02'][1], REST['neck_02'][1]))
        pole = None
        if s == S and strike > 0.:
            # Contact: the fist on the recorded line of the punch from the shoulder, at chin height, just short of
            # full arm length (the shoulder drive supplies the rest smoothly).
            shoulder = cs['upperarm_'+s][1]
            reach = p.qrot(TURN, p.sub(source_cs[hand][1], source_cs['upperarm_'+s][1]))
            arm = p.length(p.sub(REST['lowerarm_'+s][1], REST['upperarm_'+s][1]))+p.length(p.sub(REST[hand][1], REST['lowerarm_'+s][1]))
            length = min(p.length(reach), REACH_LIMIT*arm)
            up = max(-length*.9, min(length*.9, CHIN_Z-shoulder[2]))
            flat = p.norm([reach[0], reach[1], 0.])
            extended = p.add(shoulder, p.add(p.mul(flat, math.sqrt(length*length-up*up)), [0., 0., up]))
            if s == 'l': extended = p.add(extended, LEFT_AIM)
            if w['t'] <= IMPACT:
                # Wind-up to contact along the sketched overhand path.
                target, pole = strike_path(w['t'], shoulder, wound_elbow, wound_fist, extended)
            else:
                target = p.lerp(target, extended, strike)
                target = p.add(target, p.mul(side_x(FOLLOW_FIST), fo))
            fit_punch_shoulder(local, s, target); cs = p.to_cs(local)
        target = elbow_clearance(cs, s, target)
        if s == S:
            pole = pole or elbow_pole(sd, sl, p.sub(wound_elbow, cs['upperarm_'+s][1]))
            w['pole'] = pole   # the smoothing pass re-solves the arm with the same elbow direction
        else:
            # Guard forearm upright: the elbow under the fist, a little out (eased in over the pull-back).
            out = [-v for v in side_x([-1., 0., 0.])]
            pole = slerp(p.norm(p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1])),
                         p.norm(p.add(p.mul(out, GUARD_ELBOW[0]), [0., 0., GUARD_ELBOW[2]])), w['wrist'])
            w['guard_pole'] = pole
        repair_chain(local, 'upperarm_'+s, 'lowerarm_'+s, hand, target, pole)
        straight_wrist(local, s, w['wrist'])
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
    # The striking arm follows the body clip (it winds up low, out of view, and snaps into view as it fires); it eases
    # from and back to the camera guard only at the ends. Holding it in the forward camera guard while the body winds
    # it back made the arm thrash.
    home_end = min(1., 1.-ease(t/LOAD)+ease((t-(length-SETTLE))/SETTLE))
    for s in 'lr':
        weight = 1.-.6*drive if s == O else home_end
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
        view_rotation = p.qmul(FP_REST['hand_'+s][0], p.qmul(p.qconj(REST['hand_'+s][0]), cs['hand_'+s][0]))
        rotation = short_nlerp(cs['hand_'+s][0], view_rotation, weight)
        guard_pole = p.norm(p.sub(FP_REST['lowerarm_'+s][1], FP_REST['upperarm_'+s][1]))
        # The guard arm keeps last frame's elbow direction, so it evolves continuously while the torso turns through;
        # it eases back to the stance's elbow over the last 0.3 s so the clip joins RPG_FP_FightStance.
        home = ease((t-(length-.3))/.3)
        # (Striking arm by slerp: with the elbow reared back, the body's and the camera guard's elbow directions are
        # nearly opposite and a straight-line blend collapses through zero and flips the arm.)
        pole = p.lerp(p.norm(previous[s]), guard_pole, home) if s == O else slerp(p.norm(p.sub(vcs['lowerarm_'+s][1], vcs['upperarm_'+s][1])), guard_pole, weight)
        repair_chain(view, 'upperarm_'+s, 'lowerarm_'+s, 'hand_'+s, target, pole, rotation)
        # Fists in line with the forearms, as in the body clip; eased in over the pull-back and out over the settle so
        # the clip still starts and ends on RPG_FP_FightStance.
        straight_wrist(view, s, ease(t/LOAD)*(1.-ease((t-(length-SETTLE))/SETTLE)))
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
            if s == S: pole = ws[i].get('pole') or elbow_pole(ws[i]['shoulder'][1], ws[i]['shoulder'][0], p.sub(wound_arm(cs)[0], cs['upperarm_'+s][1]))
            else: pole = ws[i].get('guard_pole') or p.sub(cs['lowerarm_'+s][1], cs['upperarm_'+s][1])
            repair_chain(pose, 'upperarm_'+s, 'lowerarm_'+s, 'hand_'+s, p.mul(hands[s], 1./total), pole, cs['hand_'+s][0])
            # The averaged hand angle no longer matches the re-solved forearm: straighten the fist again (it read as
            # crooked at 0.217 s).
            straight_wrist(pose, s, ws[i]['wrist'])
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
    end_pose = recorded(PUNCH['back'], weights(IMPACT+recover))
    rows, ws = [], []
    for i in range(round(length*FPS)+1):
        t = i/FPS; w = weights(t)
        if t <= IMPACT+recover: pose = recorded(source_time(t), w)
        else: pose = blend(end_pose, STANCE, ease((t-IMPACT-recover)/SETTLE))
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
