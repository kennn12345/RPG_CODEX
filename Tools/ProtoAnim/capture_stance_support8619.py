"""Fit the existing fist-fighting curves to UEFN for captured-stance handoffs.

Existing Manny assets, flight, kicks, timing and strike windows are untouched.
RPG_SKEL=uefn python capture_stance_support8619.py
"""
import contextlib
import io
import json
from pathlib import Path
with contextlib.redirect_stdout(io.StringIO()):
    import poses
    import fpanim
import protoanim as p

assert p.SP.NAME=='uefn','Use RPG_SKEL=uefn'
clips={}
for name in ['RPG_Proto_Punch_R','RPG_Proto_Punch_L','RPG_Proto_Punch_R_Charged','RPG_Proto_Punch_L_Charged','RPG_Proto_UnarmedGuard']:
    clips[name]=poses.bake_spec(name)
for name,keys,impact,arm in fpanim.FP_CLIPS:
    if name not in ['RPG_FP_Punch_R','RPG_FP_Punch_L','RPG_FP_Punch_R_Charged','RPG_FP_Punch_L_Charged','RPG_FP_UnarmedGuard']:continue
    _,_,springs,pins=poses.SPECS[name.replace('RPG_FP_','RPG_Proto_')]
    guard=name=='RPG_FP_UnarmedGuard'
    clip=fpanim.fp_bake(name,keys,impact,arm,springs=springs,pins=pins,
        keep_until=fpanim.SWITCH.get(name,0.),shift=fpanim.GUARD_SHIFT if guard else None,
        poles=fpanim.FP_GUARD_POLE if guard else None,clav=fpanim.FP_GUARD_CLAV if guard else None)
    clip['keys']=[k[0] for k in keys]
    clips[name]=clip
(Path(__file__).parent/'capture_stance_support8619_uefn.json').write_text(json.dumps(clips))
print('UEFN fist-fighting support:',', '.join(clips))
