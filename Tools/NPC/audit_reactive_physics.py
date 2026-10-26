"""Read-only audit of the actual NPC mesh, animation class, bodies and constraint limits."""
import json, os, unreal
bp = unreal.load_asset('/Game/RPG/NPC/BP_RPGNpc')
cdo = unreal.get_default_object(bp.generated_class())
mesh = cdo.get_editor_property('mesh')
skm = mesh.get_editor_property('skeletal_mesh_asset')
asset = mesh.get_editor_property('physics_asset_override') or skm.get_editor_property('physics_asset')
data = dict(blueprint=bp.get_path_name(), mesh=skm.get_path_name(),
            skeleton=skm.get_editor_property('skeleton').get_path_name(),
            anim_class=str(mesh.get_editor_property('anim_class')), physics_asset=asset.get_path_name(),
            bodies=[], constraints=[])
for b in unreal.ObjectIterator(unreal.Object):
    if b.get_outer() != asset or b.get_class().get_name() != 'SkeletalBodySetup': continue
    data['bodies'].append(dict(bone=str(b.get_editor_property('bone_name')),
                               physics_type=str(b.get_editor_property('physics_type'))))
# PhysicsAsset's body/constraint arrays are not exposed to Python in this build.
# The integration test additionally records the actual C++ body/constraint table.
data['constraint_audit'] = 'See C++ runtime audit in ReactivePhysics test log.'
dest = os.path.join(unreal.Paths.project_saved_dir(), 'ReactivePhysics', 'asset_audit.json')
os.makedirs(os.path.dirname(dest), exist_ok=True)
with open(dest, 'w', encoding='utf-8') as f: json.dump(data, f, indent=2)
unreal.log('REACTIVE ASSET AUDIT ' + json.dumps(data))
