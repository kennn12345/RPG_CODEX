"""Close the see-through cracks at mossy rock corners (user request 2026-11-09).

Cause: M_Rock_Stone_Moss raises the moss 3 cm (+2 cm bump) with World Position Offset, pushing each vertex along its own
normal and gating the amount on its own N.z. At sharp corners one position has several normals (the weathered rocks
use split normals at 42 degrees), so the up-facing copy rose ~3 cm while the side-facing copy stayed put, tearing a
see-through crack along the corner. Only rendered geometry shows it; the mesh itself is closed.
Fix: the same lift for every copy - straight up, amount from per-vertex data only (vertex colour A = BroadUp, VC.g and
world-position noise). Edits the existing material's custom node in place (the builder, build_vista_assets.py, carries
the same MOSS_LIFT code). Stage of Tools/World/apply_ground_fixes.py; saves only with save=True (close the editor first)."""
import unreal, re

MAT = '/Game/RPG/Environment/Rocks/Materials/M_Rock_Stone_Moss'
OLD_MARK = 'return N * smoothstep(0.3, 0.46, raw)'

def _moss_lift_code():
    src = open('C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/build_vista_assets.py', encoding='utf-8').read()
    m = re.search(r"MOSS_LIFT = r'''(.*?)'''", src, re.S)
    return m.group(1) if m else None

def fix_rock_corners(save=False):
    m = unreal.load_asset(MAT)
    new_body = _moss_lift_code()
    if not m or not new_body:
        unreal.log_warning('ROCKFIX material or MOSS_LIFT code not found'); return
    hits = 0
    for e in unreal.MaterialEditingLibrary.get_material_expressions(m) if hasattr(unreal.MaterialEditingLibrary, 'get_material_expressions') else []:
        if not isinstance(e, unreal.MaterialExpressionCustom): continue
        code = e.get_editor_property('code')
        if OLD_MARK in code:
            head = code[:code.index('float raw')] if 'float raw' in code else ''
            e.set_editor_property('code', head + new_body.lstrip('\n')); hits += 1
        elif 'float3(0, 0, 1) * smoothstep(0.3, 0.46, raw)' in code:
            unreal.log('ROCKFIX moss lift already fixed'); return
    if not hits:
        unreal.log_warning('ROCKFIX moss lift node not found'); return
    unreal.MaterialEditingLibrary.recompile_material(m)
    if save: assert unreal.EditorAssetLibrary.save_loaded_asset(m, only_if_is_dirty=False), 'M_Rock_Stone_Moss save failed (editor open?)'
    unreal.log(f'ROCKFIX moss lift now straight up and shared by every vertex copy ({hits} node), saved={save}')
